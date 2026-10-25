# AAX (Pro Tools)

Drenched can build as an AAX plug-in for Pro Tools on 64-bit macOS and
Windows. It is off by default because of licensing, and a retail Pro Tools
only loads it once it is signed.

## Licence first

JUCE ships a copy of Avid's AAX SDK. That SDK is offered under two licences:
GPL v3, which Drenched can never use, or Avid's own AAX SDK licence
agreement, which is free. So before building AAX:

1. Create a developer account at <https://developer.avid.com/aax> and accept
   the AAX SDK licence agreement.
2. Add an "Avid AAX SDK" entry to `NOTICE.md` naming that agreement, and run
   `tools/third_party_licences.py` so the installers list it.

## Building

```sh
cmake -S . -B build -DDRENCHED_BUILD_AAX=ON
cmake --build build --target Drenched_AAX
```

The plug-in lands in `build/source/plugin/Drenched_artefacts/<config>/AAX/`
as `Drenched.aaxplugin`. To build against an SDK downloaded from Avid
instead of JUCE's copy, add `-DDRENCHED_AAX_SDK=/path/to/aax-sdk`. On Linux
and in 32-bit builds the option is ignored.

The same plug-in also appears in Pro Tools' AudioSuite menu for rendering
a clip offline. Select past the end of the clip so the reverb tail has
room to render.

## Signing

An unsigned AAX plug-in loads only in Pro Tools Developer, a free build
Avid gives AAX developers for testing. Retail Pro Tools needs the plug-in
signed with PACE's tools:

1. Ask Avid (through the developer account) for PACE Eden signing tools.
   PACE supplies `wraptool` and a signing account at no cost to AAX
   developers.
2. Sign each build, on the machine that builds it:

   ```sh
   wraptool sign --verbose --account <PACE account> --wcguid <product GUID> \
       --signid <code-signing identity> \
       --in Drenched.aaxplugin --out Drenched.aaxplugin
   ```

   On Windows, `--keyfile` and `--keypassword` take a code-signing
   certificate in place of `--signid`.

Signing needs an iLok for the developer only. Customers do not need one:
the signature only tells Pro Tools the plug-in is genuine, and Drenched's
own licence number still does the copy protection.

## Installers

Once AAX is signed, add it to the installers as its own optional part,
like the other formats: `/Library/Application Support/Avid/Audio/Plug-Ins`
on macOS and `{commoncf64}\Avid\Audio\Plug-Ins` on Windows.
