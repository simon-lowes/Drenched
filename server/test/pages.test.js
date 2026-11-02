// The HTML pages: their security headers, that their one script is the one the
// Content-Security-Policy allows, and how long /thanks shows licence numbers.

import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'node:test';

import { createApp, PAGE_POLICY } from '../src/index.js';
import { activationPage, copyScript, thanksPage } from '../src/pages.js';
import { memoryStore } from '../src/stores.js';
import { testKeys } from './keys.js';

const hashOf = (script) => `'sha256-${createHash ('sha256').update (script).digest ('base64')}'`;

test ('the policy allows the pages\' own script and nothing else runs on them', () =>
{
    assert.ok (PAGE_POLICY.includes (hashOf (copyScript)), 'copyScript changed: update its hash in PAGE_POLICY (index.js)');

    const pages = [activationPage(), activationPage ({ code: 'DRN1.abc', name: 'Ada' }), activationPage ({ error: 'x' }),
                   thanksPage(), thanksPage ({ keys: ['K'] }), thanksPage ({ withdrawn: true }), thanksPage ({ expired: true })];

    for (const page of pages)
    {
        assert.doesNotMatch (page, /\son[a-z]+\s*=/i, 'an inline handler would be blocked');

        for (const [, script] of page.matchAll (/<script>([\s\S]*?)<\/script>/g))
            assert.ok (PAGE_POLICY.includes (hashOf (script)), 'a page has a script the policy blocks');
    }
});

async function app (today)
{
    const store = memoryStore();
    return { store, app: createApp ({ store, signingKey: (await testKeys()).privateKey, today: () => today.value }) };
}

test ('every page is sent with headers that keep it unframed, unsniffed and its address private', async () =>
{
    const { app: run } = await app ({ value: '2026-11-02' });

    for (const [path, init] of [['/', {}], ['/activate', {}], ['/thanks?transaction=txn_01h04vsc0qhwtsbsxh3422wjs4', {}],
                                ['/activate', { method: 'POST', headers: { 'content-type': 'application/x-www-form-urlencoded' }, body: 'licence=x&machine=y' }]])
    {
        const response = await run (new Request ('https://licence.test' + path, init));
        assert.match (response.headers.get ('content-type'), /^text\/html/);
        assert.equal (response.headers.get ('content-security-policy'), PAGE_POLICY);
        assert.equal (response.headers.get ('x-frame-options'), 'DENY');
        assert.equal (response.headers.get ('referrer-policy'), 'no-referrer');
        assert.equal (response.headers.get ('x-content-type-options'), 'nosniff');
        assert.match (response.headers.get ('strict-transport-security'), /max-age=\d+/);
        assert.equal (response.headers.get ('cache-control'), 'no-store');
    }
});

test ('/thanks shows an order\'s numbers on the day of the sale and the next, then points to the email', async () =>
{
    const today = { value: '2026-11-02' };
    const { store, app: run } = await app (today);
    const order = 'txn_01h04vsc0qhwtsbsxh3422tttt';
    await store.createLicence ({ key: 'K1', name: 'Ada', email: 'a@b.c', created: '2026-11-02', orderRef: order, copy: 0 });
    await store.createLicence ({ key: 'K2', name: 'Ada', email: 'a@b.c', created: '2026-11-02', orderRef: order, copy: 1 });
    const thanks = async () => (await run (new Request ('https://licence.test/thanks?transaction=' + order))).text();

    assert.ok ((await thanks()).includes ('K1'));
    today.value = '2026-11-03';
    assert.ok ((await thanks()).includes ('K2'));

    today.value = '2026-11-04';
    const later = await thanks();
    assert.ok (! later.includes ('K1') && ! later.includes ('K2'), 'old numbers are still shown');
    assert.match (later, /email/);
    assert.doesNotMatch (later, /http-equiv="refresh"/, 'it waits for numbers that will not come');

    // Withdrawn numbers still say so, however old.
    await store.updateLicence ('K1', { revoked: true });
    await store.updateLicence ('K2', { revoked: true });
    assert.match (await thanks(), /withdrawn/);
});
