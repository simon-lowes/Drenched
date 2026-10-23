; The Windows installer for the friends beta, built in CI by Inno Setup 6 from the
; merged Windows download (see .github/workflows/build.yml, windows-installer).
;
;   iscc /DAppVersion=0.1.0 /DSource=<folder of the merged download> /O<output folder> Drenched.iss
;
; The source folder holds the merged VST3 bundle (32 and 64-bit inside), CLAP\64-bit and
; CLAP\32-bit, Drenched.exe and "Drenched (32-bit).exe". On 64-bit Windows both sets go in,
; so 64-bit hosts and 32-bit hosts (or a bridge) each find a plug-in; on 32-bit Windows only
; the 32-bit set does.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Source
  #define Source "dist"
#endif

[Setup]
AppId={{6B0F3C2E-5C1D-4F7A-9E2B-D7A1C3E5F801}
AppName=Drenched
AppVersion={#AppVersion}
AppVerName=Drenched {#AppVersion} beta
AppPublisher=Drenched
DefaultDirName={autopf}\Drenched
DefaultGroupName=Drenched
DisableProgramGroupPage=yes
OutputBaseFilename=Drenched-{#AppVersion}-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x86compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName=Drenched {#AppVersion}
UninstallDisplayIcon={app}\Drenched.exe
; Several components' licences ask for their notices in documentation shipped with the
; binaries; the licence page shows them all (tools/third_party_licences.py builds the file).
LicenseFile=..\third-party-licences.txt

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Cubase, Studio One, Ableton Live, Reaper and most others)"; Types: full custom
Name: "clap"; Description: "CLAP plug-in (Bitwig, Reaper and others)"; Types: full custom
Name: "app"; Description: "Standalone app"; Types: full custom

[Files]
; VST3: one bundle holding both builds; the 32-bit Common Files folder only exists on 64-bit Windows.
Source: "{#Source}\Drenched.vst3\*"; DestDir: "{commoncf64}\VST3\Drenched.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs; Check: Is64BitInstallMode
Source: "{#Source}\Drenched.vst3\*"; DestDir: "{commoncf32}\VST3\Drenched.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; CLAP: each folder gets the build its hosts can load.
Source: "{#Source}\CLAP\64-bit\Drenched.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#Source}\CLAP\32-bit\Drenched.clap"; DestDir: "{commoncf32}\CLAP"; Components: clap; Flags: ignoreversion

; The standalone app: 64-bit where Windows is, otherwise 32-bit.
Source: "{#Source}\Drenched.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion; Check: Is64BitInstallMode
Source: "{#Source}\Drenched (32-bit).exe"; DestDir: "{app}"; DestName: "Drenched.exe"; Components: app; Flags: ignoreversion; Check: not Is64BitInstallMode

Source: "..\third-party-licences.txt"; DestDir: "{app}"; DestName: "Third-party licences.txt"; Flags: ignoreversion

[Icons]
Name: "{group}\Drenched"; Filename: "{app}\Drenched.exe"; Components: app
Name: "{group}\Third-party licences"; Filename: "{app}\Third-party licences.txt"
Name: "{group}\Uninstall Drenched"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Drenched.exe"; Description: "Open Drenched now"; Flags: nowait postinstall skipifsilent; Components: app
