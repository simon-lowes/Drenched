// Runs the D1 store's SQL on SQLite through a small stand-in for D1's API.

import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import { test } from 'node:test';

import worker from '../src/index.js';
import { d1Store } from '../src/stores.js';
import { testKeys } from './keys.js';
import { lifeCycle } from './scenario.js';

function fakeD1()
{
    const db = new DatabaseSync (':memory:');
    db.exec (readFileSync (new URL ('../schema.sql', import.meta.url), 'utf8'));

    return {
        prepare (sql)
        {
            let args = [];
            const statement = {
                bind (...values) { args = values; return statement; },
                async first() { return db.prepare (sql).get (...args) ?? null; },
                async all() { return { results: db.prepare (sql).all (...args) }; },
                async run() { return { meta: { changes: Number (db.prepare (sql).run (...args).changes) } }; },
            };
            return statement;
        },
    };
}

test ('the licence life cycle, kept in D1', () => lifeCycle (d1Store (fakeD1())));

test ('two computers taking the last place at once get one place between them', async () =>
{
    const store = d1Store (fakeD1());
    await store.createLicence ({ key: 'K', name: 'Ada', email: 'a@b.c', created: '2026-10-20' });
    await store.activate ('K', 'M1', '2026-10-20', 2);

    const results = await Promise.all ([store.activate ('K', 'M2', '2026-10-20', 2), store.activate ('K', 'M3', '2026-10-20', 2)]);
    assert.deepEqual (results.sort(), ['added', 'full']);
    assert.equal ((await store.listActivations ('K')).length, 2);
});

test ('the Worker reads its key, token and database from the environment', async () =>
{
    const { jwk } = await testKeys();
    const env = { DB: fakeD1(), SIGNING_KEY: JSON.stringify (jwk), ADMIN_TOKEN: 'secret-token' };
    const post = (path, body, headers = {}) => worker.fetch (new Request ('https://licence.test' + path, {
        method: 'POST', headers: { 'content-type': 'application/json', ...headers }, body: JSON.stringify (body),
    }), env);

    const created = await post ('/v1/admin/licences', { name: 'Ada', email: 'ada@example.com' }, { authorization: 'Bearer secret-token' });
    const { licence } = await created.json();
    const activated = await post ('/v1/activate', { licence, machine: 'AAAAA-AAAAA-AAAAA-AAAA1' });
    assert.equal (activated.status, 200);
    assert.match ((await activated.json()).code, /^DRN1\./);
});
