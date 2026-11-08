# Tape instruments

Virtual instruments for Tape: your MIDI keyboard plays the sounds in `Studio\Sounds`.

- `soundlibrary.*`: finds every SFZ instrument and SF2/SF3 soundfont in the Sounds folder and sorts them into families (piano, strings, winds…) for the sound picker.
- `tools/render_test.cpp`: offline check of the SFZ engine (sfizz): plays a phrase and writes a WAV.
- `tools/scan_test.cpp`: prints what the sound picker would show.

Engine: [sfizz](https://github.com/sfztools/sfizz) (BSD-2-Clause) for SFZ. SF2/SF3 soundfonts will use FluidSynth.
In Tape: menu **Record → Instruments…**
- picks up your MIDI keyboard (or OP-1) automatically; choose another one in the panel
- built-in sounds always work; your SFZ libraries in `Studio\Sounds` appear after unzipping + Rescan
- plays through its own low-latency output (WASAPI on Windows)
- **Record take**: records what you play (WAV + MIDI) into `Documents\Studio\Takes` and adds it to the project as a new track
- **Record → All notes off** stops stuck notes

Next: SF2 soundfonts (FluidSynth), takes synced to playback, MIDI tracks in the editor.
