import assert from 'node:assert/strict';
import { test } from 'node:test';

import { createApp } from '../src/index.js';
import { licenceEmail, resendMailer } from '../src/mail.js';
import { copiesIn, signBody, verifySignature } from '../src/paddle.js';
import { memoryStore } from '../src/stores.js';
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

async function setup ({ failMail = 0 } = {})
{
    const store = memoryStore();
    const mail = [];
    let failures = failMail;
    const paddle = { webhookSecret: SECRET, apiKey: 'pdl_key', apiBase: 'https://sandbox-api.paddle.com', productId: 'pro_drenched',
                     ...paddleFetch ({ ctm_ada: { name: 'Ada Lovelace', email: 'ada@example.com' } }) };
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

    return { app, store, mail, send, paddle };
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

test ('copies count only Drenched items, capped', () =>
{
    assert.equal (copiesIn ({ items: [drenched (2), { price: { product_id: 'pro_other' }, quantity: 5 }] }, 'pro_drenched'), 2);
    assert.equal (copiesIn ({ items: [drenched (2), { price: { product_id: 'pro_other' }, quantity: 5 }] }, null), 7);
    assert.equal (copiesIn ({ items: [drenched (500)] }, 'pro_drenched'), 20);
    assert.equal (copiesIn ({}, 'pro_drenched'), 0);
});

test ('a sale issues and emails a licence number once, however often Paddle retries', async () =>
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

test ('several copies make several numbers in one email', async () =>
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

test ('a failed email is retried with the same numbers', async () =>
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

test ('refunds and chargebacks withdraw the numbers; a reversed chargeback restores them', async () =>
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

test ('unsigned, wrongly signed or other events issue nothing', async () =>
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
