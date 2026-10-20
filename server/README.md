# Drenched activation server

Issues licence numbers when Paddle reports a sale, gives each licence up to
three computers, and signs the activation codes the plugin checks offline.
It runs on Cloudflare Workers with a D1 database; the only code is in `src/`
and it has no dependencies.

## How a licence works

1. **Sale.** Paddle sends `transaction.completed` to `/v1/paddle`. The server
   makes one licence number per copy bought, looks up the buyer's name and
   email in Paddle, and emails the numbers through Resend. After checkout the
   buyer's browser can open `/thanks?transaction=txn_…` to see them at once.
2. **Activation.** The plugin sends the number and its computer code to
   `/v1/activate` and gets back a signed activation code (Ed25519). It checks
   that code offline from then on, so it never needs the server again.
3. **No internet in the studio.** The buyer opens `/activate` on any other
   device, types the number and the computer code the plugin shows, and pastes
   the code it gives back into the plugin.
4. **New computer.** "Deactivate this computer" calls `/v1/deactivate`, which
   frees the place. Six moves are allowed in any 365 days; after that, support
   resets the licence. (A computer that was deactivated while offline keeps
   its code, which is why moves are limited.)
5. **Refund or chargeback.** Paddle's approved `adjustment` withdraws the
   numbers, so they can't activate new computers. Computers already activated
   keep working, because activation is offline by design.

## API

| Route | Use |
| --- | --- |
| `POST /v1/activate` `{licence, machine}` | `200 {code, name}`; `400 invalid-licence / invalid-machine`, `404 unknown-licence`, `403 revoked`, `409 computer-limit` |
| `POST /v1/deactivate` `{licence, machine}` | `200 {ok}`; `429 too-many-moves` |
| `GET, POST /activate` | The offline activation page |
| `GET /thanks?transaction=txn_…` | The buyer's numbers after checkout |
| `POST /v1/paddle` | Paddle webhooks (signature checked) |
| `POST /v1/admin/licences` `{name, email, orderRef?, note?}` | Make a licence by hand (giveaways, reviewers) |
| `GET /v1/admin/licences/{key}` | A licence, its computers and moves |
| `POST /v1/admin/licences/{key}` `{name?, email?, revoked?, note?}` | Change the owner (transfers) or withdraw it |
| `POST /v1/admin/licences/{key}/reset` | Free all its computers |

Admin routes need `Authorization: Bearer <ADMIN_TOKEN>` and answer 404 without it.

## Develop and test

```sh
npm test                 # Node 22.5 or later; no install step
node scripts/dev.mjs     # local server on :8787 with the test key and one licence
```

Development builds of the plugin talk to `http://127.0.0.1:8787` and carry
the published test key, so a local server is all they need.
`test/vectors.json` is shared with `tests/LicenceTests.cpp`, so the plugin and
the server can't drift apart.

## Deploy

1. `npx wrangler d1 create drenched-licences`, then put its id in `wrangler.toml`
   and run `npx wrangler d1 execute drenched-licences --remote --file schema.sql`.
2. `node scripts/make-keys.mjs --write` makes the signing key and writes its
   public half into the plugin (`source/licence/src/Licence.cpp`). Commit that
   change; keep the private JWK it prints offline and set it with
   `npx wrangler secret put SIGNING_KEY`.
3. Set the other secrets and vars listed in `wrangler.toml`.
4. `npx wrangler deploy`, and point a domain at it.
5. Build release plugins with `-DDRENCHED_LICENCE_SERVER=https://<that domain>`.
6. In Paddle, add a notification destination for `transaction.completed`,
   `adjustment.created` and `adjustment.updated` at `https://<domain>/v1/paddle`.
