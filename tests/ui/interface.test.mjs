// The interface, driven in a browser with the plugin faked: random clicks, drags, keys,
// typing and messages from the plugin, in every look, must never raise a script error.
// A script error can leave a control dead or a change unsent to the plugin.
//
//   npm install && npx playwright install chromium && npm test

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromium } from 'playwright';

const page0 = pathToFileURL (path.resolve (path.dirname (fileURLToPath (import.meta.url)), '../../source/ui/index.html')).href;
const actionsPerLook = +(process.env.DRENCHED_UI_ACTIONS || 200);

/** Stands in for JUCE's bridge: records what the page sends, and lets the test send. */
function fakePlugin()
{
    const listeners = {};
    window.__sent = [];
    window.__JUCE__ = { backend: {
        addEventListener: (name, fn) => { (listeners[name] = listeners[name] || []).push (fn); },
        removeEventListener: () => {},
        emitEvent: (name, payload) => { window.__sent.push ([name, payload]); },
    } };
    window.__fromPlugin = (name, payload) => (listeners[name] || []).forEach (fn => fn (payload));
}

let browser, page, errors;

before (async () =>
{
    browser = await chromium.launch();
    page = await browser.newPage ({ viewport: { width: 1200, height: 760 } });
    await page.addInitScript (fakePlugin);
    page.on ('pageerror', e => errors.push (String (e)));
    page.on ('console', m => { if (m.type() === 'error') errors.push (m.text()); });
});

after (async () => browser?.close());

async function open (look)
{
    errors = [];
    await page.goto (page0 + '?k=' + look + '#' + look);
    await page.waitForTimeout (100);
}

const fromPlugin = (name, payload) => page.evaluate (([n, p]) => window.__fromPlugin (n, p), [name, payload]);

// Messages the plugin really sends (PluginEditor.cpp), with awkward but possible contents.
const NAMES = ['St Paul\'s <Nave> & "Choir"', '', 'x'.repeat (200), 'é日本 room', '<img src=x onerror=window.__ran=1>', 'a\\b\'c'];

function pluginMessage (rnd, pick)
{
    const name = pick (NAMES), decays = () => Array.from ({ length: 8 }, () => pick ([0.1, 1, 2.5, 20]));
    const r = rnd();

    if (r < 0.3)
    {
        const ui = pick ([
            () => ({ ir: pick ([{ name, missing: true }, { name, missing: false, bands: decays(), ch: pick ([1, 2, 4, 12, 16]) }]), irEvent: Math.floor (rnd() * 1000), irError: pick (['', name]) }),
            () => ({ scenes: pick ([[], [{ number: 1, name, space: name }], Array.from ({ length: 64 }, (_, i) => ({ number: i + 1, name: name + i, space: 'Hall' }))]) }),
            () => ({ outputs: pick ([[{ name: 'L', wet: true }, { name: 'R', wet: true }], Array.from ({ length: 16 }, (_, i) => ({ name: 'Ch ' + i, wet: i % 3 > 0 }))]) }),
            () => ({ speakers: pick (['', '0,0;30,0;-30,0', name]) }),
            () => ({ shortened: pick ([0, 10.5]) }),
            () => ({ spaceId: pick (['user', 'no-such-space']) }),
            () => ({ locks: pick (['', 'mix,decay,pre,size']) }),
        ])();
        return ['state', { ui }];
    }

    if (r < 0.45)
    {
        // A host's automation that isn't a number reaches the page as null.
        const params = {};

        for (const id of ['mix', 'decay63', 'decayR1k', 'size', 'predelay', 'scene', 'eq1Gain', 'freeze', 'decayLink', 'listenerX'])
            if (rnd() < 0.4)
                params[id] = null;

        return ['state', { params }];
    }

    if (r < 0.6)
        return ['matched', pick ([{ listening: true, file: name }, { error: name },
                                  { bands: decays(), heard: Array.from ({ length: 8 }, () => pick ([0, 3, 40])), spaceId: pick (['no-such', 'user']), spaceName: name, file: name, seconds: pick ([5, 600]) }])];

    if (r < 0.75)
        return ['capture', { stage: pick (['idle', 'playing', 'working', 'failed']), progress: pick ([0, 0.5, 1]), peakDb: pick ([-100, -6, 0]), message: name }];

    if (r < 0.85)
        return ['licence', { state: pick (['beta', 'licensed', 'trial', 'ended']), name, licence: pick (['', 'DRN-1234']), daysLeft: pick ([0, 13]), machine: 'ABC',
                             page: 'https://example.invalid/activate', busy: rnd() < 0.3, message: pick (['', name]), error: rnd() < 0.5, testKey: false }];

    if (r < 0.95)
    {
        // ResponseAnalysis: always 40 slices of 31 bands, 200 decay points and 8 RT60s.
        const seconds = pick ([0.01, 2.5, 20]);
        return ['analysis', { seconds, bands: Array.from ({ length: 31 }, (_, i) => 20 * Math.pow (2, i / 3)),
                              waterfall: Array.from ({ length: 40 }, () => Array.from ({ length: 31 }, () => pick ([-100, -20, 0]))), sliceSeconds: seconds / 40,
                              decay: Array.from ({ length: 200 }, (_, i) => -i * 0.4), decayStep: seconds / 199, rt60: decays() }];
    }

    return ['pasted', { target: pick (['licKey', 'licCode']), text: name }];
}

async function visible (selector)
{
    const out = [];

    for (const e of await page.$$ (selector))
        if (await e.isVisible())
            out.push (e);

    return out;
}

async function randomly (look, seed)
{
    let s = seed;
    const rnd = () => (s = (s * 1103515245 + 12345) % 2147483648) / 2147483648;
    const pick = a => a[Math.floor (rnd() * a.length)];
    const done = [];

    for (let i = 0; i < actionsPerLook; ++i)
    {
        const r = rnd();

        try
        {
            if (r < 0.5)
            {
                const controls = await visible ('[data-act]:not([disabled])');

                if (! controls.length)
                    continue;

                const c = pick (controls);
                done.push ('click ' + await c.evaluate (x => [x.getAttribute ('data-act'), x.getAttribute ('data-v'), x.getAttribute ('data-k')].filter (Boolean).join (' ')));
                await c.click ({ force: true, timeout: 500 });
            }
            else if (r < 0.75)
            {
                const targets = await visible ('[data-knob],svg[data-decay],[data-eqn],[data-drag]');

                if (! targets.length)
                    continue;

                const b = await pick (targets).boundingBox();

                if (! b)
                    continue;

                const x = b.x + rnd() * b.width, y = b.y + rnd() * b.height, dx = (rnd() - 0.5) * 600, dy = (rnd() - 0.5) * 600;
                done.push ('drag');
                await page.mouse.move (x, y);
                await page.mouse.down();
                await page.mouse.move (x + dx / 2, y + dy / 2, { steps: 3 });
                await page.mouse.move (x + dx, y + dy, { steps: 3 });
                await page.mouse.up();
            }
            else if (r < 0.85)
            {
                const key = pick (['Escape', 'Tab', 'Enter', ' ', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'PageUp', 'PageDown', 'Home', 'End', 'a', 'Backspace', 'Delete']);
                done.push ('key ' + key);
                await page.keyboard.press (key);
            }
            else if (r < 0.9)
            {
                const fields = await visible ('input:not([disabled])');

                if (! fields.length)
                    continue;

                const text = pick (NAMES.concat (['hall', '0', '-12']));
                done.push ('type ' + JSON.stringify (text.slice (0, 20)));
                await pick (fields).fill (text, { timeout: 500 });
            }
            else
            {
                const [name, payload] = pluginMessage (rnd, pick);
                done.push ('plugin ' + name);
                await fromPlugin (name, payload);
            }
        }
        catch (e)
        {
            // The page redrew under the driver; only the page's own errors count.
            if (! /Timeout|not attached|detached|outside of the viewport|intercepts pointer/.test (String (e)))
                errors.push (String (e));
        }

        if (errors.length)
            break;
    }

    return done.slice (-12).join (' | ');
}

test ('Random use of every look raises no script errors', async (t) =>
{
    await open ('anodised');
    const looks = await page.evaluate (() => SKINS.map (s => s.id));

    for (const [i, look] of looks.entries())
    {
        await t.test (look, async () =>
        {
            await open (look);
            const last = await randomly (look, 1000 + i);
            assert.deepEqual (errors, [], 'after: ' + last);
            assert.equal (await page.evaluate (() => window.__ran), undefined, 'a name was shown as markup');
        });
    }
});

test ('A redraw while a field is being edited completes', async () =>
{
    await open ('anodised');
    await page.click ('[data-act="adv"]');
    const field = (await visible ('input[data-type]'))[0];
    assert.ok (field, 'a typed value field');
    await field.fill ('12');

    // A message from the plugin redraws the page, which blurs the field and commits it.
    await fromPlugin ('state', { ui: { locks: 'mix' } });
    await page.waitForTimeout (50);
    assert.deepEqual (errors, []);
});

test ('Automation that isn\'t a number leaves the controls as they were', async () =>
{
    await open ('anodised');
    const before = await page.evaluate (() => ({ size: st.size, mix: st.mix, band: st.bands[0] }));
    await fromPlugin ('state', { params: { size: null, mix: null, decay63: null } });
    await page.waitForTimeout (50);
    assert.deepEqual (await page.evaluate (() => ({ size: st.size, mix: st.mix, band: st.bands[0] })), before);
    assert.deepEqual (errors, []);
});
