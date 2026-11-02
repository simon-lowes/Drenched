import assert from 'node:assert/strict';
import { test } from 'node:test';

import { createApp } from '../src/index.js';
import { licenceEmail, resendMailer } from '../src/mail.js';
import { copiesIn, signBody, verifySignature } from '../src/paddle.js';
import { d1Store, memoryStore } from '../src/stores.js';
import { fakeD1 } from './fake-d1.js';
import { testKeys } from './keys.js';

const SECRET = 'pdl_ntfset_test_secret';
const NOW = 1_792_000_000;

function paddleFetch (customers)
{
    const calls = [];
    const fetch = async (url, init) =>
    {
        calls.push ({ url, init });
        const id = decodeURIComponent (url.split ('/').pop());
        return customers[id] ? new Response (JSON.stringify ({ data: { id, ...customers[id] } }), { status: 200 })
                             : new Response ('{}', { status: 404 });
    };
    return { fetch, calls };
}

// Every webhook case runs against both stores: memory, and the D1 SQL that
// production uses, on SQLite with the real schema.
const stores = [['memory', () => memoryStore()], ['D1', () => d1Store (fakeD1())]];

/** Registers a test once per store; its body gets a setup() that uses that store. */
function storeTest (name, body)
{
    for (const [kind, makeStore] of stores)
        test (`${name} (${kind} store)`, () => body ((options = {}) => setup ({ ...options, store: makeStore() })));
}

async function setup ({ store, failMail = 0, failLookup = 0, productId = 'pro_drenched', apiKey = 'pdl_key' })
{
    const mail = [];
    let failures = failMail, lookupFailures = failLookup;
    const customers = paddleFetch ({ ctm_ada: { name: 'Ada Lovelace', email: 'ada@example.com' } });

    // Paddle's API down for the first failLookup lookups: an error status, then no answer at all.
    const fetch = async (url, init) =>
    {
        if (lookupFailures-- > 0)
            return lookupFailures % 2 ? new Response ('{}', { status: 503 }) : Promise.reject (new TypeError ('fetch failed'));

        return customers.fetch (url, init);
    };

    const paddle = { webhookSecret: SECRET, apiKey, apiBase: 'https://sandbox-api.paddle.com', productId, fetch, calls: customers.calls };
    const app = createApp ({
        store, signingKey: (await testKeys()).privateKey, adminToken: 'secret-token', paddle, now: () => NOW,
        mailer: async (m) => { if (failures-- > 0) throw new Error ('mail down'); mail.push (m); },
    });

    const send = async (event, { signature } = {}) =>
    {
        const body = JSON.stringify (event);
        const response = await app (new Request ('https://licence.test/v1/paddle', {
            method: 'POST', body, headers: { 'content-type': 'application/json', 'paddle-signature': signature ?? await signBody (body, SECRET, NOW) },
        }));
        return { status: response.status, body: await response.json() };
    };

    // A body sent as it is, signed as Paddle would sign it.
    const sendRaw = async (body) =>
    {
        const response = await app (new Request ('https://licence.test/v1/paddle', {
            method: 'POST', body, headers: { 'content-type': 'application/json', 'paddle-signature': await signBody (body, SECRET, NOW) },
        }));
        return { status: response.status, body: await response.json() };
    };

    const thanks = async (transaction) => (await app (new Request ('https://licence.test/thanks?transaction=' + transaction))).text();

    return { app, store, mail, send, sendRaw, thanks, paddle };
}

const sale = (id, items) => ({ event_type: 'transaction.completed', data: { id, status: 'completed', customer_id: 'ctm_ada', items } });
const drenched = (quantity = 1) => ({ price: { id: 'pri_1', product_id: 'pro_drenched' }, quantity });

test ('Paddle signatures are checked, including age and rotation', async () =>
{
    const body = '{"a":1}';
    const header = await signBody (body, SECRET, NOW);

    assert.ok (await verifySignature (header, body, SECRET, NOW));
    assert.ok (await verifySignature (header, body, SECRET, NOW + 299));
    assert.ok (! await verifySignature (header, body, SECRET, NOW + 301));
    assert.ok (! await verifySignature (header, body + ' ', SECRET, NOW));
    assert.ok (! await verifySignature (header, body, 'other', NOW));
    assert.ok (! await verifySignature ('', body, SECRET, NOW));
    assert.ok (! await verifySignature ('ts=abc;h1=00', body, SECRET, NOW));

    const rotated = header.replace ('h1=', 'h1=deadbeef;h1=');
    assert.ok (await verifySignature (rotated, body, SECRET, NOW));
});

storeTest ('sales are refused until the product id is set, so other products never issue licences', async (setup) =>
{
    const { send, store, mail } = await setup ({ productId: null });
    const result = await send (sale ('txn_1', [drenched (2), { price: { product_id: 'pro_other' }, quantity: 5 }]));

    assert.equal (result.status, 500);
    assert.equal (result.body.error, 'no-product-id');
    assert.equal ((await store.listByOrder ('txn_1')).length, 0);
    assert.equal (mail.length, 0);
});

test ('copies count only Drenched items, capped', () =>
{
    assert.equal (copiesIn ({ items: [drenched (2), { price: { product_id: 'pro_other' }, quantity: 5 }] }, 'pro_drenched'), 2);
    assert.equal (copiesIn ({ items: [drenched (2), { price: { product_id: 'pro_other' }, quantity: 5 }] }, null), 0);
    assert.equal (copiesIn ({ items: [drenched (500)] }, 'pro_drenched'), 20);
    assert.equal (copiesIn ({}, 'pro_drenched'), 0);
});

storeTest ('a sale issues and emails a licence number once, however often Paddle retries', async (setup) =>
{
    const { app, store, mail, send, paddle } = await setup();

    const first = await send (sale ('txn_01h04vsc0qhwtsbsxh3422wjs4', [drenched()]));
    assert.deepEqual (first, { status: 200, body: { ok: true, licences: 1 } });

    const [licence] = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422wjs4');
    assert.equal (licence.name, 'Ada Lovelace');
    assert.equal (licence.email, 'ada@example.com');
    assert.equal (paddle.calls[0].url, 'https://sandbox-api.paddle.com/customers/ctm_ada');
    assert.equal (paddle.calls[0].init.headers.authorization, 'Bearer pdl_key');

    assert.equal (mail.length, 1);
    assert.equal (mail[0].to, 'ada@example.com');
    assert.equal (mail[0].subject, 'Your Drenched licence number');
    assert.ok (mail[0].text.includes (licence.key));

    await send (sale ('txn_01h04vsc0qhwtsbsxh3422wjs4', [drenched()]));
    assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422wjs4')).length, 1);
    assert.equal (mail.length, 1);

    // The buyer's page after checkout.
    const thanks = await (await app (new Request ('https://licence.test/thanks?transaction=txn_01h04vsc0qhwtsbsxh3422wjs4'))).text();
    assert.ok (thanks.includes (licence.key));
    const waiting = await (await app (new Request ('https://licence.test/thanks?transaction=txn_01h04vsc0qhwtsbsxh3422zzzz'))).text();
    assert.match (waiting, /http-equiv="refresh"/);
    const nonsense = await (await app (new Request ('https://licence.test/thanks?transaction=%3Cscript%3E'))).text();
    assert.ok (! nonsense.includes ('<script>'));
});

storeTest ('the same sale delivered twice at once issues and emails its numbers once', async (setup) =>
{
    const { store, mail, send } = await setup();
    const statuses = await Promise.all ([1, 2, 3].map (() => send (sale ('txn_01h04vsc0qhwtsbsxh3422ffff', [drenched (2)]))));

    assert.deepEqual (statuses.map ((r) => r.status), [200, 200, 200]);
    const licences = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422ffff');
    assert.deepEqual (licences.map ((l) => l.copy), [0, 1]);
    assert.equal (mail.length, 1);
    assert.ok (licences.every ((l) => mail[0].text.includes (l.key) && l.emailed));
});

storeTest ('a signed body that is not a JSON object is answered 200, so Paddle stops sending it', async (setup) =>
{
    // Paddle retries every answer but 200, 4xx included, and each retry sends the same
    // signed bytes, so only a 200 ends it (developer.paddle.com, "Handle webhook delivery").
    const { store, sendRaw } = await setup();

    for (const body of ['{not json', '', 'null', '[]', '42', '"transaction.completed"'])
        assert.deepEqual (await sendRaw (body), { status: 200, body: { ok: false, error: 'bad-json' } }, JSON.stringify (body));

    // The hook still works afterwards.
    assert.deepEqual (await sendRaw (JSON.stringify (sale ('txn_01h04vsc0qhwtsbsxh3422gggg', [drenched()]))), { status: 200, body: { ok: true, licences: 1 } });
    assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422gggg')).length, 1);
});

storeTest ('a sale whose items are not a list issues nothing and is not retried', async (setup) =>
{
    const { store, send } = await setup();

    for (const items of [{}, 'pro_drenched', 3, null])
    {
        assert.deepEqual (await send (sale ('txn_01h04vsc0qhwtsbsxh3422hhhh', items)), { status: 200, body: { ok: true, licences: 0 } }, JSON.stringify (items));
        assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422hhhh')).length, 0);
    }
});

storeTest ('a failed customer lookup issues nothing, so Paddle\'s retry issues the numbers to the right buyer', async (setup) =>
{
    // Numbers issued without the lookup would carry no name or email, and the retry, finding
    // them made, would never look the buyer up or email them.
    const { store, mail, send, paddle } = await setup ({ failLookup: 2 });
    const event = sale ('txn_01h04vsc0qhwtsbsxh3422iiii', [drenched (2)]);

    for (const attempt of ['an error status', 'no answer'])
    {
        assert.equal ((await send (event)).status, 500, attempt);
        assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422iiii')).length, 0, attempt);
        assert.equal (mail.length, 0, attempt);
    }

    assert.deepEqual (await send (event), { status: 200, body: { ok: true, licences: 2 } });
    const licences = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422iiii');
    assert.deepEqual (licences.map ((l) => [l.copy, l.name, l.email]), [[0, 'Ada Lovelace', 'ada@example.com'], [1, 'Ada Lovelace', 'ada@example.com']]);
    assert.equal (paddle.calls.length, 1);
    assert.equal (mail.length, 1);
    assert.equal (mail[0].to, 'ada@example.com');
    assert.ok (licences.every ((l) => mail[0].text.includes (l.key)));
});

storeTest ('without a Paddle API key the buyer\'s email comes from the checkout\'s custom data', async (setup) =>
{
    const { store, mail, send, paddle, thanks } = await setup ({ apiKey: null });
    const event = sale ('txn_01h04vsc0qhwtsbsxh3422jjjj', [drenched()]);
    event.data.custom_data = { email: 'grace@example.com' };

    assert.deepEqual (await send (event), { status: 200, body: { ok: true, licences: 1 } });
    const [licence] = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422jjjj');
    assert.equal (licence.email, 'grace@example.com');
    assert.equal (licence.name, '');
    assert.equal (paddle.calls.length, 0);
    assert.equal (mail.length, 1);
    assert.equal (mail[0].to, 'grace@example.com');
    assert.match (mail[0].text, /^Hello,/);
    assert.ok (mail[0].text.includes (licence.key));

    // With no email either, the number is still made (the checkout page shows it) and the
    // sale is not retried, as there is nobody to email.
    assert.deepEqual (await send (sale ('txn_01h04vsc0qhwtsbsxh3422kkkk', [drenched()])), { status: 200, body: { ok: true, licences: 1 } });
    const [unmailed] = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422kkkk');
    assert.equal (unmailed.email, '');
    assert.equal (unmailed.emailed, null);
    assert.equal (mail.length, 1);
    assert.ok ((await thanks ('txn_01h04vsc0qhwtsbsxh3422kkkk')).includes (unmailed.key));
});

storeTest ('several copies make several numbers in one email', async (setup) =>
{
    const { store, mail, send } = await setup();
    await send (sale ('txn_01h04vsc0qhwtsbsxh3422aaaa', [drenched (3)]));

    const licences = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422aaaa');
    assert.equal (licences.length, 3);
    assert.equal (new Set (licences.map ((l) => l.key)).size, 3);
    assert.equal (mail.length, 1);
    assert.equal (mail[0].subject, 'Your Drenched licence numbers');
    assert.ok (licences.every ((l) => mail[0].text.includes (l.key)));
});

storeTest ('a failed email is retried with the same numbers', async (setup) =>
{
    const { store, mail, send } = await setup ({ failMail: 1 });

    const failed = await send (sale ('txn_01h04vsc0qhwtsbsxh3422bbbb', [drenched()]));
    assert.equal (failed.status, 500);
    const [licence] = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422bbbb');
    assert.equal (licence.emailed, null);

    assert.equal ((await send (sale ('txn_01h04vsc0qhwtsbsxh3422bbbb', [drenched()]))).status, 200);
    assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422bbbb')).length, 1);
    assert.equal (mail.length, 1);
    assert.ok (mail[0].text.includes (licence.key));
});

storeTest ('refunds and chargebacks withdraw the numbers; a reversed chargeback restores them', async (setup) =>
{
    const { app, store, send } = await setup();
    await send (sale ('txn_01h04vsc0qhwtsbsxh3422cccc', [drenched()]));
    const [licence] = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422cccc');

    const activate = async () => (await app (new Request ('https://licence.test/v1/activate', {
        method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify ({ licence: licence.key, machine: 'AAAAA-AAAAA-AAAAA-AAAA1' }),
    }))).status;

    assert.equal (await activate(), 200);

    // A refund waiting for approval changes nothing.
    await send ({ event_type: 'adjustment.created', data: { id: 'adj_1', action: 'refund', status: 'pending_approval', transaction_id: 'txn_01h04vsc0qhwtsbsxh3422cccc' } });
    assert.equal ((await store.getLicence (licence.key)).revoked, false);

    const refunded = await send ({ event_type: 'adjustment.updated', data: { id: 'adj_1', action: 'refund', status: 'approved', transaction_id: 'txn_01h04vsc0qhwtsbsxh3422cccc' } });
    assert.deepEqual (refunded.body, { ok: true, revoked: 1 });
    assert.equal ((await store.getLicence (licence.key)).revoked, true);
    assert.equal (await activate(), 403);

    await send ({ event_type: 'adjustment.updated', data: { id: 'adj_2', action: 'chargeback_reverse', status: 'approved', transaction_id: 'txn_01h04vsc0qhwtsbsxh3422cccc' } });
    assert.equal ((await store.getLicence (licence.key)).revoked, false);
});

const refund = (id, transaction, extra = {}) => ({ event_type: 'adjustment.updated', data: { id, action: 'refund', status: 'approved', transaction_id: transaction, ...extra } });

storeTest ('the checkout page stops showing numbers once they are withdrawn', async (setup) =>
{
    const { store, send, thanks } = await setup();
    await send (sale ('txn_01h04vsc0qhwtsbsxh3422llll', [drenched (2)]));
    const keys = (await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422llll')).map ((l) => l.key);

    const before = await thanks ('txn_01h04vsc0qhwtsbsxh3422llll');
    assert.ok (keys.every ((k) => before.includes (k)));

    assert.deepEqual ((await send (refund ('adj_3', 'txn_01h04vsc0qhwtsbsxh3422llll'))).body, { ok: true, revoked: 2 });
    const after = await thanks ('txn_01h04vsc0qhwtsbsxh3422llll');
    assert.ok (keys.every ((k) => ! after.includes (k)), 'a withdrawn number is shown');

    // It says so, rather than waiting for ever for numbers that will not come.
    assert.doesNotMatch (after, /http-equiv="refresh"/);
    assert.match (after, /withdrawn/);
});

storeTest ('a partial refund keeps every copy of the order and marks it for support', async (setup) =>
{
    // A partial adjustment names the transaction item and an amount, never a quantity
    // (developer.paddle.com, adjustment.created), and the amount can as well be a goodwill
    // discount as the price of some copies. So nothing is withdrawn; support withdraws the
    // copies refunded (POST /v1/admin/licences/{key} {revoked: true}).
    const { store, send } = await setup();
    await send (sale ('txn_01h04vsc0qhwtsbsxh3422mmmm', [drenched (3)]));

    const partial = refund ('adj_4', 'txn_01h04vsc0qhwtsbsxh3422mmmm', {
        type: 'partial', items: [{ id: 'adjitm_1', item_id: 'txnitm_1', type: 'partial', amount: '4900' }],
    });

    assert.deepEqual (await send (partial), { status: 200, body: { ok: true, revoked: 0, flagged: 3 } });
    const licences = await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422mmmm');
    assert.deepEqual (licences.map ((l) => l.revoked), [false, false, false]);
    assert.ok (licences.every ((l) => l.note === 'partial refund adj_4'));

    // Paddle delivering it again changes nothing more.
    assert.deepEqual (await send (partial), { status: 200, body: { ok: true, revoked: 0, flagged: 3 } });

    // A full refund of the same order afterwards withdraws them all.
    assert.deepEqual ((await send (refund ('adj_5', 'txn_01h04vsc0qhwtsbsxh3422mmmm'))).body, { ok: true, revoked: 3 });
});

storeTest ('a refund or chargeback for a transaction never seen is acknowledged and changes nothing', async (setup) =>
{
    const { store, send } = await setup();
    await send (sale ('txn_01h04vsc0qhwtsbsxh3422nnnn', [drenched()]));

    assert.deepEqual (await send (refund ('adj_5', 'txn_01h04vsc0qhwtsbsxh3422zzzz')), { status: 200, body: { ok: true, revoked: 0 } });
    assert.deepEqual (await send (refund ('adj_6', 'txn_01h04vsc0qhwtsbsxh3422zzzz', { action: 'chargeback' })), { status: 200, body: { ok: true, revoked: 0 } });
    assert.deepEqual (await send (refund ('adj_7', 'txn_01h04vsc0qhwtsbsxh3422zzzz', { action: 'chargeback_reverse' })), { status: 200, body: { ok: true, restored: 0 } });

    assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422zzzz')).length, 0);
    assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422nnnn'))[0].revoked, false, 'another order was withdrawn');
});

storeTest ('unsigned, wrongly signed or other events issue nothing', async (setup) =>
{
    const { store, send } = await setup();
    const event = sale ('txn_01h04vsc0qhwtsbsxh3422dddd', [drenched()]);

    assert.equal ((await send (event, { signature: 'ts=1;h1=00' })).status, 401);
    assert.equal ((await send (event, { signature: await signBody ('{}', SECRET, NOW) })).status, 401);
    assert.equal ((await send ({ ...event, event_type: 'transaction.created' })).status, 200);
    assert.equal ((await send (sale ('txn_01h04vsc0qhwtsbsxh3422eeee', [{ price: { product_id: 'pro_other' }, quantity: 1 }]))).body.licences, 0);
    assert.equal ((await store.listByOrder ('txn_01h04vsc0qhwtsbsxh3422dddd')).length, 0);
});

test ('the licence email reads well and the Resend call is right', async () =>
{
    const one = licenceEmail ({ name: 'Ada', keys: ['0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CS'] });
    assert.match (one.text, /^Hello Ada,/);
    assert.match (one.text, /three computers/);
    assert.ok (! licenceEmail ({ name: '<b>', keys: ['K'] }).html.includes ('<b>'));

    let sent = null;
    const send = resendMailer ({ apiKey: 're_key', from: 'Drenched <licences@example.com>', fetch: async (url, init) => { sent = { url, init }; return new Response ('{}', { status: 200 }); } });
    await send ({ to: 'ada@example.com', ...one });
    assert.equal (sent.url, 'https://api.resend.com/emails');
    assert.equal (sent.init.headers.authorization, 'Bearer re_key');
    assert.deepEqual (JSON.parse (sent.init.body).to, ['ada@example.com']);

    const failing = resendMailer ({ apiKey: 're_key', from: 'x', fetch: async () => new Response ('nope', { status: 422 }) });
    await assert.rejects (failing ({ to: 'a@b.c', ...one }), /422/);
});
