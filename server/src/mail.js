// The licence email, sent through Resend (https://resend.com/docs/api-reference/emails/send-email).

const escape = (s) => String (s ?? '').replace (/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

export function licenceEmail ({ name, keys })
{
    const many = keys.length > 1;
    const greeting = name ? `Hello ${name},` : 'Hello,';
    const numbers = keys.join ('\n');

    const text = `${greeting}

Thank you for buying Drenched. Your licence number${many ? 's' : ''}:

${numbers}

In Drenched, click the trial notice at the bottom of the window (or choose Licence in the space menu) and enter ${many ? 'a number' : 'it'}. Each licence works on up to three computers, and after activating once it keeps working offline.

For a studio computer with no internet, choose "Activate without internet" in the same window.

Keep this email: you need the number to move Drenched to a new computer.
`;

    const html = `<p>${escape (greeting)}</p>
<p>Thank you for buying Drenched. Your licence number${many ? 's' : ''}:</p>
${keys.map ((k) => `<p style="font:600 20px/1.4 ui-monospace,Menlo,Consolas,monospace;letter-spacing:.04em">${escape (k)}</p>`).join ('\n')}
<p>In Drenched, click the trial notice at the bottom of the window (or choose Licence in the space menu) and enter ${many ? 'a number' : 'it'}. Each licence works on up to three computers, and after activating once it keeps working offline.</p>
<p>For a studio computer with no internet, choose "Activate without internet" in the same window.</p>
<p>Keep this email: you need the number to move Drenched to a new computer.</p>`;

    return { subject: many ? 'Your Drenched licence numbers' : 'Your Drenched licence number', text, html };
}

export function resendMailer ({ apiKey, from, fetch = globalThis.fetch })
{
    return async function send ({ to, subject, text, html })
    {
        const response = await fetch ('https://api.resend.com/emails', {
            method: 'POST',
            headers: { authorization: `Bearer ${apiKey}`, 'content-type': 'application/json' },
            body: JSON.stringify ({ from, to: [to], subject, text, html }),
        });

        if (! response.ok)
            throw new Error (`Sending the licence email failed: ${response.status} ${await response.text()}`);
    };
}
