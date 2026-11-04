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

// Behind Cloudflare and the VPS's proxy, the tester's address is in a header.
function addressOf (request)
{
    const forwarded = request.headers['cf-connecting-ip'] ?? String (request.headers['x-forwarded-for'] ?? '').split (',')[0];
    return String (forwarded || request.socket.remoteAddress || 'unknown').trim();
}

export function relayServer (handle = handlerFromEnvironment())
{
    return createServer (async (request, response) =>
    {
        const path = (request.url ?? '/').split ('?')[0].replace (/\/+$/, '') || '/';

        if (path === '/health' && request.method === 'GET')
        {
            response.writeHead (200, { 'content-type': 'application/json' });
            response.end ('{"ok":true}');
            return;
        }

        if (path !== '/v1/report')
        {
            response.writeHead (404, { 'content-type': 'application/json' });
            response.end ('{"error":"not-found"}');
            return;
        }

        // Keeps no more than the limit of a huge upload (the proxies in front cap its size).
        const pieces = [];
        let size = 0;

        for await (const piece of request)
        {
            size += piece.length;

            if (size <= LIMITS.body + 1)
                pieces.push (piece);
        }

        const answer = await handle (new Request ('http://relay' + path, {
            method: request.method,
            headers: { 'content-length': String (size) },
            body: request.method === 'POST' ? Buffer.concat (pieces) : undefined,
        }), addressOf (request));

        response.writeHead (answer.status, Object.fromEntries (answer.headers));
        response.end (await answer.text());
    });
}

if (import.meta.url === `file://${process.argv[1]}`)
{
    const port = Number (env.PORT ?? 8080);
    relayServer().listen (port, () => log (`report relay on port ${port}${configured ? '' : ' (not configured: reports answer 503)'}`));
}
