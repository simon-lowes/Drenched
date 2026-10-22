# 32-bit and 64-bit

Drenched comes in 64-bit and 32-bit versions, so it runs in old hosts and on
32-bit installs as well as current ones. Every feature is in both. Sessions
open in either: a song saved in a 32-bit host sounds the same in a 64-bit one,
on any operating system.

## Which download

| System | Download | What's in it |
| --- | --- | --- |
| macOS 11 or later | `Drenched-macOS` | VST3, AU, CLAP and app, each for Apple Silicon and Intel |
| Windows 10 or 11, 32 or 64-bit | `Drenched-Windows` | One VST3 holding both versions, a CLAP for each, and both apps |
| Linux, 64-bit | `Drenched-Linux-x64` | VST3, CLAP and app |
| Linux, 32-bit | `Drenched-Linux-i686` | VST3, CLAP and app |

On Windows the host picks the right half of `Drenched.vst3` by itself. Copy it
to the folder your host scans:

- 64-bit hosts: `C:\Program Files\Common Files\VST3`, and
  `CLAP\64-bit\Drenched.clap` to `C:\Program Files\Common Files\CLAP`.
- 32-bit hosts: `C:\Program Files (x86)\Common Files\VST3`, and
  `CLAP\32-bit\Drenched.clap` to `C:\Program Files (x86)\Common Files\CLAP`.

The footer says **Beta · 32-bit** when the 32-bit version is loaded. Please
say which one you used when you report a bug.

## What you need

- A processor with SSE2: any x86 processor from the last twenty years
  (Pentium 4, Athlon 64 or later).
- Windows 10 or 11. The interface runs in Microsoft's WebView2, which comes
  with Windows 10 and 11. Microsoft stopped updating it for Windows 7 and 8.1
  in 2023, so those are untested.
- A host that loads VST3, AU or CLAP plug-ins.

## What 32-bit can't do

- **No 32-bit Mac version.** macOS has not run 32-bit software since
  Catalina (2019). The Mac build covers every Mac that runs macOS 11.
- **No AAX (Pro Tools) in 32-bit.** Pro Tools has been 64-bit only since
  version 11. AAX will be 64-bit when it arrives.
- **No VST2.** Steinberg stopped licensing VST2 in 2018, so no new plug-in can
  offer it. Hosts that only load VST2 can't load Drenched, so check that yours
  loads VST3 or CLAP.
- **Less memory.** A 32-bit host has 2 to 4 GB for everything in it. Most
  Drenched reverbs take 20 to 200 MB; the longest stereo spaces at 192 kHz
  take about 600 MB. Long surround reverbs at high sample rates would need
  several GB, so the 32-bit version shortens them to stay near half a
  gigabyte, and says so in the footer. Stereo and mono are never shortened.
  The longest each layout plays in 32-bit:

  | Layout | 48 kHz | 96 kHz | 192 kHz |
  | --- | --- | --- | --- |
  | 5.1 | full | 27.8 s | 13.9 s |
  | 7.1.4 | 27.8 s | 13.9 s | 6.9 s |
  | 9.1.6 | 20.8 s | 10.4 s | 5.2 s |

  Bundled spaces are at most 25 s and loaded files at most 30 s, so "full"
  means nothing is cut. If a host runs out of memory anyway (many long
  surround reverbs at once), Drenched keeps playing the last reverb that
  fitted instead of crashing.

## Bridging

A bridge runs a plug-in in a helper process, so a 64-bit host can use a
32-bit plug-in or the other way round. With both versions in every download,
you rarely need one: use the version that matches your host, which is faster
and simpler. Bridge only when you must, for example to keep an old 32-bit
session working inside a 64-bit host.

- **REAPER** bridges by itself. Add the folder holding the other version under
  Options, Preferences, Plug-ins, VST, then rescan. To force a plug-in into
  its own process, right-click it in the FX browser and choose **Run as**.
  If the bridged window doesn't appear inside REAPER's FX window, turn off
  embedding for that plug-in in the same menu.
- **Other hosts**: check the host's manual for "bridge" or "sandbox". Many
  bridges only handle VST2 plug-ins, and those can't load Drenched.

Drenched keeps nothing in memory that a bridge would have to share: its
settings travel in the session, and its licence and captures are files in
your user folders, which both versions use. One computer counts once towards
a licence, whichever version activates it.
