# PedalEmu on Windows

## Easiest: run the installer

Download `PedalEmu-<version>-windows-x64-setup.exe` from the release page and run it.
It puts the VST3 in the shared VST3 folder and can optionally install the standalone app.
Windows SmartScreen may warn once because the installer is not code-signed: click
"More info" then "Run anyway".

After it finishes, rescan plug-ins in your DAW:

- Reaper: Options > Preferences > Plug-ins > VST > Re-scan (use "Clear cache/re-scan" if
  PedalEmu was installed before).
- Most other DAWs rescan on startup, or have a Rescan button in their plug-in settings.

PedalEmu then appears in the plug-in list as "PedalEmu" (VST3, guitar_pedal).

## Manual install from the zip

`PedalEmu.vst3` is a **bundle** - a folder, not a single file. Install it by copying:

1. Right-click the downloaded zip, choose Properties, tick **Unblock** if that box is
   there, then Extract All. Extract it first; do not drag files out of the zip preview
   window, which copies an incomplete folder.
2. Copy the whole `PedalEmu.vst3` **folder** into `C:\Program Files\Common Files\VST3\`.
3. Rescan plug-ins in your DAW as above.

**Dragging a .vst3 into a DAW window does not install a plugin** - most DAWs answer with
"import failed" or simply ignore it. Plugins are found by scanning the VST3 folder.

## If it still does not show up

- 64-bit DAW only; there is no 32-bit or ARM64 build.
- Check the file really is at
  `C:\Program Files\Common Files\VST3\PedalEmu.vst3\Contents\x86_64-win\PedalEmu.vst3`.
- Look at your DAW's plug-in scan log for the reason it rejected the file.
- Builds from v0.1.3 onward link the Visual C++ runtime statically, so no redistributable
  is needed. Earlier builds needed the "Visual C++ 2015-2022 Redistributable (x64)" and
  failed to load without it.

## Using it

The plugin starts on its built-in guitar loop, so you hear something with no input
connected. Pick *Live input* in the Source menu to run the track's audio through the
pedal, or play the on-screen keyboard (it also responds to MIDI sent to the track).
Every effect parameter is exposed for DAW automation, and the MIDI CC/SysEx map in
`docs/midi-map.md` works from a controller.
