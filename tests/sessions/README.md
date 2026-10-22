# Saved sessions

Sessions saved by released versions of Drenched, exactly as a host stores them
(`getStateInformation` bytes). `tests/plugin/SessionTests.cpp` loads each one in the
current build and checks that it comes back with the same settings and plays the same
sound as its `.expected` file records (the space, the latency, and the energy of an
impulse response in octave bands over time, within 0.5 dB).

| Session | Saved by | What it covers |
| --- | --- | --- |
| `beta1-car-park.session` | Beta 1 (the first build sent to testers, PR #10) | A modelled space chosen by index, the old way; most original controls moved |
| `beta1-grand-hall.session` | Beta 1 | Index 0, a reshaped decay graph, Stage mode |
| `v0.1-recorded-living.session` | 0.1 (session version 1) | A recorded library space, living tail, direct sound, cross-feed, Low CPU, a speaker trim |
| `v0.1-modelled-gated.session` | 0.1 (session version 1) | A modelled space by id, Reverse, Gate, direct colour, cross-feed |

The beta 1 sessions were made by building the beta 1 source and saving from it; their
`.expected` sound matched what beta 1 itself rendered to 0.00 dB in every band.

Rules:
- Never edit or delete a session here. Add one from each release that changes what a
  session holds.
- A new parameter's default must be how the plugin played before it existed, because
  older sessions load with it at its default.
- If a change is meant to alter the sound of old sessions, run the plugin tests with
  `DRENCHED_UPDATE_SESSIONS=1` and commit the rewritten `.expected` files with it,
  saying why.
- When a setting moves (session version 2 made the listener's place across the room a
  parameter instead of interface state), `DrenchedProcessor::migrateState()` converts
  older sessions, and `savedValues()` in `tests/plugin/SessionTests.cpp` reads the
  setting from where those sessions kept it.
