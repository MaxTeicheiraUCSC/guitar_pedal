# PedalEmu VST3 on Windows — testing in Reaper

1. Unzip. Copy the whole `PedalEmu.vst3` **folder** to `C:\Program Files\Common Files\VST3\`
   (it is a bundle: `PedalEmu.vst3\Contents\x86_64-win\PedalEmu.vst3`). 64-bit Reaper only.
2. Reaper → Options → Preferences → Plug-ins → VST → **Re-scan** (or **Clear cache/re-scan** if it was
   loaded before). It appears as *PedalEmu* (VST3, guitar_pedal).
3. Insert it on a track (FX button → filter "PedalEmu").
4. Sources, same as the standalone:
   - The plugin starts on the built-in **E / G♯ loop**, so you hear it with no input. Pick *Live input*
     in the Source menu to run the track's audio (your interface) through the pedal instead.
   - Keys: click the on-screen keyboard, or send MIDI to the track (Reaper virtual MIDI keyboard:
     View → Virtual MIDI keyboard; set the track's input to it and enable record-arm + input monitoring).
   - MIDI CC/SysEx follow `docs/midi-map.md`, so a MIDI controller or Reaper automation items can drive
     every parameter; the effect parameters are also exposed as VST3 automation lanes.
5. Latency: the analog+codec model adds 31 samples; Reaper compensates automatically.

Known limits: not code-signed (SmartScreen may warn once); Windows ARM64 is not built; the VST2
format is not provided (Steinberg no longer licenses the SDK).
