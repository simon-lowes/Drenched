import assert from 'node:assert/strict';
import { test } from 'node:test';

import * as L from '../src/licence.js';
import { hex, testKeys, vectors } from './keys.js';


test ('licence numbers and machine codes match the plugin', async () =>
{
    for (const { random, key } of vectors.keys)
        assert.equal (await L.makeKey (hex (random)), key);

    for (const { device, code } of vectors.machines)
        assert.equal (await L.machineCode (device), code);
});

test ('typed licence numbers are tidied, mistakes refused', async () =>
{
    const key = vectors.keys[0].key;
    assert.equal (await L.parseKey (key), key);
    assert.equal (await L.parseKey (' 0ca2a dj7b1 mqn2w wnpzc zr7cs\n'), key);
    assert.equal (await L.parseKey ('OCA2ADJ7BIMQN2WWNPZCZR7CS'), key);
    assert.equal (await L.parseKey ('0CA2A-DJ7Bl-MQN2W-WNPZC-ZR7CS'), key);

    for (const bad of ['', '0CA2A-DJ7B1-MQN2W-WNPZC-ZR7C', '0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CSS', '0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CT', '0CA2A-DJ7B1-MQN2W-WNPZC-ZR7CU', null, 42, 'ß'.repeat (25)])
        assert.equal (await L.parseKey (bad), null, String (bad));

    const fresh = await L.makeKey();
    assert.match (fresh, /^([0-9A-HJKMNP-TV-Z]{5}-){4}[0-9A-HJKMNP-TV-Z]{5}$/);
    assert.equal (await L.parseKey (fresh), fresh);
});

test ('machine codes are tidied', () =>
{
    assert.equal (L.parseMachineCode ('2cy9a gkpdz 6vh9n z3sbp'), '2CY9A-GKPDZ-6VH9N-Z3SBP');
    assert.equal (L.parseMachineCode ('2CY9A-GKPDZ-6VH9N'), null);
    assert.equal (L.parseMachineCode (undefined), null);
});

test ('activation codes are signed exactly as the plugin expects', async () =>
{
    const { privateKey, publicKey } = await testKeys();
    assert.equal (await L.activationCode (vectors.activation, privateKey), vectors.code);
    assert.deepEqual (await L.readActivationCode (vectors.code, publicKey), vectors.activation);

    const tampered = vectors.code.slice (0, 30) + (vectors.code[30] === 'A' ? 'B' : 'A') + vectors.code.slice (31);
    assert.equal (await L.readActivationCode (tampered, publicKey), null);
});

test ('names are made safe for a code', () =>
{
    assert.equal (L.cleanName ('  Ada\n\tLovelace  '), 'Ada Lovelace');
    assert.equal (L.cleanName (undefined), '');
    assert.ok (new TextEncoder().encode (L.cleanName ('é'.repeat (300))).length <= L.MAX_NAME_BYTES);
});
