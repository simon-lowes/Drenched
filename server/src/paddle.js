// Paddle, the shop: its webhooks tell the server about each sale (to issue and
// email licence numbers) and each refund or chargeback (to withdraw them).
// https://developer.paddle.com/webhooks/overview

const encoder = new TextEncoder();

/** Signatures older than this are refused, so a captured webhook cannot be replayed later. */
const TOLERANCE_SECONDS = 300;

/** At most this many licences from one transaction. */
export const MAX_COPIES = 20;

function hex (bytes)
{
    return [...new Uint8Array (bytes)].map ((b) => b.toString (16).padStart (2, '0')).join ('');
}

function sameText (a, b)
{
    let diff = a.length ^ b.length;

    for (let i = 0; i < Math.max (a.length, b.length); ++i)
        diff |= (a.charCodeAt (i) || 0) ^ (b.charCodeAt (i) || 0);

    return diff === 0;
}

/** Checks the Paddle-Signature header ("ts=...;h1=...") against the raw body. */
export async function verifySignature (header, rawBody, secret, nowSeconds = Date.now() / 1000)
{
    if (! header || ! secret)
        return false;

    let ts = null;
    const signatures = [];

    for (const part of header.split (';'))
    {
        const [name, value = ''] = part.trim().split ('=');

        if (name === 'ts')
            ts = value;
        else if (name === 'h1')
            signatures.push (value);
    }

    if (! ts || ! /^\d+$/.test (ts) || Math.abs (nowSeconds - Number (ts)) > TOLERANCE_SECONDS || signatures.length === 0)
        return false;

    const key = await crypto.subtle.importKey ('raw', encoder.encode (secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    const expected = hex (await crypto.subtle.sign ('HMAC', key, encoder.encode (ts + ':' + rawBody)));

    // During a secret rotation Paddle sends one signature per secret.
    return signatures.some ((s) => sameText (s, expected));
}

/** Signs a body as Paddle does; for tests and for trying the hook by hand. */
export async function signBody (rawBody, secret, nowSeconds = Math.floor (Date.now() / 1000))
{
    const key = await crypto.subtle.importKey ('raw', encoder.encode (secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    return `ts=${nowSeconds};h1=${hex (await crypto.subtle.sign ('HMAC', key, encoder.encode (nowSeconds + ':' + rawBody)))}`;
}

/** Copies of Drenched in a completed transaction: the quantity of its Drenched items. */
export function copiesIn (transaction, productId)
{
    const copies = (transaction.items ?? [])
        .filter ((item) => ! productId || item.price?.product_id === productId)
        .reduce ((sum, item) => sum + Math.max (0, Math.floor (Number (item.quantity) || 0)), 0);

    return Math.min (copies, MAX_COPIES);
}

/** The buyer's name and email, from Paddle's API (webhooks carry only the customer id). */
export async function fetchCustomer (customerId, { apiKey, apiBase = 'https://api.paddle.com', fetch = globalThis.fetch })
{
    const response = await fetch (`${apiBase}/customers/${encodeURIComponent (customerId)}`, {
        headers: { authorization: `Bearer ${apiKey}`, accept: 'application/json' },
    });

    if (! response.ok)
        throw new Error (`Paddle customer lookup failed: ${response.status}`);

    const { data } = await response.json();
    return { name: data?.name ?? '', email: data?.email ?? '' };
}
