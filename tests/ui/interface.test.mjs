// The interface, driven in a browser with the plugin faked: random clicks, drags, keys,
// typing and messages from the plugin, in every look, must never raise a script error.
// A script error can leave a control dead or a change unsent to the plugin.
//
//   npm install && npx playwright install chromium && npm test

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromium } from 'playwright';

const here = path.dirname (fileURLToPath (import.meta.url));
const page0 = pathToFileURL (path.resolve (here, '../../source/ui/index.html')).href;
// DRENCHED_UI_ACTIONS and DRENCHED_UI_SEED run the random test longer, or on other seeds.
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
            // The page redrew under the driver (a control hidden or replaced between finding it
            // and clicking it); only the page's own errors count.
            if (! /Timeout|not attached|detached|not visible|outside of the viewport|intercepts pointer/.test (String (e)))
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
            const last = await randomly (look, +(process.env.DRENCHED_UI_SEED || 1000) + i);
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

    // A message from the plugin redraws the page around the field being edited.
    await fromPlugin ('state', { ui: { locks: 'mix' } });
    await page.waitForTimeout (50);
    assert.deepEqual (errors, []);
});

test ('A value typed while the plugin sends updates is what was typed, and only it is sent', async () =>
{
    // Automation on any parameter redraws the page. That must neither commit half of what is
    // being typed nor put the next key after the formatted value ("1%2" read as 2).
    await open ('anodised');
    const looks = await page.evaluate (() => SKINS.map (s => s.id));
    let tried = 0;

    for (const look of looks)
    {
        await page.goto ('about:blank');
        await open (look);
        const field = (await visible ('input[data-fid="ro-mix"]'))[0];

        if (! field)
            continue;

        ++tried;
        await field.click();
        await page.evaluate (() => { window.__sent = []; document.activeElement.__typedIn = true; });
        await page.keyboard.press ('ControlOrMeta+A');
        await page.keyboard.type ('1');
        await fromPlugin ('state', { params: { size: 120, decay1k: 3 } });
        await page.waitForTimeout (50);
        await fromPlugin ('state', { ui: { locks: 'mix' } });
        await page.waitForTimeout (50);
        const mid = await page.evaluate (() => ({ value: document.activeElement.value, same: document.activeElement.__typedIn === true }));
        await page.keyboard.type ('2');
        await fromPlugin ('state', { params: { predelay: 30 } });
        await page.waitForTimeout (50);
        await page.keyboard.press ('Enter');
        await page.waitForTimeout (50);

        const end = await page.evaluate (() => ({ mix: st.mix, value: document.activeElement.value,
                                                  sent: window.__sent.filter (([n, p]) => n === 'params' && 'mix' in p).map (([, p]) => p.mix) }));
        assert.deepEqual (mid, { value: '1', same: true }, look + ': the field was disturbed by the update');
        assert.equal (end.mix, 12, look);
        assert.deepEqual (end.sent, [12], look + ': values sent to the plugin');
        assert.equal (end.value, '12%', look + ': the entered value is shown formatted');
    }

    assert.ok (tried >= 3, 'looks with a Mix value field: ' + tried);
    assert.deepEqual (errors, []);
});

test ('The focused control is kept across a redraw, and an input method can compose in the search', async () =>
{
    await page.goto ('about:blank');
    await open ('anodised');

    // A knob changed from the keyboard stays the same element, so a screen reader hears its
    // value change rather than a new control.
    await page.focus ('[data-fid="kn-mix"]');
    await page.evaluate (() => { document.activeElement.__mark = true; });
    await page.keyboard.press ('ArrowUp');
    await fromPlugin ('state', { params: { size: 120 } });
    await page.waitForTimeout (50);
    assert.ok (await page.evaluate (() => document.activeElement.__mark === true && document.activeElement.getAttribute ('aria-valuenow') === String (+st.mix.toFixed (2))),
               'the knob was replaced');

    // Japanese typed into the space search: "ka", "kat", then the choice is committed.
    await page.click ('[data-act="browse"]');
    await page.waitForTimeout (50);
    await page.evaluate (() => { document.activeElement.__mark = true; });
    const cdp = await page.context().newCDPSession (page);
    await cdp.send ('Input.imeSetComposition', { text: 'ka', selectionStart: 2, selectionEnd: 2 });
    await page.waitForTimeout (50);
    await cdp.send ('Input.imeSetComposition', { text: 'kat', selectionStart: 3, selectionEnd: 3 });
    await page.waitForTimeout (50);
    await fromPlugin ('state', { params: { mix: 40 } });
    await page.waitForTimeout (50);
    await cdp.send ('Input.insertText', { text: 'かと' });
    await page.waitForTimeout (50);
    await cdp.detach();

    const q = await page.evaluate (() => ({ value: document.activeElement.value, id: document.activeElement.id, same: document.activeElement.__mark === true, searched: st.brwQ }));
    assert.deepEqual (q, { value: 'かと', id: 'brw-q', same: true, searched: 'かと' });

    // Plain typing keeps the caret where it is.
    await page.keyboard.press ('Home');
    await page.keyboard.type ('ab');
    await page.waitForTimeout (50);
    assert.deepEqual (await page.evaluate (() => [document.activeElement.value, document.activeElement.selectionStart, document.activeElement.__mark === true]), ['abかと', 2, true]);
    assert.deepEqual (errors, []);
});

test ('Every control of the Catalogue look is on the plate and can be clicked, in every space', async () =>
{
    // The column beside the photograph holds the name, the details, the decay figure, the
    // values, Mix and the actions; a long name or several positions must not push any off.
    const library = JSON.parse (fs.readFileSync (path.resolve (here, '../../library/library.json'), 'utf8'));

    try
    {
        for (const viewport of [{ width: 1200, height: 760 }, { width: 600, height: 380 }])
        {
            await page.setViewportSize (viewport);

            for (const which of ['preview', 'library'])
            {
                await page.goto ('about:blank');
                await open ('catalogue');

                const bad = await page.evaluate (async ([lib]) =>
                {
                    if (lib)
                        setLibrary (lib);

                    const out = [], frame = () => new Promise (r => requestAnimationFrame (() => requestAnimationFrame (r)));

                    for (let i = 0; i < SPACES.length; ++i)
                    {
                        for (const mode of ['studio', 'stage'])
                        {
                            Object.assign (st, { sp: i, mode, bands: SPACES[i].bands.slice(), rbands: SPACES[i].bands.slice() });
                            window.schedule();
                            await frame();

                            const screen = document.querySelector ('#live .screen'), box = screen.getBoundingClientRect(), where = SPACES[i].name + ', ' + mode + ': ';

                            if (screen.scrollHeight > 760 || screen.scrollTop)
                                out.push (where + 'the plate is ' + screen.scrollHeight + ' px tall');

                            for (const c of screen.querySelectorAll ('[data-act],[data-knob],input,[tabindex="0"]'))
                            {
                                const r = c.getBoundingClientRect();

                                if (! r.width)
                                    continue;

                                const hit = document.elementFromPoint (r.left + r.width / 2, r.top + r.height / 2);

                                if (r.top < box.top - 0.5 || r.bottom > box.bottom + 0.5 || r.left < box.left - 0.5 || r.right > box.right + 0.5 || ! hit || ! (hit === c || c.contains (hit)))
                                    out.push (where + (c.getAttribute ('data-act') || c.getAttribute ('data-knob') || c.getAttribute ('data-fid')));
                            }
                        }
                    }

                    return out;
                }, [which === 'library' ? library : null]);

                assert.deepEqual (bad, [], viewport.width + 'x' + viewport.height + ', ' + which + ' spaces');
            }
        }
    }
    finally
    {
        await page.setViewportSize ({ width: 1200, height: 760 });
    }

    assert.deepEqual (errors, []);
});

test ('The interface scripts are ASCII only', () =>
{
    // WebKitGTK may not read the scripts as UTF-8; write other characters as \u escapes.
    const ui = path.resolve (here, '../../source/ui');
    const scripts = fs.readdirSync (ui, { recursive: true }).filter (f => f.endsWith ('.js'));
    assert.ok (scripts.length >= 2, 'scripts found: ' + scripts);

    for (const f of scripts)
    {
        const bytes = fs.readFileSync (path.join (ui, f)), at = bytes.findIndex (b => b > 0x7f);
        const line = at < 0 ? 0 : bytes.subarray (0, at).toString ('latin1').split ('\n').length;
        assert.equal (at, -1, f + ' has a byte above 0x7F on line ' + line);
    }
});

test ('Every control a drag takes is let go however the drag ends', async () =>
{
    // A fresh page: going to the same address again keeps an earlier test's open panels.
    await page.goto ('about:blank');
    await open ('anodised');

    // How a drag can end: let go, cancelled, the pointer taken away, the window left, or
    // a second pointer starting another drag before the first ends.
    for (const ending of ['pointerup', 'pointercancel', 'lostpointercapture', 'blur', 'second'])
    {
        const held = await page.evaluate ((how) =>
        {
            const knobs = [...document.querySelectorAll ('[data-knob]')].filter (k => k.getBoundingClientRect().width > 0);
            if (knobs.length < 2)
                return { knobs: knobs.length, count: {}, began: false };

            const at = (k, type, dy) =>
            {
                const r = k.getBoundingClientRect();
                k.dispatchEvent (new PointerEvent (type, { bubbles: true, cancelable: true, pointerId: 7, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2 + dy }));
            };

            window.__sent = [];
            at (knobs[0], 'pointerdown', 0);
            at (knobs[0], 'pointermove', -20);

            if (how === 'blur')
                window.dispatchEvent (new Event ('blur'));
            else if (how === 'second')
            {
                at (knobs[1], 'pointerdown', 0);
                at (knobs[1], 'pointerup', 0);
            }
            else
                at (knobs[0], how, -20);

            // Taken minus let go, for each control the page told the plugin about.
            const count = {};

            for (const [name, payload] of window.__sent)
                if (name === 'gesture')
                    for (const id of payload.ids)
                        count[id] = (count[id] || 0) + (payload.begin ? 1 : -1);

            return { count, began: window.__sent.some (([name, payload]) => name === 'gesture' && payload.begin) };
        }, ending);

        assert.ok (held.began, ending + ': the drag never took its control ' + JSON.stringify (held));
        assert.ok (Object.values (held.count).every (n => n === 0), ending + ' left controls held: ' + JSON.stringify (held.count));
    }

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

test ('Typed values are read however they are written', async () =>
{
    await open ('anodised');

    // The advanced panel may still be open from an earlier test.
    if (! await page.$ ('input[data-type="hicut"]'))
        await page.click ('[data-act="adv"]');

    // What someone types, and what the control should end up at, in the field's own unit.
    const cases = [
        ['band', '1,5', 1.5], ['band', '800 ms', 0.8], ['band', '.5', 0.5],
        ['pre', '0,1 s', 100], ['pre', '.05 s', 50], ['pre', '12 ms', 12],
        ['gate', '1,5 s', 1500], ['gate', '1,200 ms', 1200], ['gate', '1 200', 1200],
        ['bright', '−3,5 dB', -3.5], ['bright', '–2', -2], ['bright', '-.5', -0.5],
        ['hicut', '8', 8000], ['hicut', '8,5 kHz', 8500], ['hicut', '12,000 Hz', 12000], ['hicut', '12 000', 12000], ['hicut', '800 Hz', 1000], ['hicut', '800', 1000],
        ['xover', '0,5 kHz', 500], ['locut', '120', 120], ['dDel', '2,5', 2.5], ['el', '30 / 70', 70],
    ];

    for (const [type, typed, wanted] of cases)
    {
        const got = await page.evaluate (([ty, text]) =>
        {
            const field = document.querySelector ('input[data-type="' + ty + '"]');
            field.value = text;
            field.dispatchEvent (new Event ('change', { bubbles: true }));
            return ty === 'band' ? curBands (st)[0] : getV (st, ty);
        }, [type, typed]);

        assert.ok (Math.abs (got - wanted) < 1e-6, type + ' typed as ' + JSON.stringify (typed) + ' gave ' + got + ', not ' + wanted);
    }

    assert.deepEqual (errors, []);
});

test ('A dropped impulse response reaches the plugin whole, and nothing else is taken', async () =>
{
    await open ('field');

    const result = await page.evaluate (async () =>
    {
        const drop = (type, files) =>
        {
            const data = new DataTransfer();
            files.forEach (f => data.items.add (f));
            document.querySelector ('#live .screen').dispatchEvent (new DragEvent (type, { bubbles: true, cancelable: true, dataTransfer: data }));
        };

        // Larger than one piece, with every byte value in it.
        const bytes = new Uint8Array (7 * 1024 * 1024 + 5);
        for (let i = 0; i < bytes.length; ++i)
            bytes[i] = (i * 7 + (i >> 11)) & 255;

        const ir = new File ([bytes], 'Hall "B".WAV');
        const out = {};

        drop ('dragenter', [ir]);
        out.shown = document.getElementById ('drop').classList.contains ('on');
        window.__sent = [];
        drop ('drop', [new File (['text'], 'notes.txt'), ir]);
        out.hidden = ! document.getElementById ('drop').classList.contains ('on');

        for (let wait = 0; wait < 200 && ! window.__sent.some (([n, p]) => n === 'dropIr' && p.stage === 'end'); ++wait)
            await new Promise (r => setTimeout (r, 20));

        const sent = window.__sent.filter (([n]) => n === 'dropIr').map (([, p]) => p);
        out.stages = sent.map (p => p.stage);
        out.start = sent[0];
        out.sameId = sent.every (p => p.id === sent[0].id);

        const joined = sent.filter (p => p.stage === 'piece').map (p => Uint8Array.from (atob (p.data), c => c.charCodeAt (0)));
        const all = new Uint8Array (joined.reduce ((n, b) => n + b.length, 0));
        joined.reduce ((at, b) => { all.set (b, at); return at + b.length; }, 0);
        out.whole = all.length === bytes.length && all.every ((b, i) => b === bytes[i]);

        // Only files Drenched can load are sent.
        window.__sent = [];
        drop ('drop', [new File (['x'], 'photo.jpg')]);
        await new Promise (r => setTimeout (r, 50));
        out.otherSent = window.__sent.filter (([n]) => n === 'dropIr').length;
        out.otherNote = st.note;

        window.__fromPlugin ('dropFailed', { name: 'Hall "B".WAV' });
        out.failedNote = st.note;
        window.__fromPlugin ('dropFailed', { name: 'Huge.wav', large: true });
        out.largeNote = st.note;
        return out;
    });

    assert.ok (result.shown && result.hidden);
    assert.deepEqual (result.stages, ['start', 'piece', 'piece', 'piece', 'end']);
    assert.equal (result.start.name, 'Hall "B".WAV');
    assert.equal (result.start.size, 7 * 1024 * 1024 + 5);
    assert.equal (result.start.keep, false);
    assert.ok (result.sameId && result.whole);
    assert.equal (result.otherSent, 0);
    assert.match (result.otherNote, /photo\.jpg.*isn.t an impulse response/);
    assert.match (result.failedNote, /Drop it again/);
    assert.match (result.largeNote, /Huge\.wav.*too large to drop here.*Load your own IR/);
    assert.ok (page.url().includes ('index.html'));
    assert.deepEqual (errors, []);
});

/** Where the keyboard is: the open window it is in, and whether everything behind that window is inert. */
const whereFocus = () => page.evaluate (() =>
{
    const a = document.activeElement, screen = document.querySelector ('#live .screen');
    const dialog = [...screen.children].filter (c => c.getAttribute ('role') === 'dialog').pop() || null;
    return { inDialog: !! dialog && dialog.contains (a), body: a === document.body,
             behindInert: !! dialog && [...screen.children].every (c => c === dialog || c.hasAttribute ('inert')),
             fid: a.getAttribute ('data-fid'), act: a.getAttribute ('data-act') };
});

test ('A window holds the keyboard, and gives it back to the control that opened it', async () =>
{
    await page.goto ('about:blank');
    await open ('anodised');

    // Opened from the keyboard: Tab and Shift+Tab go round the window, Escape gives the
    // keyboard back to the control that opened it.
    for (const opener of ['[data-act="eq"]', '[data-act="wf"]', '[data-act="licence"]', '.wmb[data-act="skins"]', '[data-act="browse"]'])
    {
        await page.focus (opener);
        await page.keyboard.press ('Enter');
        await page.waitForTimeout (50);
        let at = await whereFocus();
        assert.ok (at.inDialog && at.behindInert, opener + ' opened: ' + JSON.stringify (at));

        for (const key of ['Tab', 'Shift+Tab'])
            for (let i = 0; i < 40; ++i)
            {
                await page.keyboard.press (key);
                at = await whereFocus();
                assert.ok (at.inDialog, opener + ', ' + key + ' ' + (i + 1) + ' left the window: ' + JSON.stringify (at));
            }

        await page.keyboard.press ('Escape');
        await page.waitForTimeout (50);
        assert.ok (await page.evaluate ((sel) => document.activeElement === document.querySelector (sel) && ! document.querySelector ('#live [inert]'), opener),
                   opener + ': Escape left the keyboard at ' + JSON.stringify (await whereFocus()));
    }

    // A window opened from another (the browser's tools) gives the keyboard back to the first opener.
    await page.focus ('[data-act="browse"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    await page.focus ('.brw [data-act="scenes"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    assert.equal ((await whereFocus()).inDialog, true);
    await page.focus ('.scn .cx');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    assert.equal (await page.evaluate (() => document.activeElement.getAttribute ('data-act')), 'browse', 'closed with its Close button');

    // A look picked from the keyboard: the keyboard is on the new look's own wordmark.
    await page.focus ('.wmb[data-act="skins"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    await page.focus ('.skins .sk[data-v="signal"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    assert.deepEqual (await page.evaluate (() => [st.skin, document.activeElement.matches ('#live .s-signal .wmb[data-act="skins"]')]), ['signal', true]);

    // Capture closes itself when the room is captured: the keyboard goes back as well.
    await page.focus ('[data-act="browse"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    await page.focus ('.brw [data-act="capture"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    await fromPlugin ('capture', { stage: 'working', progress: 1, peakDb: -12, message: '' });
    await page.waitForTimeout (50);
    await fromPlugin ('capture', { stage: 'idle', progress: 0, peakDb: -100, message: '' });
    await page.waitForTimeout (50);
    assert.deepEqual (await page.evaluate (() => [st.capture, document.activeElement.getAttribute ('data-act')]), [false, 'browse']);
    assert.deepEqual (errors, []);
});

test ('A control clicked or dragged leaves the host its keys, and the keyboard keeps every control', async () =>
{
    await page.goto ('about:blank');
    await open ('anodised');
    await page.evaluate (() =>
    {
        window.__prevented = [];
        window.addEventListener ('keydown', e => window.__prevented.push ([e.key, e.defaultPrevented]));
    });

    const centre = async (sel) => { const b = await (await page.$ (sel)).boundingBox(); return [b.x + b.width / 2, b.y + b.height / 2]; };
    const decay = () => page.evaluate (() => decayOf (st.bands));

    // Click Decay, then Home (return to zero in many hosts): the decay stays, and the key isn't taken.
    const d0 = await decay();
    await page.mouse.click (...await centre ('[data-fid="kn-decay"]'));
    await page.waitForTimeout (50);
    await page.keyboard.press ('Home');
    await page.keyboard.press ('ArrowUp');
    await page.waitForTimeout (50);
    assert.equal (await decay(), d0, 'Home or an arrow after a click changed Decay');
    assert.deepEqual (await page.evaluate (() => window.__prevented.filter (([, p]) => p)), [], 'a key the page did not use was prevented');

    // Drag Mix, then the arrows: Mix stays where the drag left it.
    const [mx, my] = await centre ('[data-fid="kn-mix"]');
    await page.mouse.move (mx, my);
    await page.mouse.down();
    await page.mouse.move (mx, my - 40, { steps: 4 });
    await page.mouse.up();
    await page.waitForTimeout (50);
    const mix = await page.evaluate (() => st.mix);
    await page.keyboard.press ('ArrowDown');
    await page.keyboard.press ('End');
    assert.equal (await page.evaluate (() => st.mix), mix);

    // Click Freeze, then Space (play and stop in every host): Freeze stays on.
    await page.click ('[data-act="freeze"]');
    await page.waitForTimeout (50);
    await page.keyboard.press (' ');
    await page.waitForTimeout (50);
    assert.equal (await page.evaluate (() => st.freeze), true, 'Space after a click toggled Freeze back');

    // Click a point on the decay graph, then the arrows: the band stays.
    const band = await page.evaluate (() => st.bands[3]);
    const [nx, ny] = await centre ('[data-fid="nd-3"]');
    await page.mouse.move (nx, ny);
    await page.mouse.down();
    await page.mouse.up();
    await page.waitForTimeout (50);
    const clicked = await page.evaluate (() => st.bands[3]);
    await page.keyboard.press ('ArrowUp');
    assert.equal (await page.evaluate (() => st.bands[3]), clicked);
    assert.ok (Math.abs (clicked - band) < 0.2);

    // Click an EQ point, then Delete: its gain stays. The window keeps the keyboard itself.
    await page.click ('[data-act="eq"]');
    await page.waitForTimeout (50);
    await page.evaluate (() => { st.eq3g = 6; window.schedule(); });
    await page.waitForTimeout (50);
    await page.mouse.click (...await centre ('[data-eqn="2"] circle'));
    await page.waitForTimeout (50);
    await page.keyboard.press ('Delete');
    await page.waitForTimeout (50);
    assert.equal (await page.evaluate (() => st.eq3g), 6, 'Delete after a click reset the EQ point');
    assert.deepEqual (await whereFocus(), { inDialog: true, body: false, behindInert: true, fid: 'dlg-eq', act: null });

    // From the keyboard every one of them still works.
    await page.focus ('[data-eqn="2"]');
    await page.keyboard.press ('Delete');
    assert.equal (await page.evaluate (() => st.eq3g), 0, 'Delete on an EQ point focused from the keyboard');
    await page.keyboard.press ('Escape');
    await page.waitForTimeout (50);
    await page.focus ('[data-act="freeze"]');
    await page.keyboard.press (' ');
    await page.waitForTimeout (50);
    assert.equal (await page.evaluate (() => st.freeze), false, 'Space on Freeze focused from the keyboard');
    await page.focus ('[data-fid="kn-decay"]');
    await page.keyboard.press ('Home');
    const low = await decay();
    assert.ok (low < 0.11, 'Home on Decay focused from the keyboard: ' + low);

    // Keys with Ctrl, Alt or Cmd held are the host's, even on a focused control.
    await page.evaluate (() => { window.__prevented = []; });
    await page.keyboard.press ('Control+ArrowUp');
    await page.keyboard.press ('Alt+End');
    await page.keyboard.press ('Meta+ArrowUp');
    assert.equal (await decay(), low);
    assert.deepEqual (await page.evaluate (() => window.__prevented.filter (([, p]) => p)), []);

    // A text field clicked keeps the keyboard, to type in.
    await page.click ('input[data-fid="ro-mix"]');
    await page.waitForTimeout (50);
    assert.equal (await page.evaluate (() => document.activeElement.getAttribute ('data-fid')), 'ro-mix');
    assert.deepEqual (errors, []);
});

test ('The Decay knob covers the whole range of the bands, and never moves against the key', async () =>
{
    await page.goto ('about:blank');
    await open ('anodised');
    const all = (v) => Object.fromEntries (['decay63', 'decay125', 'decay250', 'decay500', 'decay1k', 'decay2k', 'decay4k', 'decay8k'].map (id => [id, v]));
    const knob = () => page.evaluate (() =>
    {
        const k = document.querySelector ('[data-fid="kn-decay"]');
        return { bands: st.bands.slice(), now: +k.getAttribute ('aria-valuenow'), min: +k.getAttribute ('aria-valuemin'), max: +k.getAttribute ('aria-valuemax') };
    });

    // Every band at the 20 s ceiling: the knob reports 20 within its range, and Up leaves it there.
    await fromPlugin ('state', { params: all (20) });
    await page.waitForTimeout (50);
    await page.focus ('[data-fid="kn-decay"]');
    let k = await knob();
    assert.deepEqual ([k.now, k.min, k.max], [20, 0.1, 20]);
    for (const key of ['ArrowUp', 'PageUp', 'Shift+ArrowUp', 'End'])
        await page.keyboard.press (key);
    await page.waitForTimeout (50);
    assert.deepEqual ((await knob()).bands, Array (8).fill (20), 'a step up pulled the bands down');
    await page.keyboard.press ('ArrowDown');
    await page.waitForTimeout (50);
    k = await knob();
    assert.ok (k.bands.every (b => b < 20) && k.now < 20 && k.now >= k.min, JSON.stringify (k));

    // Long but under the old 12 s end: Up still lengthens it.
    await fromPlugin ('state', { params: all (15) });
    await page.waitForTimeout (50);
    await page.keyboard.press ('ArrowUp');
    await page.waitForTimeout (50);
    assert.ok ((await knob()).bands.every (b => b > 15));

    // At the 0.1 s floor, and just above it, a step down never raises it.
    for (const v of [0.1, 0.14])
    {
        await fromPlugin ('state', { params: all (v) });
        await page.waitForTimeout (50);
        for (const key of ['ArrowDown', 'Shift+ArrowDown', 'PageDown', 'Home'])
            await page.keyboard.press (key);
        await page.waitForTimeout (50);
        k = await knob();
        assert.ok (k.bands.every (b => b <= v) && k.now >= k.min, v + ': ' + JSON.stringify (k));
    }

    // A small step up from just above the floor never lowers it either.
    await fromPlugin ('state', { params: all (0.14) });
    await page.waitForTimeout (50);
    await page.keyboard.press ('Shift+ArrowUp');
    await page.waitForTimeout (50);
    assert.ok ((await knob()).bands.every (b => b >= 0.14));
    assert.deepEqual (errors, []);
});

/** Pixels around the focused control whose colour focused and unfocused differ by 3:1 or more. */
async function focusRing()
{
    const box = await page.evaluate (() =>
    {
        const a = document.activeElement;
        if (! a || a === document.body)
            return null;
        const r = a.getBoundingClientRect();
        return { x: r.left, y: r.top, w: r.width, h: r.height, id: a.getAttribute ('data-fid') || [a.getAttribute ('data-act'), a.getAttribute ('data-v'), a.getAttribute ('data-k')].filter (Boolean).join (' ') || a.tagName };
    });

    if (! box)
        return null;

    const m = 6, x = Math.max (0, Math.floor (box.x - m)), y = Math.max (0, Math.floor (box.y - m));
    const clip = { x, y, width: Math.min (1200, Math.ceil (box.x + box.w + m)) - x, height: Math.min (760, Math.ceil (box.y + box.h + m)) - y };

    if (clip.width <= 0 || clip.height <= 0)
        return { ...box, count: 0 };

    const focused = await page.screenshot ({ clip });
    await page.evaluate (() => { window.__ring = document.activeElement; window.__ring.blur(); });
    const plain = await page.screenshot ({ clip });
    await page.evaluate (() => window.__ring.focus ({ preventScroll: true }));

    const count = await page.evaluate (async ([a, b]) =>
    {
        const pixels = async (s) =>
        {
            const bitmap = await createImageBitmap (new Blob ([Uint8Array.from (atob (s), c => c.charCodeAt (0))], { type: 'image/png' }));
            const ctx = new OffscreenCanvas (bitmap.width, bitmap.height).getContext ('2d');
            ctx.drawImage (bitmap, 0, 0);
            return ctx.getImageData (0, 0, bitmap.width, bitmap.height).data;
        };
        const lum = (r, g, b) => [r, g, b].map (v => (v /= 255) <= 0.03928 ? v / 12.92 : Math.pow ((v + 0.055) / 1.055, 2.4)).reduce ((s, v, i) => s + v * [0.2126, 0.7152, 0.0722][i], 0);
        const p = await pixels (a), q = await pixels (b);
        let n = 0;

        for (let i = 0; i < p.length; i += 4)
        {
            const l1 = lum (p[i], p[i + 1], p[i + 2]), l2 = lum (q[i], q[i + 1], q[i + 2]);
            n += (Math.max (l1, l2) + 0.05) / (Math.min (l1, l2) + 0.05) >= 3 ? 1 : 0;
        }

        return n;
    }, [focused.toString ('base64'), plain.toString ('base64')]);

    return { ...box, count };
}

test ('Every control shows where the keyboard is, at 3:1 or more, in every look', async () =>
{
    // Tab through each look, Studio and Stage, and through two windows, measuring the change
    // focus makes around each control: at least its perimeter in pixels must change by 3:1.
    const looks = await page.evaluate (() => SKINS.map (s => s.id));
    const weak = [];

    for (const look of looks)
    {
        for (const where of ['studio', 'stage', ...(look === 'signal' || look === 'catalogue' ? ['capture', 'eq'] : [])])
        {
            await page.goto ('about:blank');
            await open (look);
            await page.evaluate ((w) =>
            {
                if (w === 'stage')
                    st.mode = 'stage';
                else if (w === 'capture')
                    st.capture = true;
                else if (w === 'eq')
                    st.eq = true;
                window.schedule();
            }, where);
            await page.waitForTimeout (50);
            await page.mouse.move (1199, 1);
            const seen = new Set();

            for (let i = 0; i < 80; ++i)
            {
                await page.keyboard.press ('Tab');
                const r = await focusRing();

                if (! r)
                    break;

                const key = r.id + '@' + Math.round (r.x) + ',' + Math.round (r.y);

                if (seen.has (key))
                    break;

                seen.add (key);

                if (r.count < 2 * (r.w + r.h))
                    weak.push (look + ' ' + where + ' ' + r.id + ': ' + r.count + ' of ' + Math.round (2 * (r.w + r.h)));
            }

            assert.ok (seen.size >= (where === 'eq' || where === 'capture' ? 8 : 30), look + ' ' + where + ': only ' + seen.size + ' controls reached');
        }
    }

    assert.deepEqual (weak, []);
    assert.deepEqual (errors, []);
});

test ('Status messages are announced from one region the redraws never touch', async () =>
{
    await page.goto ('about:blank');
    await open ('field');
    const regions = () => page.evaluate (() =>
    {
        const say = document.getElementById ('say'), err = document.getElementById ('say-err');
        return { polite: say.textContent.trim(), alert: err.textContent.trim(), role: say.getAttribute ('role'), live: say.getAttribute ('aria-live'), alertRole: err.getAttribute ('role'),
                 outside: ! document.getElementById ('live').contains (say), same: say.__mark === true };
    });
    await page.evaluate (() => { document.getElementById ('say').__mark = true; });

    // A space chosen: "<space> loaded" is said once, and stays through redraws and meter ticks.
    await page.focus ('[data-act="next"]');
    await page.keyboard.press ('Enter');
    await page.waitForTimeout (50);
    let r = await regions();
    assert.match (r.polite, /loaded/);
    assert.deepEqual ([r.role, r.live, r.alertRole, r.outside], ['status', 'polite', 'alert', true]);
    const said = r.polite;

    for (let i = 0; i < 5; ++i)
    {
        await fromPlugin ('meters', { inL: -10 - i, inR: -12, outL: -8, outR: -9, spk: [-8, -9] });
        await fromPlugin ('state', { params: { size: 100 + i } });
        await page.waitForTimeout (30);
    }

    r = await regions();
    assert.equal (r.polite, said, 'redraws or meters changed what was said');
    assert.ok (r.same, 'the region was replaced');

    // The same message again is said again.
    await page.evaluate (() => { st.note = null; window.schedule(); });
    await page.waitForTimeout (50);
    await page.evaluate ((n) => { st.note = n; window.schedule(); }, await page.evaluate (() => '<b>' + SPACES[st.sp].name + '</b> loaded. Lock a value to hold it when you change space.'));
    await page.waitForTimeout (50);
    assert.notEqual (await page.evaluate (() => document.getElementById ('say').textContent), said);

    // Errors are said assertively: a match that failed, a file that didn't load, a capture that failed.
    await fromPlugin ('matched', { error: 'The recording is too short.' });
    await page.waitForTimeout (50);
    r = await regions();
    assert.match (r.alert, /No match\. The recording is too short\./);
    assert.equal (r.polite, '');

    await fromPlugin ('matched', { listening: true, file: 'Room.wav' });
    await page.waitForTimeout (50);
    r = await regions();
    assert.match (r.polite, /Listening to Room\.wav/);

    await fromPlugin ('state', { ui: { irEvent: 1, irError: '' } });
    await fromPlugin ('state', { ui: { irEvent: 2, irError: 'Not a sound file.' } });
    await page.waitForTimeout (50);
    assert.match ((await regions()).alert, /didn.t load\. Not a sound file\./);

    await page.evaluate (() => { st.capture = true; window.schedule(); });
    await page.waitForTimeout (50);
    await fromPlugin ('capture', { stage: 'failed', progress: 0, peakDb: -100, message: 'No microphone signal.' });
    await page.waitForTimeout (50);
    assert.equal ((await regions()).alert, 'No microphone signal.');
    assert.deepEqual (await page.evaluate (() => [...document.querySelectorAll ('#live [aria-live],#live [role="status"],#live [role="alert"]')].length), 0, 'a live region inside the redrawn page');
    assert.deepEqual (errors, []);
});

test ('Stage shows the output the plugin has: its layout, and a meter per channel', async () =>
{
    await page.goto ('about:blank');
    await open ('anodised');

    const layouts = [
        ['2.0', ['L', 'R'], 'Output, Stereo, 2 channels'],
        ['1.0', ['M'], 'Output, Mono, 1 channel'],
        ['5.1', ['L', 'R', 'C', 'Lfe', 'Ls', 'Rs'], 'Output, 5.1, 6 channels'],
        ['9.1.6', ['L', 'R', 'C', 'Lfe', 'Ls', 'Rs', 'Lss', 'Rss', 'Wl', 'Wr', 'Tfl', 'Tfr', 'Trl', 'Trr', 'Tsl', 'Tsr'], 'Output, 9.1.6, 16 channels'],
    ];

    for (const [layout, names, label] of layouts)
    {
        await fromPlugin ('state', { ui: { mode: 'stage', layout, outputs: names.map (n => ({ name: n, wet: n !== 'Lfe' })) } });
        await page.waitForTimeout (50);
        await fromPlugin ('meters', { inL: -20, inR: -20, outL: -6, outR: -9, spk: names.map ((_, i) => -6 - 3 * i) });
        await page.waitForTimeout (20);

        const shown = await page.evaluate (() =>
        {
            const met = document.querySelector ('.stg-met'), box = met.getBoundingClientRect();
            return { label: met.querySelector ('.lbl').textContent, names: [...met.querySelectorAll ('.mt span')].map (s => s.textContent),
                     levels: [...met.querySelectorAll ('.mt em')].map (e => e.textContent),
                     inside: [...met.querySelectorAll ('.mt')].every (m => { const r = m.getBoundingClientRect(); return r.left >= box.left && r.right <= box.right; }) };
        });

        assert.equal (shown.label, label);
        assert.deepEqual (shown.names, names);
        assert.deepEqual (shown.levels, names.map ((_, i) => (-6 - 3 * i).toFixed (1).replace ('-', '−')));
        assert.ok (shown.inside, layout + ': a meter is outside the panel');
    }

    // Back to Studio: the next test opens this look at the same address, which keeps the view.
    await fromPlugin ('state', { ui: { mode: 'studio', layout: '2.0', outputs: [{ name: 'L', wet: true }, { name: 'R', wet: true }] } });
    await page.waitForTimeout (50);
    assert.deepEqual (errors, []);
});

test ('A space this version doesn\'t have stays the session\'s space', async () =>
{
    // A session saved by a newer Drenched may name a space this one lacks. The plugin plays
    // the first modelled space meanwhile, and the session must keep naming the newer one,
    // or saving it here would lose it. So the page never sends another space in its place.
    await open ('anodised');
    await page.evaluate (() => { window.__sent = []; });
    await fromPlugin ('state', { ui: { spaceId: 'from-a-newer-version' } });
    await page.waitForTimeout (50);
    await fromPlugin ('state', { ui: { locks: 'mix' } });
    await page.click ('[data-act="adv"]');
    await page.waitForTimeout (50);

    const spaces = await page.evaluate (() => window.__sent.filter (([n, p]) => n === 'ui' && p && 'spaceId' in p));
    assert.deepEqual (spaces, []);
    assert.deepEqual (errors, []);
});

test ('Only the interface\'s own scripts run: markup that gets into the page runs nothing', async () =>
{
    // JUCE gives the page the native bridge (clipboard, dropped files, parameters, the licence),
    // so a name or note that ever escaped esc() must not be able to call it. The page's
    // Content-Security-Policy refuses inline handlers and scripts, javascript: links, scripts
    // from anywhere else, frames, forms and requests.
    await open ('anodised');
    const refused = await page.evaluate (async () =>
    {
        const seen = [];
        document.addEventListener ('securitypolicyviolation', e => seen.push (e.violatedDirective));
        window.__ran = 0;
        const box = document.createElement ('div');
        box.innerHTML = '<img src="data:," onerror="window.__ran++">'
                      + '<svg><animate onbegin="window.__ran++" attributeName="x" dur="1s"/></svg>'
                      + '<a id="xss-link" href="javascript:window.__ran++">x</a>';
        document.body.appendChild (box);
        const script = document.createElement ('script');
        script.textContent = 'window.__ran++';
        document.body.appendChild (script);
        const remote = document.createElement ('script');
        remote.src = 'https://example.com/x.js';
        document.body.appendChild (remote);
        document.getElementById ('xss-link').click();
        try { await fetch ('https://example.com/'); } catch (e) {}
        await new Promise (r => setTimeout (r, 300));
        box.remove(); script.remove(); remote.remove();
        return { ran: window.__ran, seen };
    });

    assert.equal (refused.ran, 0);
    for (const directive of ['script-src', 'connect-src'])
        assert.ok (refused.seen.some (d => d.startsWith (directive)), directive + ' refused: ' + refused.seen.join (', '));

    // The page itself still works: its own scripts still answer a click.
    const before = await page.evaluate (() => st.adv);
    await page.click ('[data-act="adv"]');
    await page.waitForFunction (was => st.adv !== was, before);
    assert.deepEqual (errors.filter (e => ! /Content Security Policy/.test (e)), []);
});
