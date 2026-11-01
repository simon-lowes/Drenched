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
| `v0.2-everything.session` | 0.2 before release (session version 2) | Every parameter off its default but Freeze (which would leave the impulse dry), the right decays unlinked, the wet EQ, cloud size, listener and source positions, playing a file of the person's own (`v0.2-everything.wav`) with speaker trims and locks; three scenes: a modelled space, a library space and that file, scene 3 playing and changed since |
| `v0.2-file-level.session` | 0.2 before release (session version 2, with `loadedLevel`) | The first session to say a loaded response plays at the level it has at its own rate (`loadedLevel="fileRate"`): a 96 kHz file of the person's own (`v0.2-file-level.wav`), heard here at 48 kHz, where the sessions before it play 3 dB quieter; Size, early/late balance, mix and pre-delay moved |

The beta 1 sessions were made by building the beta 1 source and saving from it; their
`.expected` sound matched what beta 1 itself rendered to 0.00 dB in every band. The 0.2
session was saved from main at `02f0639`, the build that first wrote session version 2
in full (scenes, the wet EQ, unlinked decays), before 0.2 was released. The file-level
session was saved by the build that first wrote `loadedLevel` (branch
`claude/loaded-room-level`), before 0.2 was released.

Sessions without `loadedLevel` (every one before it) play a loaded file or library
recording at unit energy at the session's rate, which differs from its level at its own
rate whenever the rates differ: louder above it (a 44.1 kHz file 3.4 dB at 96 kHz), and
quieter below it by about as much as the file has little above the lower Nyquist (a
96 kHz room recording about 3 dB at 48 kHz); loaded and saved again they keep that, and
`tests/plugin/SessionTests.cpp` checks both rules.

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
  saying why.
- When a setting moves (session version 2 made the listener's place across the room a
  parameter instead of interface state), `DrenchedProcessor::migrateState()` converts
  older sessions, and `savedValues()` in `tests/plugin/SessionTests.cpp` reads the
  setting from where those sessions kept it.
