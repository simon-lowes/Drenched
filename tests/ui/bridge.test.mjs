// The page's side of the bridge to the plugin: every control the page has is moved, in
// every look, and every message it sends is held against what the plugin takes. A renamed
// parameter id, a value in the wrong units, a misspelt event or data-act would otherwise
// break a control without a sound or a script error.
//
// bridge.json is what the plugin takes: every parameter id with its range, every event the
// plugin window listens for and sends, and the interface keys it keeps. It is made from the
// plugin's own code by tests/plugin/PageMessageTests.cpp, which fails when it is out of date
// (DRENCHED_UPDATE_SESSIONS=1 rewrites it), so these tests run without building the plugin.

import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromium } from 'playwright';

const here = path.dirname (fileURLToPath (import.meta.url));
const page0 = pathToFileURL (path.resolve (here, '../../source/ui/index.html')).href;
const bridge = JSON.parse (fs.readFileSync (path.resolve (here, 'bridge.json'), 'utf8'));
const params = new Map (bridge.parameters.map (p => [p.id, p]));
const toPlugin = new Set (bridge.toPlugin), toPage = new Set (bridge.toPage), uiKeys = new Set (bridge.interfaceKeys);

/** Stands in for JUCE's bridge, as interface.test.mjs does, and also records the events the
    page listens for. What the page sends crosses as JSON, as it does in the plugin. */
function fakePlugin()
{
    const listeners = {};
    window.__sent = [];
    window.__listened = [];
    window.__JUCE__ = { backend: {
        addEventListener: (name, fn) => { window.__listened.push (name); (listeners[name] = listeners[name] || []).push (fn); },
        removeEventListener: () => {},
        emitEvent: (name, payload) => { window.__sent.push ([name, JSON.parse (JSON.stringify (payload === undefined ? null : payload))]); },
    } };
    window.__fromPlugin = (name, payload) => (listeners[name] || []).forEach (fn => fn (payload));
    window.__frame = () => new Promise (r => requestAnimationFrame (() => requestAnimationFrame (r)));

    // What tells one control from another of its kind (see KINDS).
    const a = (e, n) => e.getAttribute (n) || '';
    window.__keyOf = {
        click: e => a (e, 'data-act') + '|' + a (e, 'data-k') + '|' + a (e, 'data-v'),
        knob: e => a (e, 'data-knob'),
        field: e => (a (e, 'data-type') || a (e, 'data-in') || a (e, 'data-f') || (e.hasAttribute ('data-scn') ? 'scene' : e.id)) + '|' + a (e, 'data-i'),
        drag: e => ['data-knob', 'data-decay', 'data-eqn', 'data-drag', 'data-node'].map (n => a (e, n)).join ('|'),
    };
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

const fromPlugin = (name, payload) => page.evaluate (([n, p]) => window.__fromPlugin (n, p), [name, payload]);

/** The page in a look, with scenes, a licence to activate and an output to trim, so every
    control there is can be shown. */
async function open (look)
{
    errors = [];
    await page.goto ('about:blank');
    await page.goto (page0 + '?k=' + look + '#' + look);
    await page.waitForTimeout (50);
    await fromPlugin ('state', { ui: { scenes: [{ number: 1, name: 'Close', space: 'Hall' }, { number: 2, name: 'Wide', space: 'Hall' }],
                                       outputs: [{ name: 'L', wet: true }, { name: 'R', wet: true }, { name: 'C', wet: true }] } });
    await fromPlugin ('licence', { state: 'trial', name: '', licence: '', daysLeft: 9, machine: 'ABC-DEF', page: 'https://example.invalid/activate',
                                   busy: false, message: '', error: false, testKey: false });
    await page.evaluate (() => window.__frame());
}

const looks = async () => { await open ('anodised'); return page.evaluate (() => SKINS.map (s => s.id)); };

/** Everything wrong with what the page has sent: an event the plugin doesn't listen for, a
    parameter it doesn't have, or a value it would refuse or that is outside the range. */
function problems (sent)
{
    const out = [];
    const near = (a, b) => Math.abs (a - b) <= 1e-6 * Math.max (1, Math.abs (b));

    for (const [name, payload] of sent)
    {
        if (! toPlugin.has (name))
        {
            out.push ('an event the plugin doesn\'t listen for: ' + name);
            continue;
        }

        if (name === 'params')
        {
            for (const [id, v] of Object.entries (payload || {}))
            {
                const p = params.get (id);

                if (! p)
                    out.push ('params: no parameter "' + id + '"');
                else if (typeof v !== 'number' || ! Number.isFinite (v))
                    out.push ('params: ' + id + ' is ' + JSON.stringify (v) + ', which the plugin ignores: it takes numbers');
                else if (v < p.min && ! near (v, p.min) || v > p.max && ! near (v, p.max))
                    out.push ('params: ' + id + ' = ' + v + ', outside ' + p.min + ' to ' + p.max);
                else if (p.kind === 'bool' && v !== 0 && v !== 1)
                    out.push ('params: ' + id + ' = ' + v + ', not 0 or 1');
                else if ((p.kind === 'int' || p.kind === 'choice') && ! Number.isInteger (v))
                    out.push ('params: ' + id + ' = ' + v + ', not a whole number');
            }
        }
        else if (name === 'gesture')
        {
            for (const id of (payload && payload.ids) || [])
                if (! params.has (id))
                    out.push ('gesture: no parameter "' + id + '"');

            if (typeof (payload && payload.begin) !== 'boolean')
                out.push ('gesture without begin: ' + JSON.stringify (payload));
        }
        else if (name === 'ui')
        {
            for (const key of Object.keys (payload || {}))
                if (! uiKeys.has (key))
                    out.push ('ui: the plugin doesn\'t keep "' + key + '"');
        }
        else if (name.startsWith ('scene'))
        {
            if (! Number.isInteger (payload.number) || payload.number < 0 || payload.number > params.get ('scene').max || typeof payload.name !== 'string')
                out.push (name + ': ' + JSON.stringify (payload));
        }
    }

    return out;
}

/** What the page shows in turn: each window open, Studio and Stage, All parameters. */
const STATES = [
    {}, { adv: true }, { mode: 'stage' }, { mode: 'stage', adv: true }, { eq: true }, { wf: true }, { scn: true }, { spk: true },
    { capture: true }, { lic: true }, { lic: true, licOff: true }, { credits: true }, { skins: true }, { browse: true },
    { rep: true, repShow: true }, { rep: true, report: { state: 'idle', message: '', crash: '4 Nov 2026, 18:00', details: 'Drenched', steps: '', crashText: 'crashed' } },
];
const CLOSED = { adv: false, mode: 'studio', eq: false, wf: false, scn: false, spk: false, capture: false, lic: false, licOff: false, credits: false, skins: false, browse: false,
                 rep: false, repShow: false, report: { state: 'idle', message: '', crash: false, details: '', steps: '', crashText: '' } };

/** Puts the page in one of STATES, in its look. */
const show = (look, state) => page.evaluate (async ([look, state, closed]) =>
{
    Object.assign (st, closed, state, { skin: look });
    window.schedule();
    await window.__frame();
}, [look, state, CLOSED]);

test ('bridge.json lists the same parameters as the record of every one that has shipped', () =>
{
    // tests/sessions/parameters.txt, which the session tests hold the plugin to.
    const shipped = fs.readFileSync (path.resolve (here, '../sessions/parameters.txt'), 'utf8').split ('\n')
        .map (l => l.replace (/#.*/, '').trim()).filter (Boolean).map (l => l.split (/\s+/));

    assert.deepEqual ([...params.keys()].sort(), shipped.map (f => f[0]).sort());

    for (const [id, def, lo, hi] of shipped)
        assert.deepEqual ([params.get (id).default, params.get (id).min, params.get (id).max].map (v => +v.toPrecision (6)), [+def, +lo, +hi], id);
});

test ('The page listens for every event the plugin sends, and only those', async () =>
{
    await open ('anodised');
    const listened = new Set (await page.evaluate (() => window.__listened));
    assert.deepEqual ([...listened].filter (n => ! toPage.has (n)), [], 'listened for, never sent');
    assert.deepEqual ([...toPage].filter (n => ! listened.has (n)), [], 'sent, never listened for');

    // The first thing a page that has loaded says.
    const sent = await page.evaluate (() => window.__sent.map (([n]) => n));
    assert.equal (sent[0], 'ready');
    assert.deepEqual (problems (await page.evaluate (() => window.__sent)), []);
});

// The click handler's cases in app.js, which a data-act names.
const app = fs.readFileSync (path.resolve (here, '../../source/ui/js/app.js'), 'utf8');
const clickAt = app.indexOf ('live.addEventListener(\'click\'');
const handled = new Set ([...app.slice (app.indexOf ('switch(a){', clickAt), app.indexOf ('\n }\n', clickAt)).matchAll (/case '([^']+)':/g)].map (m => m[1]));

// What each kind of control is, in the page. What tells one from another of its kind is in
// fakePlugin (window.__keyOf), since it runs in the page.
const KINDS = {
    click: '[data-act]',
    knob: '[data-knob]',
    field: 'input',
    drag: '[data-knob],svg[data-decay],[data-eqn],[data-drag],[data-node]',
};

/** The controls of a kind on the page now, shown and usable: index (among all of the kind)
    and what tells it from others. */
const controls = (kind) => page.evaluate (([kind, sel]) =>
{
    return [...document.querySelectorAll ('#live ' + sel.split (',').join (',#live '))]
        .map ((e, i) => ({ i, key: window.__keyOf[kind] (e), shown: e.getBoundingClientRect().width > 0 && ! e.disabled && ! e.readOnly }))
        .filter (c => c.shown);
}, [kind, KINDS[kind]]);

/** The control of a kind at index i, after showing the page in its state again. */
const again = (look, state, kind, i) => page.evaluate (async ([look, state, closed, sel, i]) =>
{
    Object.assign (st, closed, state, { skin: look });
    window.schedule();
    await window.__frame();
    const e = document.querySelectorAll ('#live ' + sel.split (',').join (',#live '))[i];
    const r = e && e.getBoundingClientRect();
    return r && r.width > 0 ? { x: r.left + r.width / 2, y: r.top + r.height / 2 } : null;
}, [look, state, CLOSED, KINDS[kind], i]);

/** Goes through every look and every state, and calls act (look, state, control) for each
    control of the kind the first time it is shown (most behave the same in every look;
    each look's own markup is checked by the test that every control is handled). */
async function eachControl (kind, states, act, perKey = 1)
{
    const done = new Map();

    for (const look of await looks())
    {
        await open (look);

        for (const state of states)
        {
            await show (look, state);

            for (const c of await controls (kind))
            {
                if ((done.get (c.key) || 0) >= perKey)
                    continue;

                done.set (c.key, (done.get (c.key) || 0) + 1);
                await act (look, state, c);
            }
        }

        assert.deepEqual (problems (await page.evaluate (() => window.__sent)), [], look);
        assert.deepEqual (errors, [], look);
    }

    return [...done.keys()];
}

test ('Every control in every look is one the page handles', async () =>
{
    assert.ok (handled.size > 40, 'cases found: ' + [...handled]);
    const unknown = new Set(), seen = new Set();

    for (const look of await looks())
    {
        await open (look);

        for (const state of STATES)
        {
            await show (look, state);
            const where = ' (' + look + ', ' + JSON.stringify (state) + ')';
            const found = await page.evaluate (() => ({
                acts: [...document.querySelectorAll ('#live [data-act]')].map (e => e.getAttribute ('data-act')),
                knobs: [...document.querySelectorAll ('#live [data-knob]')].map (e => e.getAttribute ('data-knob')).filter (k => ! P[k]),
                fields: [...document.querySelectorAll ('#live input[data-type]')].map (e => e.getAttribute ('data-type'))
                    .filter (t => ! P[t] && ! ['band', 'dist'].includes (t)),
            }));

            for (const a of found.acts)
            {
                seen.add (a);

                if (! handled.has (a))
                    unknown.add ('data-act ' + a + where);
            }

            for (const k of found.knobs)
                unknown.add ('data-knob ' + k + where);

            for (const t of found.fields)
                unknown.add ('data-type ' + t + where);
        }

        assert.deepEqual (errors, [], look);
    }

    assert.deepEqual ([...unknown], [], 'controls the page doesn\'t handle');
    assert.ok (seen.size > 30, 'controls found: ' + [...seen]);
});

test ('Every control clicked sends only what the plugin takes', async () =>
{
    const names = new Set(), heard = new Set();
    const clicked = await eachControl ('click', STATES, async (look, state, c) =>
    {
        if (! await again (look, state, 'click', c.i))
            return;

        // The click handler sends what a click changes before it returns.
        const click = await page.evaluate ((i) =>
        {
            const e = document.querySelectorAll ('#live [data-act]')[i], mark = window.__sent.length;
            e.dispatchEvent (new MouseEvent ('click', { bubbles: true, cancelable: true })); // SVG controls have no click()
            return { act: e.getAttribute ('data-act'), sent: window.__sent.slice (mark).map (([n]) => n) };
        }, c.i);

        names.add (click.act);
        click.sent.forEach (n => heard.add (n));
    }, 3);

    assert.ok (names.size > 40, 'clicked: ' + [...names]);
    assert.ok (clicked.length > 60, 'controls: ' + clicked.length);

    // The plugin heard from each of the buttons that are for it.
    const wanted = ['params', 'ui', 'match', 'loadIr', 'copyReport', 'sceneStore', 'sceneRecall', 'sceneDelete', 'startCapture',
                    'licenceOpen', 'licenceActivate', 'licenceCode', 'licencePage', 'copyText', 'pasteText', 'displays',
                    'reportOpen', 'reportSend', 'reportDismissCrash'];
    assert.deepEqual (wanted.filter (n => ! heard.has (n)), [], 'never sent; heard: ' + [...heard]);
});

test ('Every knob, at either end, sends its parameter\'s own end, in its own units', async () =>
{
    // The page sends values in the parameter's units (Mix 0 to 100, High cut 1000 to 20000 Hz),
    // not the host's 0 to 1: a knob at its top must send the top of its parameter's range.
    const near = (v, end) => Math.abs (v - end) <= 1e-6 * Math.max (1, Math.abs (end));
    const at = (sent, end) => Object.entries (sent).some (([id, v]) => params.has (id) && near (v, params.get (id)[end]));

    const knobs = await eachControl ('knob', [{}, { adv: true }, { mode: 'stage' }, { mode: 'stage', adv: true }, { eq: true }], async (look, state, c) =>
    {
        const turned = await page.evaluate (async (i) =>
        {
            const out = [];

            for (const key of ['Home', 'End', 'Home'])
            {
                const e = document.querySelectorAll ('#live [data-knob]')[i];
                e.focus ({ preventScroll: true });
                const mark = window.__sent.length;
                e.dispatchEvent (new KeyboardEvent ('keydown', { key, bubbles: true, cancelable: true }));
                await window.__frame();
                out.push (Object.assign ({}, ...window.__sent.slice (mark).filter (([n]) => n === 'params').map (([, p]) => p)));
            }

            return out;
        }, c.i);

        const [, top, bottom] = turned, where = look + ' ' + JSON.stringify (state) + ' knob ' + c.key;
        assert.ok (at (top, 'max'), where + ': End sent ' + JSON.stringify (top) + ', none at its parameter\'s top');
        assert.ok (at (bottom, 'min'), where + ': Home sent ' + JSON.stringify (bottom) + ', none at its parameter\'s bottom');
    });

    for (const k of ['mix', 'decay', 'pre', 'size', 'bright', 'el', 'dist'])
        assert.ok (knobs.includes (k), 'knobs turned: ' + knobs);
});

test ('Every field, given any value, sends only values in range, and its ends in its own units', async () =>
{
    const near = (v, end) => Math.abs (v - end) <= 1e-6 * Math.max (1, Math.abs (end));
    const at = (sent, end) => Object.entries (sent).some (([id, v]) => params.has (id) && near (v, params.get (id)[end]));
    let ends = 0;

    const fields = await eachControl ('field', [{ adv: true }, { mode: 'stage', adv: true }, { spk: true }, { scn: true }, { lic: true, licOff: true }], async (look, state, c) =>
    {
        // A field takes what was typed when it changes, and redraws there and then.
        const typed = await page.evaluate ((i) =>
        {
            const out = [];
            const type = (text) =>
            {
                const e = document.querySelectorAll ('#live input')[i];
                const mark = window.__sent.length;
                e.focus ({ preventScroll: true });
                e.value = text;
                e.dispatchEvent (new Event ('input', { bubbles: true }));
                e.dispatchEvent (new Event ('change', { bubbles: true }));
                out.push (Object.assign ({}, ...window.__sent.slice (mark).filter (([n]) => n === 'params').map (([, p]) => p)));
            };

            const isValue = document.querySelectorAll ('#live input')[i].hasAttribute ('data-type');

            for (const text of ['-999999', '999999', '-999999', '0', 'off', '12k', '1.5 s', '50 left', 'centre', 'x', ''])
                type (text);

            return { isValue, out };
        }, c.i);

        if (! typed.isValue)
            return;

        // A value typed past either end is that end, sent in the parameter's own units.
        const [, top, bottom] = typed.out, where = look + ' ' + JSON.stringify (state) + ' field ' + c.key;
        assert.ok (at (top, 'max'), where + ': a huge number sent ' + JSON.stringify (top) + ', none at its parameter\'s top');
        assert.ok (at (bottom, 'min'), where + ': a huge negative number sent ' + JSON.stringify (bottom) + ', none at its parameter\'s bottom');
        ++ends;
    });

    assert.ok (fields.length >= 25, 'fields typed in: ' + fields);
    assert.ok (ends >= 25, 'value fields: ' + ends);
});

test ('Every drag, as far as it goes either way, sends only values in range', async () =>
{
    const took = new Set();
    const drags = await eachControl ('drag', [{}, { adv: true }, { mode: 'stage' }, { eq: true }], async (look, state, c) =>
    {
        const mark = await page.evaluate (() => window.__sent.length);

        for (const [dx, dy] of [[0, -2000], [0, 2000], [2000, 0], [-2000, 0]])
        {
            const at = await again (look, state, 'drag', c.i);

            if (! at)
                return;

            await page.mouse.move (at.x, at.y);
            await page.mouse.down();
            await page.mouse.move (at.x + dx / 4, at.y + dy / 4, { steps: 2 });
            await page.mouse.move (at.x + dx, at.y + dy, { steps: 2 });
            await page.mouse.up();
        }

        if (await page.evaluate ((mark) => window.__sent.slice (mark).some (([n, p]) => n === 'gesture' && p.begin), mark))
            took.add (c.key);
    });

    assert.ok (drags.length >= 20, 'controls dragged: ' + drags);
    assert.ok (took.size > 10, 'drags that took a control: ' + [...took]);
});

test ('The checks here catch what they are for', () =>
{
    // A test that never fails proves nothing: each mistake these tests look for is found.
    assert.deepEqual (problems ([['params', { mix: 50, size: 100, freeze: 1, scene: 3 }], ['ui', { skin: 'aperture' }], ['sceneRecall', { number: 1, name: '' }]]), []);
    assert.equal (problems ([['sceneRecal', { number: 1, name: '' }]]).length, 1, 'a misspelt event');
    assert.equal (problems ([['params', { Mix: 50 }]]).length, 1, 'a renamed id');
    assert.equal (problems ([['params', { size: 0.5 }]]).length, 1, 'a value in the host\'s 0 to 1');
    assert.equal (problems ([['params', { wetHighCut: 25000 }]]).length, 1, 'past the top');
    assert.equal (problems ([['params', { reverse: true }]]).length, 1, 'true, which the plugin ignores');
    assert.equal (problems ([['params', { mix: null }]]).length, 1, 'not a number');
    assert.equal (problems ([['params', { freeze: 0.5 }]]).length, 1, 'a switch half on');
    assert.equal (problems ([['params', { scene: 1.5 }]]).length, 1, 'half a scene');
    assert.equal (problems ([['gesture', { ids: ['decay1K'], begin: true }]]).length, 1, 'a drag of an id there isn\'t');
    assert.equal (problems ([['ui', { width: 900 }]]).length, 1, 'an interface key the plugin doesn\'t keep');
});
