#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

//==============================================================================
// Makes MIDI patterns that play the slicer's slices or the kit's pads.
// A pattern depends only on the settings and a seed, so turning a knob reshapes the
// current pattern, and pressing GENERATE (a new seed) makes a new one.
namespace Generator
{
    enum Control { bars, grid, noteLength, density, swing, order, vary, rolls, accent, range, numControls };

    struct ControlInfo
    {
        const char* name;
        float min, max, defaultValue;
    };

    const ControlInfo& getControlInfo (int control);
    juce::String getValueText (int control, float value);

    struct Note
    {
        double start, length;   // in beats
        int note;
        float velocity;
    };

    // Identifies a generated note that was erased by hand
    struct EditKey
    {
        double start;
        int note;
    };

    struct Settings
    {
        Settings();

        std::array<float, numControls> values;
        juce::int64 seed = 0;   // 0 = nothing generated (the pattern is only what was drawn by hand)

        // Hand edits, kept on top of the generated notes when knobs are turned
        std::vector<Note> added;
        std::vector<EditKey> removed;

        int getInt (Control c) const   { return juce::roundToInt (values[(size_t) c]); }
        float getAmount (Control c) const { return values[(size_t) c] / 100.0f; }   // percentage controls as 0..1
        bool hasEdits() const          { return ! added.empty() || ! removed.empty(); }
    };

    // Something the pattern can play: a slice or a pad
    struct Source
    {
        int note;
        double position;   // where it sits in the loop, 0..1 (used by ORDER)
    };

    // The step grid, for drawing notes by hand
    double getBarsChoiceBeats (int barsIndex);   // length of each BARS choice
    double getPatternBeats (const Settings&);
    double getStepBeats (const Settings&);
    double getStepStart (const Settings&, int step);   // includes swing
    double getDrawnNoteBeats (const Settings&);        // length of a note drawn by hand
    bool isSameNote (const Note&, const EditKey&);

    struct Pattern
    {
        std::vector<Note> notes;
        double lengthBeats = 4.0;
    };

    // loopBeats is how long the sliced loop lasts, so ORDER can line slices up with where they were
    Pattern generate (const Settings&, const std::vector<Source>& sources, double loopBeats);

    juce::MidiFile toMidiFile (const Pattern&);
}
