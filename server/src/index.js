// The Drenched activation server, a Cloudflare Worker. It knows every licence
// number sold, gives each one up to three computers, and signs the activation
// codes the plugin checks offline. See README.md for the API and deployment.

import { COMPUTERS_PER_LICENCE, activationCode, cleanName, importSigningKey, makeKey, parseKey, parseMachineCode } from './licence.js';
import { licenceEmail, resendMailer } from './mail.js';
import { copiesIn, fetchCustomer, verifySignature } from './paddle.js';
import { activationPage, thanksPage } from './pages.js';
import { d1Store } from './stores.js';

/** Computers a licence may release (to make room for another) in any 365 days. */
export const MOVES_PER_YEAR = 6;

const MAX_BODY = 4096;

const MESSAGES = {
    'invalid-licence': 'That is not a Drenched licence number. Check it against your purchase email.',
    'invalid-machine': 'That is not a Drenched computer code.',
    'unknown-licence': 'That licence number is not one we sold. Check it against your purchase email.',
    'revoked': 'This licence number has been withdrawn. Contact support if you think that is a mistake.',
    'computer-limit': `This licence is already active on ${COMPUTERS_PER_LICENCE} computers. Deactivate one of them in Drenched first.`,
    'too-many-moves': 'This licence has moved between computers too often this year. Contact support and we will sort it out.',
    'bad-request': 'The request was not understood.',
    'not-found': 'Not found.',
};

const json = (status, body) => new Response (JSON.stringify (body), { status, headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' } });
const failure = (status, error, extra = {}) => json (status, { error, message: MESSAGES[error], ...extra });
const html = (status, body) => new Response (body, { status, headers: { 'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store' } });

function daysBefore (date, days)
{
    const d = new Date (date + 'T00:00:00Z');
    d.setUTCDate (d.getUTCDate() - days);
    return d.toISOString().slice (0, 10);
}

/** Constant-time comparison, so the admin token cannot be guessed a byte at a time. */
function sameText (a, b)
{
    const x = new TextEncoder().encode (a), y = new TextEncoder().encode (b);
    let diff = x.length ^ y.length;

    for (let i = 0; i < Math.max (x.length, y.length); ++i)
        diff |= (x[i] ?? 0) ^ (y[i] ?? 0);

    return diff === 0;
}

async function readBody (request)
{
    const text = await request.text();

    if (text.length > MAX_BODY)
        return null;

    if ((request.headers.get ('content-type') ?? '').includes ('application/x-www-form-urlencoded'))
        return Object.fromEntries (new URLSearchParams (text));

    try
    {
        const body = JSON.parse (text);
        return body && typeof body === 'object' && ! Array.isArray (body) ? body : null;
    }
    catch
    {
        return null;
    }
}

/** The app, with everything it depends on passed in so the tests can run it. */
export function createApp ({ store, signingKey, adminToken = null, paddle = null, mailer = null,
                             today = () => new Date().toISOString().slice (0, 10),
                             now = () => Date.now() / 1000,
                             randomBytes = (n) => crypto.getRandomValues (new Uint8Array (n)) })
{
    /** A new licence with a number no other licence has. */
    async function issue (fields)
    {
        for (;;)
        {
            const key = await makeKey (randomBytes (15));

            if (await store.createLicence ({ key, created: today(), ...fields }))
                return key;
        }
    }

    /** Paddle's webhook: a sale issues and emails licence numbers, a refund or chargeback withdraws them. */
    async function paddleEvent (request)
    {
        const raw = await request.text();

        if (! paddle?.webhookSecret || ! await verifySignature (request.headers.get ('paddle-signature'), raw, paddle.webhookSecret, now()))
            return json (401, { error: 'bad-signature' });

        const event = JSON.parse (raw);
        const data = event.data ?? {};

        if (event.event_type === 'transaction.completed')
        {
            const copies = copiesIn (data, paddle.productId);

            if (copies === 0 || ! data.id)
                return json (200, { ok: true, licences: 0 });

            // Paddle retries until it gets a 200, so each step can run again safely.
            let licences = await store.listByOrder (data.id);

            if (licences.length < copies)
            {
                const customer = paddle.apiKey && data.customer_id
                    ? await fetchCustomer (data.customer_id, paddle)
                    : { name: '', email: data.custom_data?.email ?? '' };

                for (let i = licences.length; i < copies; ++i)
                    await issue ({ name: cleanName (customer.name), email: customer.email, orderRef: data.id });

                licences = await store.listByOrder (data.id);
            }

            const unsent = licences.filter ((l) => ! l.emailed);

            if (mailer && unsent.length > 0 && licences[0].email)
            {
                await mailer ({ to: licences[0].email, ...licenceEmail ({ name: licences[0].name, keys: licences.map ((l) => l.key) }) });

                for (const l of licences)
                    await store.markEmailed (l.key, today());
            }

            return json (200, { ok: true, licences: licences.length });
        }

        const adjustment = event.event_type === 'adjustment.created' || event.event_type === 'adjustment.updated';

        if (adjustment && data.status === 'approved' && (data.action === 'refund' || data.action === 'chargeback') && data.transaction_id)
        {
            const licences = await store.listByOrder (data.transaction_id);

            for (const l of licences)
                await store.updateLicence (l.key, { revoked: true, note: `${data.action} ${data.id ?? ''}`.trim() });

            return json (200, { ok: true, revoked: licences.length });
        }

        // The bank found for us after all.
        if (adjustment && data.status === 'approved' && data.action === 'chargeback_reverse' && data.transaction_id)
        {
            const licences = await store.listByOrder (data.transaction_id);

            for (const l of licences)
                await store.updateLicence (l.key, { revoked: false, note: `chargeback reversed ${data.id ?? ''}`.trim() });

            return json (200, { ok: true, restored: licences.length });
        }

        return json (200, { ok: true });
    }

    /** Activates a computer: { code, name } or { status, error }. */
    async function activate (typedLicence, typedMachine)
    {
        const key = await parseKey (typedLicence);
        const machine = parseMachineCode (typedMachine);

        if (! key)
            return { status: 400, error: 'invalid-licence' };

        if (! machine)
            return { status: 400, error: 'invalid-machine' };

        const licence = await store.getLicence (key);

        if (! licence)
            return { status: 404, error: 'unknown-licence' };

        if (licence.revoked)
            return { status: 403, error: 'revoked' };

        const issued = today();

        if (await store.activate (key, machine, issued, COMPUTERS_PER_LICENCE) === 'full')
            return { status: 409, error: 'computer-limit' };

        const name = cleanName (licence.name);
        return { code: await activationCode ({ key, machine, name, issued }, await signingKey), name };
    }

    async function deactivate (body)
    {
        const key = await parseKey (body.licence);
        const machine = parseMachineCode (body.machine);

        if (! key)
            return failure (400, 'invalid-licence');

        if (! machine)
            return failure (400, 'invalid-machine');

        if (! await store.getLicence (key))
            return failure (404, 'unknown-licence');

        const places = await store.listActivations (key);

        if (! places.some ((p) => p.machine === machine))
            return json (200, { ok: true });

        if (await store.countMoves (key, daysBefore (today(), 365)) >= MOVES_PER_YEAR)
            return failure (429, 'too-many-moves');

        await store.deactivate (key, machine, today());
        return json (200, { ok: true });
    }

    async function admin (request, path)
    {
        const auth = request.headers.get ('authorization') ?? '';

        if (! adminToken || ! sameText (auth, 'Bearer ' + adminToken))
            return failure (404, 'not-found');

        if (path === '/v1/admin/licences' && request.method === 'POST')
        {
            const body = await readBody (request);

            if (! body || typeof body.email !== 'string' || ! body.email.includes ('@'))
                return failure (400, 'bad-request');

            const key = await issue ({ name: cleanName (body.name), email: body.email.trim(), orderRef: body.orderRef ?? null, note: body.note ?? null });
            return json (201, { licence: key });
        }

        const match = path.match (/^\/v1\/admin\/licences\/([^/]+)(\/reset)?$/);
        const key = match && await parseKey (decodeURIComponent (match[1]));

        if (! key || ! await store.getLicence (key))
            return failure (404, 'not-found');

        if (match[2])
        {
            if (request.method !== 'POST')
                return failure (404, 'not-found');

            await store.resetActivations (key);
        }
        else if (request.method === 'POST')
        {
            const body = await readBody (request);

            if (! body)
                return failure (400, 'bad-request');

            await store.updateLicence (key, {
                name: body.name === undefined ? undefined : cleanName (body.name),
                email: body.email, revoked: body.revoked, note: body.note,
            });
        }
        else if (request.method !== 'GET')
            return failure (404, 'not-found');

        return json (200, {
            licence: await store.getLicence (key),
            computers: await store.listActivations (key),
            movesThisYear: await store.countMoves (key, daysBefore (today(), 365)),
        });
    }

    return async function handle (request)
    {
        const url = new URL (request.url);
        const path = url.pathname.replace (/\/+$/, '') || '/';

        try
        {
            if (path === '/v1/activate' && request.method === 'POST')
            {
                const body = await readBody (request);

                if (! body)
                    return failure (400, 'bad-request');

                const result = await activate (body.licence, body.machine);
                return result.error ? failure (result.status, result.error) : json (200, result);
            }

            if (path === '/v1/deactivate' && request.method === 'POST')
            {
                const body = await readBody (request);
                return body ? await deactivate (body) : failure (400, 'bad-request');
            }

            if (path.startsWith ('/v1/admin/'))
                return await admin (request, path);

            if (path === '/v1/paddle' && request.method === 'POST')
                return await paddleEvent (request);

            if (path === '/thanks' && request.method === 'GET')
            {
                const transaction = url.searchParams.get ('transaction') ?? '';
                const licences = /^txn_[a-z0-9]{10,40}$/.test (transaction) ? await store.listByOrder (transaction) : [];
                return html (200, thanksPage ({ keys: licences.filter ((l) => ! l.revoked).map ((l) => l.key), transaction }));
            }

            if ((path === '/' || path === '/activate') && request.method === 'GET')
                return html (200, activationPage());

            if (path === '/activate' && request.method === 'POST')
            {
                const body = await readBody (request) ?? {};
                const result = await activate (body.licence, body.machine);
                const fields = { licence: String (body.licence ?? ''), machine: String (body.machine ?? '') };

                return result.error ? html (result.status, activationPage ({ ...fields, error: MESSAGES[result.error] }))
                                    : html (200, activationPage ({ ...fields, code: result.code, name: result.name }));
            }

            return failure (404, 'not-found');
        }
        catch (e)
        {
            console.error (e);
            return json (500, { error: 'server-error', message: 'Something went wrong on our side. Try again in a minute.' });
        }
    };
}

let cached = null;

export default {
    async fetch (request, env)
    {
        if (! cached || cached.secret !== env.SIGNING_KEY)
        {
            cached = {
                secret: env.SIGNING_KEY,
                app: createApp ({
                    store: d1Store (env.DB),
                    signingKey: importSigningKey (env.SIGNING_KEY),
                    adminToken: env.ADMIN_TOKEN || null,
                    paddle: env.PADDLE_WEBHOOK_SECRET ? {
                        webhookSecret: env.PADDLE_WEBHOOK_SECRET,
                        apiKey: env.PADDLE_API_KEY || null,
                        apiBase: env.PADDLE_API_BASE || 'https://api.paddle.com',
                        productId: env.PADDLE_PRODUCT_ID || null,
                    } : null,
                    mailer: env.RESEND_API_KEY && env.MAIL_FROM ? resendMailer ({ apiKey: env.RESEND_API_KEY, from: env.MAIL_FROM }) : null,
                }),
            };
        }

        return cached.app (request);
    },
};
