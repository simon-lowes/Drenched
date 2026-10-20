// Licence numbers, machine codes and signed activation codes. The plugin's
// source/licence/src/Licence.cpp reads and checks exactly what this writes;
// the shared test vectors in test/vectors.json keep the two in step.

// Crockford's base 32: no I, L, O or U.
const ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';

export const KEY_LENGTH = 25;
export const MACHINE_LENGTH = 20;
export const COMPUTERS_PER_LICENCE = 3;
export const MAX_NAME_BYTES = 200;

const encoder = new TextEncoder();

async function sha512 (text)
{
    return new Uint8Array (await crypto.subtle.digest ('SHA-512', encoder.encode (text)));
}

function symbolsFrom (bytes, count)
{
    let out = '';

    for (let i = 0; i < count; ++i)
    {
        let value = 0;

        for (let bit = i * 5; bit < i * 5 + 5; ++bit)
            value = (value << 1) | ((bytes[bit >> 3] >> (7 - (bit & 7))) & 1);

        out += ALPHABET[value];
    }

    return out;
}

function grouped (raw)
{
    return raw.match (/.{1,5}/g).join ('-');
}

/** The symbols someone typed, in canonical form, if there are exactly `length` of them. */
function canonicalSymbols (typed, length)
{
    if (typeof typed !== 'string')
        return null;

    let raw = '';

    for (const ch of typed)
    {
        if (ch === '-' || /\s/.test (ch))
            continue;

        let c = ch.toUpperCase();

        if (c === 'O')
            c = '0';
        else if (c === 'I' || c === 'L')
            c = '1';

        if (c.length !== 1 || ! ALPHABET.includes (c) || raw.length === length)
            return null;

        raw += c;
    }

    return raw.length === length ? raw : null;
}

async function checkSymbols (body)
{
    const hash = await sha512 ('Drenched licence number ' + body);
    return ALPHABET[hash[0] >> 3] + ALPHABET[hash[1] >> 3];
}

/** A new licence number from 15 random bytes. */
export async function makeKey (randomBytes = crypto.getRandomValues (new Uint8Array (15)))
{
    const body = symbolsFrom (randomBytes, KEY_LENGTH - 2);
    return grouped (body + await checkSymbols (body));
}

/** A typed licence number in canonical form, or null if it is not one. */
export async function parseKey (typed)
{
    const raw = canonicalSymbols (typed, KEY_LENGTH);

    if (raw === null || raw.slice (-2) !== await checkSymbols (raw.slice (0, -2)))
        return null;

    return grouped (raw);
}

/** A machine code from the operating system's identifier, as the plugin makes it. */
export async function machineCode (deviceIdentifier)
{
    return grouped (symbolsFrom (await sha512 ('Drenched machine ' + deviceIdentifier), MACHINE_LENGTH));
}

/** A typed machine code in canonical form, or null if it is not one. */
export function parseMachineCode (typed)
{
    const raw = canonicalSymbols (typed, MACHINE_LENGTH);
    return raw === null ? null : grouped (raw);
}

/** A buyer's name made safe for an activation code: one line, at most 200 bytes. */
export function cleanName (name)
{
    let clean = String (name ?? '').replace (/[\u0000-\u001f\u007f]/g, ' ').replace (/\s+/g, ' ').trim();

    while (encoder.encode (clean).length > MAX_NAME_BYTES)
        clean = Array.from (clean).slice (0, -1).join ('').trimEnd();

    return clean;
}

export function activationPayload ({ key, machine, name, issued })
{
    return 'Drenched activation 1\n'
         + `licence: ${key}\n`
         + `machine: ${machine}\n`
         + `name: ${name}\n`
         + `issued: ${issued}\n`;
}

function base64Url (bytes)
{
    let binary = '';

    for (const b of bytes)
        binary += String.fromCharCode (b);

    return btoa (binary).replace (/\+/g, '-').replace (/\//g, '_').replace (/=+$/, '');
}

function fromBase64Url (text)
{
    const binary = atob (text.replace (/-/g, '+').replace (/_/g, '/'));
    return Uint8Array.from (binary, (c) => c.charCodeAt (0));
}

/** The server's signing key, from the JWK held in the SIGNING_KEY secret. */
export function importSigningKey (jwkJson)
{
    return crypto.subtle.importKey ('jwk', JSON.parse (jwkJson), { name: 'Ed25519' }, false, ['sign']);
}

/** A signed activation code: DRN1.<payload>.<signature>, both base64url. */
export async function activationCode (activation, signingKey)
{
    const payload = encoder.encode (activationPayload (activation));
    const signature = new Uint8Array (await crypto.subtle.sign ('Ed25519', signingKey, payload));
    return 'DRN1.' + base64Url (payload) + '.' + base64Url (signature);
}

/** The activation inside a code if publicKey (a CryptoKey) signed it; for tests and support. */
export async function readActivationCode (code, publicKey)
{
    const parts = String (code).replace (/\s+/g, '').split ('.');

    if (parts.length !== 3 || parts[0] !== 'DRN1')
        return null;

    const payload = fromBase64Url (parts[1]);

    if (! await crypto.subtle.verify ('Ed25519', publicKey, fromBase64Url (parts[2]), payload))
        return null;

    const lines = new TextDecoder().decode (payload).split ('\n');
    const value = (i, label) => lines[i]?.startsWith (label + ': ') ? lines[i].slice (label.length + 2) : null;

    return { key: value (1, 'licence'), machine: value (2, 'machine'), name: value (3, 'name'), issued: value (4, 'issued') };
}
