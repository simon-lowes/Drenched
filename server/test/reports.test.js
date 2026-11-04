import assert from 'node:assert/strict';
import { test } from 'node:test';

import { LIMITS, readReport, reportEmail, reportHandler, reportLimiter, resendReportMailer } from '../src/reports.js';
import { relayServer } from '../reports/relay.mjs';
import { createApp } from '../src/index.js';
import { memoryStore } from '../src/stores.js';
import { testKeys } from './keys.js';

const DETAILS = [
    'Drenched 0.1.0 beta, build 312 (abc1234), 64-bit',
    'Format: VST3',
    'Host: Reaper',
    'System: Mac OSX 15.1, Apple M2, 16.0 GB',
    'Audio: 48000 Hz, buffer 128, latency 0 samples',
    'Channels: Stereo in, Stereo out',
    'Space: stpauls-ortf',
    'Look: aperture, view studio',
].join ('\n');

const post = (body, headers = {}) => new Request ('https://x/v1/report', {
    method: 'POST', headers: { 'content-type': 'application/json', ...headers },
    body: typeof body === 'string' ? body : JSON.stringify (body),
});

function collect ()
{
    const sent = [];
    return { sent, send: async (email) => { sent.push (email); } };
}

test ('a bug report becomes an email with the setup, what the tester did and a reply address', async () =>
{
    const { sent, send } = collect();
    const handle = reportHandler ({ send });
    const answer = await handle (post ({ kind: 'bug', description: 'Clicked Freeze\r\nand the tail\u0007 stopped', details: DETAILS,
                                         replyTo: 'tester@example.org', steps: '18:02:11 loaded space stpauls-ortf' }));

    assert.equal (answer.status, 200);
    assert.deepEqual (await answer.json(), { ok: true });
    assert.equal (sent.length, 1);
    assert.equal (sent[0].replyTo, 'tester@example.org');
    assert.match (sent[0].subject, /^Drenched bug: Clicked Freeze and the tail stopped \(build 312, Reaper, Mac OSX 15\.1\)$/);
    assert.match (sent[0].text, /Clicked Freeze\nand the tail stopped/);
    assert.match (sent[0].text, /Format: VST3/);
    assert.match (sent[0].text, /What Drenched last did\n-+\n18:02:11 loaded space/);
    assert.doesNotMatch (sent[0].text, /\r|\u0007/);
});

test ('a crash report needs the crash, not a description', async () =>
{
    assert.equal (readReport ({ kind: 'crash', details: DETAILS, description: 'it just closed' }), null);

    const report = readReport ({ kind: 'crash', details: DETAILS, crash: 'Signal 11 (SIGSEGV)\n#0 Drenched+0x1234' });
    assert.equal (report.kind, 'crash');
    const email = reportEmail (report);
    assert.match (email.subject, /^Drenched crash: Crash \(build 312/);
    assert.match (email.text, /Crash\n-----\nSignal 11/);
    assert.match (email.text, /\(nothing written\)/);
});

test ('reports without the plugin\'s setup lines, or not JSON objects, are refused', async () =>
{
    const { sent, send } = collect();
    const handle = reportHandler ({ send });

    for (const body of ['not json', '[]', 'null', JSON.stringify ({ description: 'hi', details: 'hello' }),
                        JSON.stringify ({ description: '   ', details: DETAILS })])
        assert.equal ((await handle (post (body))).status, 400, body);

    assert.equal (sent.length, 0);
});

test ('a bug report stays a bug report, so a crash field there is dropped', () =>
{
    const report = readReport ({ kind: 'bug', description: 'x', details: DETAILS, crash: 'y' });
    assert.equal (report.crash, '');
    assert.equal (readReport ({ kind: 'nonsense', description: 'x', details: DETAILS }).kind, 'bug');
});

test ('only a plain reply address is kept, so no header or extra recipient can be added', () =>
{
    for (const bad of ['a@b.com\nBcc: c@d.com', 'a@b.com, c@d.com', 'A <a@b.com>', 'nobody', 'a@b', 42, 'x'.repeat (250) + '@b.com'])
        assert.equal (readReport ({ description: 'x', details: DETAILS, replyTo: bad }).replyTo, '', String (bad));

    assert.equal (readReport ({ description: 'x', details: DETAILS, replyTo: ' a.b+c@d-e.co.uk ' }).replyTo, 'a.b+c@d-e.co.uk');
});

test ('long fields are cut to their limits and huge bodies are refused', async () =>
{
    const report = readReport ({ kind: 'crash', description: 'd'.repeat (20000), details: DETAILS + 'x'.repeat (20000), crash: 'c'.repeat (90000) });
    assert.equal (report.description.length, LIMITS.description);
    assert.equal (report.details.length, LIMITS.details);
    assert.equal (report.crash.length, LIMITS.crash);

    const { sent, send } = collect();
    const handle = reportHandler ({ send });
    assert.equal ((await handle (post ({ description: 'x', details: DETAILS, pad: 'p'.repeat (LIMITS.body) }))).status, 413);
    assert.equal (sent.length, 0);
});

test ('one address can send a few reports, then waits; and there is a daily cap for everyone', () =>
{
    let t = 0;
    const allow = reportLimiter ({ perAddress: 2, perAddressWindowMs: 1000, perDay: 3, now: () => t });

    assert.ok (allow ('a'));
    assert.ok (allow ('a'));
    assert.ok (! allow ('a'));
    assert.ok (allow ('b'));
    assert.ok (! allow ('c'), 'daily cap');
    t = 2000;
    assert.ok (! allow ('a'), 'still the same day');
    t = 24 * 60 * 60 * 1000;
    assert.ok (allow ('a'));
});

test ('without an address configured the endpoint says so, and other methods are refused', async () =>
{
    const handle = reportHandler ({ send: null });
    assert.equal ((await handle (post ({ description: 'x', details: DETAILS }))).status, 503);
    assert.equal ((await reportHandler ({ send: async () => {} }) (new Request ('https://x/v1/report'))).status, 405);
});

test ('a failed send is reported as not sent, without the error text or the address', async () =>
{
    const lines = [];
    const handle = reportHandler ({ send: async () => { throw new Error ('boom'); }, log: (l) => lines.push (l) });
    const answer = await handle (post ({ description: 'x', details: DETAILS }));

    assert.equal (answer.status, 502);
    assert.deepEqual (await answer.json(), { error: 'not-sent' });
    assert.deepEqual (lines, ['report not sent: boom']);
});

test ('the Resend mailer sends to the configured address with the tester as Reply-To', async () =>
{
    const calls = [];
    const fetch = async (url, init) => { calls.push ({ url, init }); return new Response ('{}', { status: 200 }); };
    const send = resendReportMailer ({ apiKey: 'key', from: 'Reports <r@example.com>', to: 'owner@example.com', fetch });

    await send ({ subject: 's', text: 't', replyTo: 'tester@example.org' });
    await send ({ subject: 's', text: 't', replyTo: '' });

    assert.equal (calls[0].url, 'https://api.resend.com/emails');
    assert.equal (calls[0].init.headers.authorization, 'Bearer key');
    assert.deepEqual (JSON.parse (calls[0].init.body), { from: 'Reports <r@example.com>', to: ['owner@example.com'], subject: 's', text: 't', reply_to: 'tester@example.org' });
    assert.equal (JSON.parse (calls[1].init.body).reply_to, undefined);

    const failing = resendReportMailer ({ apiKey: 'k', from: 'f', to: 'owner@example.com', fetch: async () => new Response ('no', { status: 403 }) });
    await assert.rejects (failing ({ subject: 's', text: 't' }), (e) => ! e.message.includes ('owner@example.com'));
});

test ('the standalone relay answers /v1/report and /health over HTTP', async () =>
{
    const { sent, send } = collect();
    const server = relayServer (reportHandler ({ send }));
    await new Promise ((resolve) => server.listen (0, '127.0.0.1', resolve));
    const base = `http://127.0.0.1:${server.address().port}`;

    try
    {
        const health = await fetch (base + '/health');
        assert.equal (health.status, 200);
        const report = await fetch (base + '/v1/report', { method: 'POST', body: JSON.stringify ({ description: 'x', details: DETAILS }) });
        assert.equal (report.status, 200);
        assert.equal (sent.length, 1);
        assert.equal ((await fetch (base + '/v1/other')).status, 404);
        const huge = await fetch (base + '/v1/report', { method: 'POST', body: 'x'.repeat (LIMITS.body * 3) });
        assert.equal (huge.status, 413);
        assert.equal (sent.length, 1);

        // Not configured: the report is understood, then refused as unsendable.
        assert.equal ((await reportHandler ({ send: null }) (post ({ description: 'x', details: DETAILS }))).status, 503);
    }
    finally
    {
        server.closeAllConnections();
        server.close();
    }
});

test ('the activation server answers /v1/report too, keyed by the caller\'s Cloudflare address', async () =>
{
    const { privateKey, publicKey } = await testKeys();
    const { sent, send } = collect();
    const seen = [];
    const reports = reportHandler ({ send, allow: (address) => { seen.push (address); return true; } });
    const app = createApp ({ store: memoryStore(), signingKey: privateKey, verifyKey: publicKey, reports });

    assert.equal ((await app (post ({ description: 'x', details: DETAILS }, { 'cf-connecting-ip': '203.0.113.9' }))).status, 200);
    assert.deepEqual (seen, ['203.0.113.9']);
    assert.equal (sent.length, 1);

    const bare = createApp ({ store: memoryStore(), signingKey: privateKey, verifyKey: publicKey });
    assert.equal ((await bare (post ({ description: 'x', details: DETAILS }))).status, 503);
});
