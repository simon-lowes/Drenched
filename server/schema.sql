-- Drenched activation server: Cloudflare D1 (SQLite).
-- Apply with: npx wrangler d1 execute drenched-licences --remote --file schema.sql

CREATE TABLE IF NOT EXISTS licences (
    key       TEXT PRIMARY KEY,           -- XXXXX-XXXXX-XXXXX-XXXXX-XXXXX
    name      TEXT NOT NULL,              -- shown in the plugin as "Licensed to"
    email     TEXT NOT NULL,
    created   TEXT NOT NULL,              -- YYYY-MM-DD
    revoked   INTEGER NOT NULL DEFAULT 0, -- refunds and chargebacks
    order_ref TEXT,                       -- the shop's transaction
    copy      INTEGER,                    -- which copy of the order, from 0
    emailed   TEXT,                       -- when the number was emailed to the buyer
    note      TEXT
);

-- The computers holding a licence, at most three each.
CREATE TABLE IF NOT EXISTS activations (
    key       TEXT NOT NULL REFERENCES licences (key),
    machine   TEXT NOT NULL,              -- XXXXX-XXXXX-XXXXX-XXXXX
    activated TEXT NOT NULL,
    PRIMARY KEY (key, machine)
);

-- Computers released to make room for another, limited per year (see README).
CREATE TABLE IF NOT EXISTS moves (
    key     TEXT NOT NULL REFERENCES licences (key),
    machine TEXT NOT NULL,
    moved   TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS moves_by_key ON moves (key, moved);
CREATE INDEX IF NOT EXISTS licences_by_order ON licences (order_ref);

-- Paddle can deliver one sale twice at once; this keeps it to one licence per copy.
CREATE UNIQUE INDEX IF NOT EXISTS licences_by_order_copy ON licences (order_ref, copy);

-- A refund or chargeback can arrive before its sale does (Paddle retries a sale the
-- server failed to take, for about two days). Each order's standing adjustment is
-- kept here so the sale, when it comes, issues its numbers already withdrawn.
CREATE TABLE IF NOT EXISTS order_adjustments (
    order_ref TEXT PRIMARY KEY,           -- the shop's transaction
    outcome   TEXT NOT NULL,              -- 'withdrawn' (full refund, chargeback) or 'flagged' (partial refund)
    note      TEXT NOT NULL               -- copied to the order's licences, e.g. "refund adj_..."
);
