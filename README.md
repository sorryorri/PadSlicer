# PadSlicer

An 8-bit chop sampler plugin by **@sorryorri** & **franko**. Slice loops, play one-shots on 16 pads, add some FX, and level up Sir Slicealot the knight while you play.

Built with [JUCE](https://juce.com). Runs as an Audio Unit, VST3 or standalone app on macOS 11+ (Apple Silicon and Intel).

## Features

- **Slicer**: drop a loop and cut it into an even grid (1–64 slices) or at every detected hit. Slices play from C1 (MIDI note 36) upwards. Scroll or pinch over the waveform to zoom in, then drag the slice lines to fine-tune them (double-click adds a line, Option-click removes one).
- **Tempo sync and pitch**: loops play at your DAW's tempo without changing pitch (SYNC, with the loop's BPM detected from its length), and PITCH changes pitch without changing length. Hits stay tight thanks to a grain engine that restarts on every transient. SPEED is tape-style and changes both.
- **Pads**: 16 Drum Rack–style pads for one-shots. Tick slices in the slicer and press **TRANSFER** to send them to empty pads.
- **Per-slice and per-pad settings**: start, pitch (±24 semitones, keeps the length), speed, low-pass cutoff and volume for every slice and pad, plus master controls that can be automated in your DAW.
- **Trigger / Gate** playback, sample-accurate MIDI, 16 voices.
- **Pattern generator** (GEN page): every press of GENERATE makes a new MIDI pattern for the slices or pads, shaped by bars, grid, note length, density, swing, order, variation, rolls, accent and range. Edit it in a piano roll that works like Ableton's MIDI editor (select notes or bars, Delete, Cmd+D duplicate, Cmd+C/X/V, Cmd+Z, arrow keys, B for draw mode); your edits survive knob tweaks. Or import a MIDI file by dropping it on the page. Preview it in the plugin, then drag it onto a DAW track as a MIDI clip or save it as a .mid file. Previewing doesn't earn the knight XP.
- **FX**: Juno-style chorus, analog-style flanger, tempo-synced tape echo and a Dattorro plate reverb.
- **Retro UI**: pixel-art interface drawn entirely in code, with an amber LCD and a resizable window (75–200%).
- **Sir Slicealot**: every hit earns XP. He levels up through 8 ranks, each with new gear and scenery.

## Building

You need:

- macOS with the Xcode Command Line Tools (`xcode-select --install`)
- [CMake](https://cmake.org/download/) 3.22 or newer

JUCE is downloaded automatically the first time you configure.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The AU and VST3 are copied into `~/Library/Audio/Plug-Ins` after building. Restart your DAW and rescan plug-ins; PadSlicer appears under **@sorryorri**.

To use a JUCE checkout you already have instead of downloading it, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE` to the first command.

## Code layout

| File | What it does |
| --- | --- |
| `PluginProcessor.*` | Audio engine: voices, slicing, sample loading, parameters, saving/restoring state |
| `PluginEditor.*` | The interface: slicer screen, pad grid, FX page, knobs, size menu |
| `Effects.*` | Chorus, flanger, tape echo and plate reverb |
| `Transients.*` | Hit (transient) detection for slicing by hits |
| `Knight.*`, `KnightProgress.h` | Sir Slicealot: sprites, ranks, scenery and XP |
| `Retro.*` | Pixel font, look-and-feel, knobs and buttons |

## Licence

PadSlicer uses JUCE, which is available under the AGPLv3 or a commercial JUCE licence. See [juce.com/get-juce](https://juce.com/get-juce) for details.
