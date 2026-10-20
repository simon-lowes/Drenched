# Drenched: working notes for Claude

- Product: a premium convolution and hybrid reverb rivalling Audio Ease
  Altiverb. One edition with every feature; macOS and Windows; VST3, AU,
  AAX, CLAP; copy protection by a random licence number per copy, checked
  online once, then working offline (no iLok).
- Design docs (Claude Docs): product spec
  https://claude.ai/code/artifact/e55ee3b5-92c6-4d86-99b0-c85cf8f9e462 and
  technical design
  https://claude.ai/code/artifact/76c75f4f-4df0-44f7-899c-445bf8effedc.
- `source/engine/` must stay JUCE-free and real-time safe: no allocation,
  locks or I/O on the audio path. Every engine component gets Catch2 tests,
  and fast convolution paths are null-tested against `DirectConvolver`.
- Licences: only MIT, BSD, zlib, ISC, Apache, BSL or bought licences. Never
  GPL, LGPL (unless dynamically linked and approved), or non-commercial
  code. Record each new dependency in `NOTICE.md`.
- Never reverse-engineer, import or reference Altiverb's IR files or code.
- Style: JUCE-style formatting (Allman braces, a space before call
  parentheses), 4-space indent, `drenched` namespace.
