#include "Generator.h"

#include <algorithm>

namespace Generator
{
namespace
{
    constexpr std::array<double, 4> barBeats { 4.0, 8.0, 16.0, 32.0 };
    constexpr std::array<const char*, 4> barNames { "1 BAR", "2 BARS", "4 BARS", "8 BARS" };

    constexpr std::array<int, 5> gridStepsPerBeat { 2, 4, 8, 3, 6 };
    constexpr std::array<const char*, 5> gridNames { "1/8", "1/16", "1/32", "1/8T", "1/16T" };

    constexpr double legato = -1.0;
    constexpr std::array<double, 6> noteBeats { 0.125, 0.25, 0.5, 1.0, 2.0, legato };
    constexpr std::array<const char*, 6> noteNames { "1/32", "1/16", "1/8", "1/4", "1/2", "LEGATO" };

    const std::array<ControlInfo, numControls> controls
    { {
        { "BARS",    0.0f, 3.0f,   0.0f },
        { "GRID",    0.0f, 4.0f,   1.0f },
        { "LENGTH",  0.0f, 5.0f,   1.0f },
        { "DENSITY", 0.0f, 100.0f, 60.0f },
        { "SWING",   0.0f, 100.0f, 0.0f },
        { "ORDER",   0.0f, 100.0f, 50.0f },
        { "VARY",    0.0f, 100.0f, 25.0f },
        { "ROLLS",   0.0f, 100.0f, 10.0f },
        { "ACCENT",  0.0f, 100.0f, 50.0f },
        { "RANGE",   0.0f, 100.0f, 100.0f },
    } };

    struct Hit
    {
        bool on = false;
        int source = 0;
        float velocity = 0.8f;
        int roll = 1;
    };
}

//==============================================================================
const ControlInfo& getControlInfo (int control)
{
    return controls[(size_t) juce::jlimit (0, numControls - 1, control)];
}

juce::String getValueText (int control, float value)
{
    const int index = juce::roundToInt (value);

    switch (control)
    {
        case bars:       return barNames[(size_t) juce::jlimit (0, 3, index)];
        case grid:       return gridNames[(size_t) juce::jlimit (0, 4, index)];
        case noteLength: return noteNames[(size_t) juce::jlimit (0, 5, index)];
        default:         return juce::String (index) + " %";
    }
}

Settings::Settings()
{
    for (int c = 0; c < numControls; ++c)
        values[(size_t) c] = controls[(size_t) c].defaultValue;
}

//==============================================================================
double getBarsChoiceBeats (int barsIndex)
{
    return barBeats[(size_t) juce::jlimit (0, 3, barsIndex)];
}

double getPatternBeats (const Settings& settings)
{
    return barBeats[(size_t) juce::jlimit (0, 3, settings.getInt (bars))];
}

double getStepBeats (const Settings& settings)
{
    return 1.0 / gridStepsPerBeat[(size_t) juce::jlimit (0, 4, settings.getInt (grid))];
}

double getStepStart (const Settings& settings, int step)
{
    const bool triplets = settings.getInt (grid) >= 3;
    const double stepBeats = getStepBeats (settings);
    const double swingDelay = triplets ? 0.0 : settings.getAmount (swing) * 0.5 * stepBeats;
    return step * stepBeats + (step % 2 == 1 ? swingDelay : 0.0);
}

double getDrawnNoteBeats (const Settings& settings)
{
    const double length = noteBeats[(size_t) juce::jlimit (0, 5, settings.getInt (noteLength))];
    return length > 0.0 ? length : getStepBeats (settings);
}

bool isSameNote (const Note& note, const EditKey& key)
{
    return note.note == key.note && std::abs (note.start - key.start) < 1.0e-4;
}

namespace
{
    void generateNotes (Pattern& pattern, const Settings& settings, const std::vector<Source>& allSources, double loopBeats);

    void applyEdits (Pattern& pattern, const Settings& settings)
    {
        auto& notes = pattern.notes;

        notes.erase (std::remove_if (notes.begin(), notes.end(), [&settings] (const Note& note)
                     {
                         return std::any_of (settings.removed.begin(), settings.removed.end(),
                                             [&note] (const EditKey& key) { return isSameNote (note, key); });
                     }),
                     notes.end());

        for (auto note : settings.added)
        {
            if (note.start >= pattern.lengthBeats)
                continue;

            note.length = juce::jmax (0.01, juce::jmin (note.length, pattern.lengthBeats - note.start));
            notes.push_back (note);
        }

        std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    }
}

Pattern generate (const Settings& settings, const std::vector<Source>& allSources, double loopBeats)
{
    Pattern pattern;
    pattern.lengthBeats = getPatternBeats (settings);

    if (! allSources.empty() && settings.seed != 0)
        generateNotes (pattern, settings, allSources, loopBeats);

    applyEdits (pattern, settings);
    return pattern;
}

namespace
{
void generateNotes (Pattern& pattern, const Settings& settings, const std::vector<Source>& allSources, double loopBeats)
{
    juce::Random random (settings.seed);

    // RANGE: use only some of the slices or pads, picked by the seed
    auto sources = allSources;

    for (size_t i = sources.size() - 1; i > 0; --i)
        std::swap (sources[i], sources[(size_t) random.nextInt ((int) i + 1)]);

    const int keep = juce::jlimit (1, (int) sources.size(), juce::roundToInt (settings.getAmount (range) * (float) sources.size()));
    sources.resize ((size_t) keep);
    std::sort (sources.begin(), sources.end(), [] (const Source& a, const Source& b) { return a.position < b.position; });

    const int gridIndex = juce::jlimit (0, 4, settings.getInt (grid));
    const int stepsPerBeat = gridStepsPerBeat[(size_t) gridIndex];
    const bool triplets = gridIndex >= 3;
    const double stepBeats = 1.0 / stepsPerBeat;
    const int stepsPerBar = 4 * stepsPerBeat;
    const int totalSteps = juce::roundToInt (pattern.lengthBeats * stepsPerBeat);
    loopBeats = juce::jmax (1.0, loopBeats);

    const float densityAmount = settings.getAmount (density);
    const float orderAmount = settings.getAmount (order);
    const float accentAmount = settings.getAmount (accent);
    const float rollAmount = settings.getAmount (rolls);

    auto rollStep = [&] (int step)
    {
        // Always draw the same random numbers per step, so turning one knob doesn't reshuffle the rest
        const float rOn = random.nextFloat(), rOrder = random.nextFloat(), rPick = random.nextFloat();
        const float rVelocity = random.nextFloat(), rRoll = random.nextFloat(), rRollCount = random.nextFloat();

        const int inBar = step % stepsPerBar;
        const bool onBeat = inBar % stepsPerBeat == 0;
        const bool onHalfBeat = ! triplets && (inBar * 2) % stepsPerBeat == 0;
        const float weight = onBeat ? 1.4f : (onHalfBeat ? 1.0f : 0.7f);

        Hit hit;
        hit.on = (inBar == 0 && densityAmount > 0.0f) || rOn < densityAmount * weight;

        // ORDER: the slice that sits at this point of the original loop, or any slice
        const double where = std::fmod (step * stepBeats, loopBeats) / loopBeats;
        int ordered = 0;

        for (size_t i = 0; i < sources.size(); ++i)
            if (sources[i].position <= where + 1.0e-9)
                ordered = (int) i;

        hit.source = rOrder < orderAmount ? ordered
                                          : juce::jmin ((int) sources.size() - 1, (int) (rPick * (float) sources.size()));

        // ACCENT: louder on the beat, looser velocities in between
        const float velocity = 0.78f + (onBeat ? 0.2f * accentAmount : 0.0f) + (rVelocity - 0.5f) * 0.5f * accentAmount;
        hit.velocity = juce::jlimit (0.2f, 1.0f, velocity);

        // ROLLS: a quick burst of 2 to 4 hits inside the step
        if (rRoll < rollAmount * 0.4f)
            hit.roll = 2 + juce::jmin (2, (int) (rRollCount * 3.0f));

        return hit;
    };

    // The first loop-length (at least a bar) is the motif; after that it repeats, and VARY re-rolls some steps
    const int motifSteps = juce::jlimit (1, totalSteps, juce::jmax (stepsPerBar, juce::roundToInt (loopBeats * stepsPerBeat)));
    const float varyAmount = settings.getAmount (vary);
    std::vector<Hit> hits ((size_t) totalSteps);

    for (int step = 0; step < totalSteps; ++step)
    {
        const auto fresh = rollStep (step);
        const bool reroll = random.nextFloat() < varyAmount;
        hits[(size_t) step] = step < motifSteps || reroll ? fresh : hits[(size_t) (step - motifSteps)];
    }

    // Turn steps into notes
    const double noteBeatsChoice = noteBeats[(size_t) juce::jlimit (0, 5, settings.getInt (noteLength))];
    const double swingDelay = triplets ? 0.0 : settings.getAmount (swing) * 0.5 * stepBeats;

    for (int step = 0; step < totalSteps; ++step)
    {
        const auto& hit = hits[(size_t) step];

        if (! hit.on)
            continue;

        double start = step * stepBeats;

        if (step % 2 == 1)
            start += swingDelay;   // swing pushes every second step late

        const int note = sources[(size_t) hit.source].note;

        if (hit.roll > 1)
        {
            const double rollLength = stepBeats / hit.roll;

            for (int k = 0; k < hit.roll; ++k)
                pattern.notes.push_back ({ start + k * rollLength, rollLength * 0.9, note,
                                           hit.velocity * std::pow (0.85f, (float) k) });
        }
        else
        {
            pattern.notes.push_back ({ start, noteBeatsChoice, note, hit.velocity });
        }
    }

    std::sort (pattern.notes.begin(), pattern.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });

    for (size_t i = 0; i < pattern.notes.size(); ++i)
    {
        auto& n = pattern.notes[i];

        // LEGATO: each note lasts until the next one starts
        if (n.length < 0.0)
        {
            const double next = i + 1 < pattern.notes.size() ? pattern.notes[i + 1].start : pattern.lengthBeats;
            n.length = juce::jmax (stepBeats * 0.25, next - n.start);
        }

        // Keep everything inside the clip
        n.length = juce::jmax (0.01, juce::jmin (n.length, pattern.lengthBeats - n.start));
    }
}
}

juce::MidiFile toMidiFile (const Pattern& pattern)
{
    constexpr double ticksPerBeat = 960.0;

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "PadSlicer"), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    for (const auto& note : pattern.notes)
    {
        track.addEvent (juce::MidiMessage::noteOn (1, note.note, note.velocity), note.start * ticksPerBeat);
        track.addEvent (juce::MidiMessage::noteOff (1, note.note), (note.start + note.length) * ticksPerBeat);
    }

    track.updateMatchedPairs();

    // Marks the clip's full length, so a pattern ending in a rest keeps its last bars
    track.addEvent (juce::MidiMessage::endOfTrack(), pattern.lengthBeats * ticksPerBeat);

    juce::MidiFile file;
    file.setTicksPerQuarterNote ((int) ticksPerBeat);
    file.addTrack (track);
    return file;
}
}
