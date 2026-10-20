// The whole licence life cycle, run against each store.

import assert from 'node:assert/strict';

import { MOVES_PER_YEAR, createApp } from '../src/index.js';
import { readActivationCode } from '../src/licence.js';
import { testKeys } from './keys.js';

const machines = ['AAAAA-AAAAA-AAAAA-AAAA1', 'AAAAA-AAAAA-AAAAA-AAAA2', 'AAAAA-AAAAA-AAAAA-AAAA3', 'AAAAA-AAAAA-AAAAA-AAAA4'];

export async function lifeCycle (store)
{
    const { privateKey, publicKey } = await testKeys();
    let day = '2026-10-20';
    const app = createApp ({ store, signingKey: privateKey, adminToken: 'secret-token', today: () => day });

    const call = async (method, path, body, headers = {}) =>
    {
        const response = await app (new Request ('https://licence.test' + path, {
            method, headers: { 'content-type': 'application/json', ...headers }, body: body === undefined ? undefined : JSON.stringify (body),
        }));
        const type = response.headers.get ('content-type');
        return { status: response.status, body: type.includes ('json') ? await response.json() : await response.text() };
    };
    const admin = (method, path, body) => call (method, path, body, { authorization: 'Bearer secret-token' });

    // Admin routes hide without the token.
    assert.equal ((await call ('POST', '/v1/admin/licences', { name: 'Ada', email: 'ada@example.com' })).status, 404);
    assert.equal ((await call ('POST', '/v1/admin/licences', { email: 'ada@example.com' }, { authorization: 'Bearer wrong' })).status, 404);
    assert.equal ((await admin ('POST', '/v1/admin/licences', { name: 'Ada' })).status, 400);

    const created = await admin ('POST', '/v1/admin/licences', { name: 'Ada\nLovelace', email: 'ada@example.com', orderRef: 'ord_1' });
    assert.equal (created.status, 201);
    const key = created.body.licence;

    // Activation, typed sloppily.
    const first = await call ('POST', '/v1/activate', { licence: key.toLowerCase().replace (/-/g, ' '), machine: machines[0].toLowerCase() });
    assert.equal (first.status, 200);
    assert.equal (first.body.name, 'Ada Lovelace');
    assert.deepEqual (await readActivationCode (first.body.code, publicKey), { key, machine: machines[0], name: 'Ada Lovelace', issued: day });

    // The same computer again takes no new place.
    assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: machines[0] })).status, 200);
    assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: machines[1] })).status, 200);
    assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: machines[2] })).status, 200);

    const full = await call ('POST', '/v1/activate', { licence: key, machine: machines[3] });
    assert.equal (full.status, 409);
    assert.equal (full.body.error, 'computer-limit');
    assert.match (full.body.message, /3 computers/);

    // Moving a computer frees its place.
    assert.equal ((await call ('POST', '/v1/deactivate', { licence: key, machine: machines[2] })).status, 200);
    assert.equal ((await call ('POST', '/v1/deactivate', { licence: key, machine: machines[2] })).status, 200); // already gone
    assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: machines[3] })).status, 200);

    // But only so often in a year.
    for (let i = 1; i < MOVES_PER_YEAR; ++i)
    {
        assert.equal ((await call ('POST', '/v1/deactivate', { licence: key, machine: machines[3] })).status, 200);
        assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: machines[3] })).status, 200);
    }

    const tooMany = await call ('POST', '/v1/deactivate', { licence: key, machine: machines[3] });
    assert.equal (tooMany.status, 429);
    assert.equal (tooMany.body.error, 'too-many-moves');

    day = '2027-10-21';
    assert.equal ((await call ('POST', '/v1/deactivate', { licence: key, machine: machines[3] })).status, 200);

    // Support can see and reset a licence, and withdraw it after a refund.
    const record = await admin ('GET', '/v1/admin/licences/' + key);
    assert.equal (record.status, 200);
    assert.deepEqual (record.body.computers.map ((c) => c.machine), machines.slice (0, 2));
    assert.equal (record.body.licence.orderRef, 'ord_1');

    assert.equal ((await admin ('POST', '/v1/admin/licences/' + key + '/reset')).body.computers.length, 0);
    assert.equal ((await admin ('POST', '/v1/admin/licences/' + key, { revoked: true })).body.licence.revoked, true);

    const withdrawn = await call ('POST', '/v1/activate', { licence: key, machine: machines[0] });
    assert.equal (withdrawn.status, 403);
    assert.equal (withdrawn.body.error, 'revoked');

    // Transferred to a new owner.
    const moved = await admin ('POST', '/v1/admin/licences/' + key, { revoked: false, name: 'Grace Hopper', email: 'grace@example.com' });
    assert.equal (moved.body.licence.name, 'Grace Hopper');
    assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: machines[0] })).body.name, 'Grace Hopper');

    // Mistakes.
    assert.equal ((await call ('POST', '/v1/activate', { licence: 'nonsense', machine: machines[0] })).body.error, 'invalid-licence');
    assert.equal ((await call ('POST', '/v1/activate', { licence: key, machine: 'nonsense' })).body.error, 'invalid-machine');
    assert.equal ((await call ('POST', '/v1/activate', { licence: '00000-00000-00000-00000-0004C', machine: machines[0] })).status, 404);
    assert.equal ((await call ('POST', '/v1/activate', '[1]')).status, 400);
    assert.equal ((await call ('POST', '/v1/activate', { licence: 'x'.repeat (5000) })).status, 400);
    assert.equal ((await call ('GET', '/v1/activate')).status, 404);
    assert.equal ((await admin ('GET', '/v1/admin/licences/00000-00000-00000-00000-0004C')).status, 404);

    // The offline page.
    const page = await call ('GET', '/activate');
    assert.equal (page.status, 200);
    assert.match (page.body, /Activate Drenched without internet/);

    const form = await app (new Request ('https://licence.test/activate', {
        method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams ({ licence: key, machine: machines[1] }),
    }));
    const formPage = await form.text();
    assert.equal (form.status, 200);
    const code = formPage.match (/<textarea id="code" readonly>([^<]+)<\/textarea>/)[1];
    assert.equal ((await readActivationCode (code, publicKey)).machine, machines[1]);

    const badForm = await app (new Request ('https://licence.test/activate', {
        method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams ({ licence: '<script>', machine: machines[1] }),
    }));
    const badPage = await badForm.text();
    assert.equal (badForm.status, 400);
    assert.match (badPage, /not a Drenched licence number/);
    assert.ok (! badPage.includes ('<script>'));
}
