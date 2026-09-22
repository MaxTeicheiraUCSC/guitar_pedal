; Inno Setup script for the PedalEmu Windows package.
; Built in CI: iscc /DMyAppVersion=x.y.z /DSrcDir=<artefacts Release dir> /DRepoDir=<repo root> installer\pedalemu.iss
;
; Installs the VST3 bundle into the standard shared VST3 folder so the user never has to
; drag anything into a DAW (dragging a .vst3 into a DAW window does not install it, and
; dragging from inside Windows' zip viewer copies an incomplete bundle - both look like
; "import failed"). The standalone app is an optional component for testing without a DAW.

#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
#ifndef SrcDir
  #define SrcDir "..\emulator\build-juce\juce\PedalEmu_artefacts\Release"
#endif
#ifndef RepoDir
  #define RepoDir ".."
#endif

[Setup]
AppId={{82099445-D141-5602-B719-E6955951AF09}
AppName=PedalEmu
AppVersion={#MyAppVersion}
AppPublisher=guitar_pedal (GPLv3)
AppPublisherURL=https://github.com/MaxTeicheiraUCSC/guitar_pedal
DefaultDirName={autopf}\PedalEmu
DefaultGroupName=PedalEmu
DisableProgramGroupPage=yes
LicenseFile={#RepoDir}\LICENSE
OutputBaseFilename=PedalEmu-{#MyAppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayName=PedalEmu {#MyAppVersion}

[Types]
Name: "full"; Description: "VST3 plugin and standalone app"
Name: "plugin"; Description: "VST3 plugin only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plugin (installs to the shared VST3 folder)"; Types: full plugin custom; Flags: fixed
Name: "standalone"; Description: "Standalone app (play the pedal without a DAW)"; Types: full

[Files]
; VST3 is a bundle (a folder), so the whole tree is copied.
Source: "{#SrcDir}\VST3\PedalEmu.vst3\*"; DestDir: "{commoncf64}\VST3\PedalEmu.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SrcDir}\Standalone\PedalEmu.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#RepoDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoDir}\docs\WINDOWS-INSTALL.md"; DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion
Source: "{#RepoDir}\emulator\assets\FREEPATS-LICENSE-CC0.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\PedalEmu"; Filename: "{app}\PedalEmu.exe"; Components: standalone
Name: "{group}\PedalEmu read me"; Filename: "{app}\README.txt"

[Run]
Filename: "{app}\PedalEmu.exe"; Description: "Start PedalEmu now"; \
    Components: standalone; Flags: nowait postinstall skipifsilent

[Messages]
FinishedLabel=PedalEmu is installed.%n%nThe VST3 went into the shared VST3 folder. In your DAW, rescan plug-ins (Reaper: Options > Preferences > Plug-ins > VST > Re-scan) and PedalEmu will appear in the list. Do not drag the .vst3 into the DAW window - it is already installed.
