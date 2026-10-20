// A local activation server for development: the real app on Node, with an
// in-memory SQLite database and the published test key, at http://127.0.0.1:8787
// (the address development builds of the plugin use).
//
//   node scripts/dev.mjs            makes one licence and prints its number
//   node scripts/dev.mjs --port 9000

import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { DatabaseSync } from 'node:sqlite';

import { createApp } from '../src/index.js';
import { d1Store } from '../src/stores.js';
import { testKeys } from '../test/keys.js';

const port = Number (process.argv[process.argv.indexOf ('--port') + 1]) || 8787;

const db = new DatabaseSync (':memory:');
db.exec (readFileSync (new URL ('../schema.sql', import.meta.url), 'utf8'));

const d1 = {
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

const app = createApp ({ store: d1Store (d1), signingKey: (await testKeys()).privateKey, adminToken: 'dev' });

const created = await (await app (new Request ('http://dev/v1/admin/licences', {
    method: 'POST', headers: { authorization: 'Bearer dev', 'content-type': 'application/json' },
    body: JSON.stringify ({ name: 'Dev Tester', email: 'dev@example.com' }),
}))).json();

createServer (async (req, res) =>
{
    const chunks = [];

    for await (const chunk of req)
        chunks.push (chunk);

    const body = chunks.length ? Buffer.concat (chunks) : undefined;
    const response = await app (new Request (`http://127.0.0.1:${port}${req.url}`, { method: req.method, headers: req.headers, body }));

    res.writeHead (response.status, Object.fromEntries (response.headers));
    res.end (Buffer.from (await response.arrayBuffer()));
}).listen (port, '127.0.0.1', () =>
{
    console.log (`Activation server on http://127.0.0.1:${port} (admin token "dev")`);
    console.log (`Licence number for testing: ${created.licence}`);
});
