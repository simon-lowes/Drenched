// Where licences and activations are kept: Cloudflare D1 in production, memory
// in tests. Both give the same answers; test/d1.test.js runs the D1 one on
// SQLite to prove its SQL.
//
// Dates are YYYY-MM-DD strings, so they compare as text.

export function d1Store (db)
{
    const one = (sql, ...args) => db.prepare (sql).bind (...args).first();
    const all = async (sql, ...args) => (await db.prepare (sql).bind (...args).all()).results;
    const run = async (sql, ...args) => (await db.prepare (sql).bind (...args).run()).meta.changes;

    const fromRow = (row) => row && {
        key: row.key, name: row.name, email: row.email, created: row.created,
        revoked: !! row.revoked, orderRef: row.order_ref ?? null, emailed: row.emailed ?? null, note: row.note ?? null,
    };

    return {
        async getLicence (key)
        {
            return fromRow (await one ('SELECT * FROM licences WHERE key = ?1', key));
        },

        async createLicence ({ key, name, email, created, orderRef = null, note = null })
        {
            return await run ('INSERT OR IGNORE INTO licences (key, name, email, created, revoked, order_ref, note) VALUES (?1, ?2, ?3, ?4, 0, ?5, ?6)',
                              key, name, email, created, orderRef, note) === 1;
        },

        async updateLicence (key, { name, email, revoked, note })
        {
            const current = await this.getLicence (key);

            if (! current)
                return false;

            await run ('UPDATE licences SET name = ?2, email = ?3, revoked = ?4, note = ?5 WHERE key = ?1', key,
                       name ?? current.name, email ?? current.email, (revoked ?? current.revoked) ? 1 : 0, note ?? current.note);
            return true;
        },

        async listByOrder (orderRef)
        {
            return (await all ('SELECT * FROM licences WHERE order_ref = ?1 ORDER BY created, key', orderRef)).map (fromRow);
        },

        async markEmailed (key, date)
        {
            await run ('UPDATE licences SET emailed = ?2 WHERE key = ?1', key, date);
        },

        async listActivations (key)
        {
            return all ('SELECT machine, activated FROM activations WHERE key = ?1 ORDER BY activated, machine', key);
        },

        /** 'existing' if the machine already holds a place, 'added' if it took a free one, 'full' if none is free. */
        async activate (key, machine, today, limit)
        {
            if (await one ('SELECT 1 AS found FROM activations WHERE key = ?1 AND machine = ?2', key, machine))
                return 'existing';

            // One statement, so two computers activating at once cannot both take the last place.
            const added = await run ('INSERT OR IGNORE INTO activations (key, machine, activated) SELECT ?1, ?2, ?3 WHERE (SELECT COUNT(*) FROM activations WHERE key = ?1) < ?4',
                                     key, machine, today, limit);
            return added === 1 ? 'added' : (await one ('SELECT 1 AS found FROM activations WHERE key = ?1 AND machine = ?2', key, machine) ? 'existing' : 'full');
        },

        async deactivate (key, machine, today)
        {
            if (await run ('DELETE FROM activations WHERE key = ?1 AND machine = ?2', key, machine) === 0)
                return 'absent';

            await run ('INSERT INTO moves (key, machine, moved) VALUES (?1, ?2, ?3)', key, machine, today);
            return 'removed';
        },

        async countMoves (key, since)
        {
            return (await one ('SELECT COUNT(*) AS n FROM moves WHERE key = ?1 AND moved >= ?2', key, since)).n;
        },

        async resetActivations (key)
        {
            await run ('DELETE FROM activations WHERE key = ?1', key);
            await run ('DELETE FROM moves WHERE key = ?1', key);
        },
    };
}

export function memoryStore()
{
    const licences = new Map();
    const activations = new Map(); // key -> Map(machine -> activated)
    const moves = [];

    const placesOf = (key) => activations.get (key) ?? activations.set (key, new Map()).get (key);

    return {
        async getLicence (key)
        {
            const l = licences.get (key);

            if (! l)
                return null;

            const { order, ...copy } = l;
            return copy;
        },

        async listByOrder (orderRef)
        {
            return [...licences.values()].filter ((l) => l.orderRef === orderRef).sort ((a, b) => a.order - b.order)
                                         .map (({ order, ...copy }) => copy);
        },

        async markEmailed (key, date)
        {
            const l = licences.get (key);

            if (l)
                l.emailed = date;
        },

        async createLicence ({ key, name, email, created, orderRef = null, note = null })
        {
            if (licences.has (key))
                return false;

            licences.set (key, { key, name, email, created, revoked: false, orderRef, emailed: null, note, order: licences.size });
            return true;
        },

        async updateLicence (key, fields)
        {
            const l = licences.get (key);

            if (! l)
                return false;

            for (const f of ['name', 'email', 'revoked', 'note'])
                if (fields[f] !== undefined)
                    l[f] = f === 'revoked' ? !! fields[f] : fields[f];

            return true;
        },

        async listActivations (key)
        {
            return [...placesOf (key)].map (([machine, activated]) => ({ machine, activated }))
                                      .sort ((a, b) => a.activated.localeCompare (b.activated) || a.machine.localeCompare (b.machine));
        },

        async activate (key, machine, today, limit)
        {
            const places = placesOf (key);

            if (places.has (machine))
                return 'existing';

            if (places.size >= limit)
                return 'full';

            places.set (machine, today);
            return 'added';
        },

        async deactivate (key, machine, today)
        {
            if (! placesOf (key).delete (machine))
                return 'absent';

            moves.push ({ key, machine, moved: today });
            return 'removed';
        },

        async countMoves (key, since)
        {
            return moves.filter ((m) => m.key === key && m.moved >= since).length;
        },

        async resetActivations (key)
        {
            placesOf (key).clear();

            for (let i = moves.length - 1; i >= 0; --i)
                if (moves[i].key === key)
                    moves.splice (i, 1);
        },
    };
}
