# Saved sessions

Sessions saved by released versions of Drenched, exactly as a host stores them
(`getStateInformation` bytes). `tests/plugin/SessionTests.cpp` loads each one in the
current build and checks that it comes back with the same settings and plays the same
sound as its `.expected` file records.

What an `.expected` file records:
- `space`, `latency` (samples) and `tail` (seconds, what the host is told; within 10 ms).
- `energy`: the first fingerprint, of an impulse into both inputs at once. One line per
  output (left, then right) and octave band (125 Hz to 8 kHz), one value per time
  window (0, 50 ms, 0.2, 0.5, 1 and 1.5 s), in dB.
- `path <input> <output> <band>`: each input played on its own, so each of the four
  paths (left input to left output, left to right, right to left, right to right) is
  checked by itself; with both inputs at once two paths swapped, a broken cross-feed or
  one path upside down can sound the same. Octave bands from 63 Hz to 16 kHz, windows
  at 0, 50 ms, 0.2, 0.5, 1, 1.5, 2, 3, 4 and 6 s, past the longest decay a session here
  has (2.9 s at 63 Hz). Bands are filtered in double precision.
- `wave <input> <output> <from ms>`: the waveform of that path, counted from the
  latency on, in three stretches: the first 50 ms, 50 to 200 ms and 200 to 500 ms (where
  the pre-delay is long, as in `v0.2-everything`, the first 50 ms holds only the dry
  sound or nothing). Each stretch is sketched as sixteen sums of its samples, each sum
  with them signed by a fixed random pattern, so the sign of the waveform shows, which
  energy can't. It must match to within -20 dB of the stretch's level: a path upside
  down is +6 dB off, another path in its place about +3 dB.

Every energy must be within 0.5 dB of what it was, in every band and window. Rounding
differs between platforms and processors in the last digits, which shows in windows
quieter than about -110 dB, so these are not skipped but compared as heard over -110 dB
of noise: both values have -110 dB added (as powers) before they are compared. A loud
window compares exactly as before, two windows far below -110 dB compare as equal, and a
window that should be silent but sounds at -100 dB (or the other way round) still fails.

| Session | Saved by | What it covers |
| --- | --- | --- |
| `beta1-car-park.session` | Beta 1 (the first build sent to testers, PR #10) | A modelled space chosen by index, the old way; most original controls moved |
| `beta1-grand-hall.session` | Beta 1 | Index 0, a reshaped decay graph, Stage mode |
| `v0.1-recorded-living.session` | 0.1 (session version 1) | A recorded library space, living tail, direct sound, cross-feed, Low CPU, a speaker trim |
| `v0.1-modelled-gated.session` | 0.1 (session version 1) | A modelled space by id, Reverse, Gate, direct colour, cross-feed |
| `v0.2-everything.session` | 0.2 before release (session version 2) | Every parameter off its default but Freeze (which would leave the impulse dry), the right decays unlinked, the wet EQ, cloud size, listener and source positions, playing a file of the person's own (`v0.2-everything.wav`) with speaker trims and locks; three scenes: a modelled space, a library space and that file, scene 3 playing and changed since |

The beta 1 sessions were made by building the beta 1 source and saving from it; their
`.expected` sound matched what beta 1 itself rendered to 0.00 dB in every band. The 0.2
session was saved from main at `02f0639`, the build that first wrote session version 2
in full (scenes, the wet EQ, unlinked decays), before 0.2 was released.

A session that plays a file of the person's own names it by the path it had on the
computer that saved it. That file is kept here next to the session, and the tests point
the session at it here, by its name, as a person who copied the file over would find it.
Scenes in a session are recalled too, and must bring back what they stored.

Rules:
- Never edit or delete a session here. Add one from each release that changes what a
  session holds.
- A new parameter's default must be how the plugin played before it existed, because
  older sessions load with it at its default.
- `parameters.txt` lists every parameter that has shipped, in the order hosts see them,
  with its default, its range, and its value at a quarter, half and three quarters of
  the host's 0 to 1 (what automation is written in). Never remove or rename one, or
  change its default or range: sessions, scenes and automation name parameters by id,
  and a session that doesn't mention one plays it at its default. The test fails for a
  parameter missing from the list too, so a new one is added with
  `DRENCHED_UPDATE_SESSIONS=1` only once its default is how the plugin played before it;
  that update is the sign-off, and the commit says so.
- `spaces.txt` lists every space that has shipped, which sessions and scenes name by
  id. Never remove, reorder or rename one. A new space is added to it, and a recording
  replaced on purpose gets a new fingerprint, both with `DRENCHED_UPDATE_SESSIONS=1`.
- If a change is meant to alter the sound of old sessions, run the plugin tests with
  `DRENCHED_UPDATE_SESSIONS=1` and commit the rewritten `.expected` files with it,
  saying why. To rewrite only the `.expected` files, run just the sound test:
  `DRENCHED_UPDATE_SESSIONS=1 drenched_plugin_tests "[sound]"`.
- When the fingerprint itself gains a check, write the new `.expected` files from a
  build whose sound is unchanged, in the same commit, and check that the lines already
  there came out the same. The `tail`, `path` and `wave` lines were added this way
  (audit H3, November 2026); the `energy` lines came out the same but for one value
  that moved by 0.01 dB in its last digit (`beta1-grand-hall`, right, 500 Hz, 0.2 to
  0.5 s).
- When a setting moves (session version 2 made the listener's place across the room a
  parameter instead of interface state), `DrenchedProcessor::migrateState()` converts
  older sessions, and `savedValues()` in `tests/plugin/SessionTests.cpp` reads the
  setting from where those sessions kept it.
