// The report relay on its own: a small Node server for a VPS, doing what the Worker's
// /v1/report route does (src/reports.js). Settings come only from the environment:
//
//   REPORT_TO        where reports are emailed (required; never in the repo)
//   REPORT_FROM      the sender, on a domain verified in Resend, e.g. "Drenched reports <reports@example.com>"
//   RESEND_API_KEY   a Resend key with sending access
//   PORT             default 8080
//
// It answers POST /v1/report and GET /health, and nothing else.

import { createServer } from 'node:http';
import { BlockList, isIPv6 } from 'node:net';

import { LIMITS, reportHandler, reportLimiter, resendReportMailer } from '../src/reports.js';

const env = process.env;
const configured = Boolean (env.REPORT_TO && env.REPORT_FROM && env.RESEND_API_KEY);
const log = (line) => console.log (`${new Date().toISOString()} ${line}`);

function handlerFromEnvironment ()
{
    return reportHandler ({
        send: configured ? resendReportMailer ({ apiKey: env.RESEND_API_KEY, from: env.REPORT_FROM, to: env.REPORT_TO }) : null,
        allow: reportLimiter(),
        log,
    });
}

// Cloudflare's addresses (https://www.cloudflare.com/ips-v4 and ips-v6, 2026-11-10). Only a
// request that reached us from one of these carries a cf-connecting-ip we can believe.
const cloudflare = new BlockList();

for (const range of ['173.245.48.0/20', '103.21.244.0/22', '103.22.200.0/22', '103.31.4.0/22', '141.101.64.0/18',
                     '108.162.192.0/18', '190.93.240.0/20', '188.114.96.0/20', '197.234.240.0/22', '198.41.128.0/17',
                     '162.158.0.0/15', '104.16.0.0/13', '104.24.0.0/14', '172.64.0.0/13', '131.0.72.0/22'])
    cloudflare.addSubnet (range.split ('/')[0], Number (range.split ('/')[1]), 'ipv4');

for (const range of ['2400:cb00::/32', '2606:4700::/32', '2803:f800::/32', '2405:b500::/32', '2405:8100::/32',
                     '2a06:98c0::/29', '2c0f:f248::/32'])
    cloudflare.addSubnet (range.split ('/')[0], Number (range.split ('/')[1]), 'ipv6');

function plainAddress (text)
{
    const address = String (text ?? '').trim().replace (/^::ffff:(?=\d+\.)/, '');
    return /^[0-9a-f:.]{2,45}$/i.test (address) ? address : '';
}

function isCloudflare (address)
{
    try
    {
        return address !== '' && cloudflare.check (address, isIPv6 (address) ? 'ipv6' : 'ipv4');
    }
    catch
    {
        return false;
    }
}

// The address a report is counted against. The VPS's proxy (Traefik) adds the address it was
// reached from as the last X-Forwarded-For entry; anything before that, and cf-connecting-ip,
// the sender can write, so cf-connecting-ip only counts when that last hop is Cloudflare.
// IPv6 is counted per /64, the block one connection usually has.
export function addressOf (request)
{
    const chain = String (request.headers['x-forwarded-for'] ?? '').split (',');
    const peer = plainAddress (chain.at (-1)) || plainAddress (request.socket?.remoteAddress);
    const sender = isCloudflare (peer) ? plainAddress (request.headers['cf-connecting-ip']) || peer : peer;

    if (sender === '')
        return 'unknown';

    return isIPv6 (sender) ? sender.toLowerCase().split (':').slice (0, 4).join (':') + '::/64' : sender;
}

const answerWith = (response, status, body) =>
{
    if (! response.headersSent && ! response.destroyed)
    {
        response.writeHead (status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
        response.end (body);
    }
};

export function relayServer (handle = handlerFromEnvironment())
{
    return createServer (async (request, response) =>
    {
        // A request that fails half way (the sender hangs up mid-upload, a method fetch's
        // Request won't take) gets an answer if it can; it must never end the relay.
        try
        {
            const path = (request.url ?? '/').split ('?')[0].replace (/\/+$/, '') || '/';

            if (path === '/health' && request.method === 'GET')
                return answerWith (response, 200, '{"ok":true}');

            if (path !== '/v1/report')
                return answerWith (response, 404, '{"error":"not-found"}');

            if (request.method !== 'POST')
                return answerWith (response, 405, '{"error":"method"}');

            // Stops reading at the limit: a huge upload is refused, not kept.
            const pieces = [];
            let size = 0;

            for await (const piece of request)
            {
                size += piece.length;

                if (size > LIMITS.body)
                {
                    response.setHeader ('connection', 'close');
                    answerWith (response, 413, '{"error":"too-large"}');
                    request.destroy();
                    return;
                }

                pieces.push (piece);
            }

            const answer = await handle (new Request ('http://relay' + path, {
                method: 'POST',
                headers: { 'content-length': String (size) },
                body: Buffer.concat (pieces),
            }), addressOf (request));

            const text = await answer.text();

            if (! response.headersSent && ! response.destroyed)
            {
                response.writeHead (answer.status, Object.fromEntries (answer.headers));
                response.end (text);
            }
        }
        catch (error)
        {
            log (`request failed: ${error?.code ?? error?.message ?? 'unknown'}`);
            answerWith (response, 400, '{"error":"bad-request"}');
        }
    });
}

if (import.meta.url === `file://${process.argv[1]}`)
{
    // A last guard: whatever goes wrong with one request, the relay keeps answering the rest.
    process.on ('unhandledRejection', (error) => log (`unhandled: ${error?.message ?? error}`));

    const port = Number (env.PORT ?? 8080);
    relayServer().listen (port, () => log (`report relay on port ${port}${configured ? '' : ' (not configured: reports answer 503)'}`));
}
