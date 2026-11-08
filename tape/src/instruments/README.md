# Tape instruments

Virtual instruments for Tape: your MIDI keyboard plays the sounds in `Studio\Sounds`.

- `soundlibrary.*`: finds every SFZ instrument and SF2/SF3 soundfont in the Sounds folder and sorts them into families (piano, strings, winds…) for the sound picker.
- `tools/render_test.cpp`: offline check of the SFZ engine (sfizz): plays a phrase and writes a WAV.
- `tools/scan_test.cpp`: prints what the sound picker would show.

Engine: [sfizz](https://github.com/sfztools/sfizz) (BSD-2-Clause) for SFZ. SF2/SF3 soundfonts will use FluidSynth.
Not yet wired into Tape's build: next steps are MIDI input, the live audio path, and the picker UI.
