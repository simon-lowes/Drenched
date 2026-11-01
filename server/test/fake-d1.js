// A small stand-in for Cloudflare D1's API, on SQLite in memory, with the
// production schema (server/schema.sql) applied, so its constraints, such as
// one licence per (order_ref, copy), hold here exactly as in D1.

import { readFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';

export function fakeD1()
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
