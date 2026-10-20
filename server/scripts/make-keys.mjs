// Makes the activation server's signing key.
//
//   node scripts/make-keys.mjs           prints a new key pair
//   node scripts/make-keys.mjs --write   also writes its public half into the plugin
//
// The private half (a JWK) goes into the Worker as a secret and nowhere else:
//   npx wrangler secret put SIGNING_KEY    (paste the JWK line)
// Losing it means existing codes still work but no new ones can be signed with
// the same key; leaking it means anyone can make codes. Keep a copy offline.

import { readFileSync, writeFileSync } from 'node:fs';

const pair = await crypto.subtle.generateKey ({ name: 'Ed25519' }, true, ['sign', 'verify']);
const jwk = await crypto.subtle.exportKey ('jwk', pair.privateKey);
const publicKey = Buffer.from (jwk.x, 'base64url');

const bytes = [...publicKey].map ((b) => '0x' + b.toString (16).padStart (2, '0'));
const array = bytes.slice (0, 16).join (', ') + ',\n                                     ' + bytes.slice (16).join (', ');

console.log ('SIGNING_KEY (secret, for wrangler secret put):');
console.log (JSON.stringify ({ kty: jwk.kty, crv: jwk.crv, x: jwk.x, d: jwk.d }));
console.log ('\nPublic key: ' + publicKey.toString ('hex'));

if (process.argv.includes ('--write'))
{
    const path = new URL ('../../source/licence/src/Licence.cpp', import.meta.url);
    const source = readFileSync (path, 'utf8');
    const block = /\/\/ BEGIN SERVER KEY\n[\s\S]*?\/\/ END SERVER KEY/;

    if (! block.test (source))
        throw new Error ('No server key block in Licence.cpp');

    writeFileSync (path, source.replace (block, `// BEGIN SERVER KEY\n    static constexpr PublicKey key { ${array} };\n    return key;\n    // END SERVER KEY`));
    console.log ('\nWrote the public key into source/licence/src/Licence.cpp.');
}
