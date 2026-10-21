// The published test signing key (seed 01 02 ... 20) and the vectors shared with tests/LicenceTests.cpp.

import { readFileSync } from 'node:fs';

export const vectors = JSON.parse (readFileSync (new URL ('./vectors.json', import.meta.url)));

export const hex = (s) => Uint8Array.from (s.match (/../g) ?? [], (b) => parseInt (b, 16));

export async function testKeys()
{
    const pkcs8 = new Uint8Array ([...hex ('302e020100300506032b657004220420'), ...hex (vectors.seed)]);
    const privateKey = await crypto.subtle.importKey ('pkcs8', pkcs8, { name: 'Ed25519' }, true, ['sign']);
    const jwk = await crypto.subtle.exportKey ('jwk', privateKey);
    const publicKey = await crypto.subtle.importKey ('jwk', { kty: 'OKP', crv: 'Ed25519', x: jwk.x }, { name: 'Ed25519' }, true, ['verify']);
    return { privateKey, publicKey, jwk };
}
