// The offline activation page: for a studio computer with no internet, the
// buyer opens this on any other device, types the licence number and the code
// the plugin shows, and carries the activation code back.

const escape = (s) => String (s ?? '').replace (/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

const style = `
:root{--bg:#f6f5f2;--card:#fff;--tx:#1d1c1a;--tx2:#5f5c56;--ln:#d9d6cf;--ac:#1f5f8b;--err:#a3361f}
@media (prefers-color-scheme:dark){:root{--bg:#121417;--card:#1b1e22;--tx:#ecebe7;--tx2:#a5a29b;--ln:#33373d;--ac:#7fb6dd;--err:#f08a6f}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:16px/1.55 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:560px;margin:0 auto;padding:48px 16px}
h1{font-size:26px;font-weight:600;margin:0 0 8px;letter-spacing:-.01em}
p{color:var(--tx2);margin:0 0 16px}
form,.out{background:var(--card);border:1px solid var(--ln);border-radius:10px;padding:20px}
label{display:block;font-weight:500;margin:0 0 4px}
label small{display:block;font-weight:400;color:var(--tx2);font-size:14px}
input,textarea{width:100%;font:16px ui-monospace,"SF Mono",Consolas,monospace;padding:10px 12px;border:1px solid var(--ln);border-radius:6px;background:transparent;color:var(--tx);margin:6px 0 16px}
textarea{height:132px;resize:vertical;word-break:break-all}
button{font:inherit;font-weight:600;border:0;border-radius:6px;padding:10px 18px;background:var(--ac);color:var(--card);cursor:pointer}
button:focus-visible,input:focus-visible,textarea:focus-visible{outline:2px solid var(--ac);outline-offset:2px}
.err{color:var(--err);margin:0 0 16px}
.out{margin-bottom:20px}
.key{font:600 22px/1.4 ui-monospace,"SF Mono",Consolas,monospace;letter-spacing:.04em;color:var(--tx);margin:4px 0}
a{color:var(--ac)}
`;

function page (title, body)
{
    return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>${escape (title)}</title><style>${style}</style></head><body><main>${body}</main></body></html>`;
}

export function activationPage ({ licence = '', machine = '', error = null, code = null, name = '' } = {})
{
    const result = code
        ? `<div class="out" role="status"><label for="code">Activation code<small>Copy it to the computer with Drenched and paste it under "Activate without internet".</small></label>
<textarea id="code" readonly>${escape (code)}</textarea>
<button type="button" onclick="var t=document.getElementById('code');t.select();navigator.clipboard&&navigator.clipboard.writeText(t.value);this.textContent='Copied'">Copy code</button></div>
<p>${name ? 'Licensed to ' + escape (name) + '. ' : ''}This computer now holds one of the licence's three places.</p>`
        : '';

    return page ('Activate Drenched', `<h1>Activate Drenched without internet</h1>
<p>For a computer that cannot go online. Enter your licence number and the computer code Drenched shows, and you get an activation code to take back.</p>
${result}
<form method="post" action="/activate">
${error ? `<p class="err" role="alert">${escape (error)}</p>` : ''}
<label for="licence">Licence number<small>From your purchase email, for example 7Q2KD-M9XRT-0BW4H-NC8VZ-3JPA6.</small></label>
<input id="licence" name="licence" autocomplete="off" autocapitalize="characters" spellcheck="false" required value="${escape (licence)}">
<label for="machine">Computer code<small>In Drenched, open Licence, then "Activate without internet".</small></label>
<input id="machine" name="machine" autocomplete="off" autocapitalize="characters" spellcheck="false" required value="${escape (machine)}">
<button type="submit">Get activation code</button>
</form>`);
}

/** After checkout: the buyer's licence numbers, or a short wait while the sale is recorded. */
export function thanksPage ({ keys = [], transaction = '' } = {})
{
    if (keys.length === 0)
    {
        return page ('Thank you for buying Drenched', `<meta http-equiv="refresh" content="4">
<h1>Thank you for buying Drenched</h1>
<p role="status">Your licence number is being made. This page shows it in a moment, and it is on its way to your email too.</p>`);
    }

    const many = keys.length > 1;

    return page ('Your Drenched licence', `<h1>Thank you for buying Drenched</h1>
<p>Your licence number${many ? 's' : ''}, also sent to your email:</p>
<div class="out">${keys.map ((k) => `<p class="key">${escape (k)}</p>`).join ('')}</div>
<p>In Drenched, click the trial notice at the bottom of the window (or choose Licence in the space menu) and enter ${many ? 'a number' : 'it'}. Each licence works on up to three computers, and keeps working offline after activating once.</p>
<p>No internet on the studio computer? Use <a href="/activate">offline activation</a>.</p>`);
}
