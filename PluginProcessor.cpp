#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Transients.h"

namespace
{
    constexpr float attackSeconds  = 0.001f;
    constexpr float releaseSeconds = 0.010f;
    constexpr double declickSeconds = 0.002;   // fade applied as a voice reaches the end of its region
    constexpr double maxSampleSeconds = 600.0;
    constexpr double grainSeconds = 0.05;           // each grain; they overlap by half
    constexpr double transientFadeSeconds = 0.0015; // crossfade from the old grains into a new hit
    constexpr double alignSeconds = 0.012;          // how far a grain may be nudged to line up with the last one

    // Tempo-synced echo times
    struct Division
    {
        const char* name;
        double beats;
    };

    constexpr Division echoDivisions[] =
    {
        { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
        { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 },
        { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/1", 4.0 },
    };

    constexpr int numEchoDivisions = (int) (sizeof (echoDivisions) / sizeof (echoDivisions[0]));

    // Saved-state names
    const juce::Identifier slotsTag ("SLOTS"), slotTag ("SLOT"), settingsTag ("SETTINGS"), settingTag ("S");
    const juce::Identifier uiScaleProperty ("uiScale");
    const juce::Identifier controlProperties[] = { "start", "pitch", "speed", "cutoff", "volume" };
    const juce::Identifier generatorTag ("GEN");
}

//==============================================================================
juce::String PadSlicerAudioProcessor::SlotInfo::getLabel() const
{
    const auto name = file.getFileNameWithoutExtension();
    return chopNumber > 0 ? "#" + juce::String (chopNumber) + " " + name : name;
}

const char* PadSlicerAudioProcessor::masterParameterFor (SlotControl control)
{
    switch (control)
    {
        case startControl:  return startId;
        case pitchControl:  return pitchId;
        case speedControl:  return speedId;
        case cutoffControl: return cutoffId;
        case volumeControl: return volumeId;
        case numSlotControls: break;
    }

    return startId;
}

float PadSlicerAudioProcessor::defaultSetting (SlotControl control)
{
    switch (control)
    {
        case speedControl:  return 1.0f;
        case cutoffControl: return 20000.0f;
        case startControl:
        case pitchControl:
        case volumeControl:
        case numSlotControls: break;
    }

    return 0.0f;
}

float PadSlicerAudioProcessor::getSetting (int settingsIndex, SlotControl control) const
{
    return settings[(size_t) settingsIndex][(size_t) control].load (std::memory_order_relaxed);
}

void PadSlicerAudioProcessor::setSetting (int settingsIndex, SlotControl control, float value)
{
    settings[(size_t) settingsIndex][(size_t) control].store (value, std::memory_order_relaxed);
}

void PadSlicerAudioProcessor::resetSettings (int settingsIndex)
{
    for (int c = 0; c < numSlotControls; ++c)
        setSetting (settingsIndex, (SlotControl) c, defaultSetting ((SlotControl) c));
}

//==============================================================================
PadSlicerAudioProcessor::PadSlicerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PadSlicerParams", createParameterLayout())
{
    formatManager.registerBasicFormats();

    modeParam    = apvts.getRawParameterValue (modeId);
    triggerParam = apvts.getRawParameterValue (triggerId);
    slicesParam  = apvts.getRawParameterValue (slicesId);
    sliceByParam = apvts.getRawParameterValue (sliceById);
    hitSensParam = apvts.getRawParameterValue (hitSensId);
    startParam   = apvts.getRawParameterValue (startId);
    cutoffParam  = apvts.getRawParameterValue (cutoffId);
    speedParam   = apvts.getRawParameterValue (speedId);
    pitchParam   = apvts.getRawParameterValue (pitchId);
    volumeParam  = apvts.getRawParameterValue (volumeId);
    syncParam    = apvts.getRawParameterValue (syncId);
    loopBpmParam = apvts.getRawParameterValue (loopBpmId);

    auto bindFx = [this] (FxParams& params, const FxIds& ids)
    {
        params.on = apvts.getRawParameterValue (ids.on);

        for (size_t i = 0; i < ids.controls.size(); ++i)
            params.controls[i] = apvts.getRawParameterValue (ids.controls[i]);
    };

    bindFx (chorusParams, chorusIds);
    bindFx (flangerParams, flangerIds);
    bindFx (echoParams, echoIds);
    bindFx (plateParams, plateIds);

    for (int i = 0; i < numSettingSlots; ++i)
        resetSettings (i);

    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    filter.setResonance (1.0f / juce::MathConstants<float>::sqrt2);

    for (auto& voice : voices)
    {
        voice.filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        voice.filter.setResonance (1.0f / juce::MathConstants<float>::sqrt2);
    }

    // Re-detects hits when the sensitivity changes
    startTimerHz (5);
}

PadSlicerAudioProcessor::~PadSlicerAudioProcessor()
{
    stopTimer();
    loader.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout PadSlicerAudioProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { modeId, 1 }, "Mode",
                                                        StringArray { "Slice", "Kit" }, 0));

    // Trigger = play to the end regardless of note-off (Simpler's one-shot mode), Gate = stop on note-off
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { triggerId, 1 }, "Trigger Mode",
                                                        StringArray { "Trigger", "Gate" }, 0));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { slicesId, 1 }, "Slices",
                                                     1, maxSlices, 16));

    // Grid = even slices, Hits = one slice per detected transient, Manual = slice lines placed by hand
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { sliceById, 1 }, "Slice By",
                                                        StringArray { "Grid", "Hits", "Manual" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { hitSensId, 1 }, "Hit Sensitivity",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));

    // Master controls. They act on top of each slice's and pad's own settings.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { startId, 1 }, "Master Start",
                                                       NormalisableRange<float> (0.0f, 95.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")));

    NormalisableRange<float> cutoffRange (20.0f, 20000.0f, 1.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { cutoffId, 1 }, "Master Cutoff",
                                                       cutoffRange, 20000.0f,
                                                       AudioParameterFloatAttributes().withLabel ("Hz")));

    NormalisableRange<float> speedRange (0.25f, 4.0f, 0.01f);
    speedRange.setSkewForCentre (1.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { speedId, 1 }, "Master Speed",
                                                       speedRange, 1.0f,
                                                       AudioParameterFloatAttributes().withLabel ("x")));

    // Transpose in semitones. Like Speed, this resamples, so it changes length as well as pitch.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pitchId, 1 }, "Master Pitch",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withStringFromValueFunction ([] (float value, int)
                                                           {
                                                               const int semitones = roundToInt (value);
                                                               return (semitones > 0 ? "+" : "") + String (semitones) + " st";
                                                           })
                                                           .withValueFromStringFunction ([] (const String& text)
                                                           {
                                                               return text.getFloatValue();
                                                           })));

    // -60 dB is treated as silence
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { volumeId, 1 }, "Master Volume",
                                                       NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));

    // Tempo sync: loops (and chops cut from them) play at the host's tempo without changing pitch
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { syncId, 1 }, "Tempo Sync", true));

    // The loop's own tempo. Set from the loop's length when one is loaded; can be changed by hand.
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { loopBpmId, 1 }, "Loop BPM",
                                                       NormalisableRange<float> (40.0f, 300.0f, 0.01f), 120.0f,
                                                       AudioParameterFloatAttributes().withLabel ("BPM")));

    //==============================================================================
    auto addSwitch = [&layout] (const char* id, const char* name)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false));
    };

    auto addFloat = [&layout] (const char* id, const char* name, float min, float max, float step,
                               float defaultValue, const char* label, float centre = 0.0f)
    {
        NormalisableRange<float> range (min, max, step);

        if (centre > min)
            range.setSkewForCentre (centre);

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, defaultValue,
                                                           AudioParameterFloatAttributes().withLabel (label)));
    };

    // Juno-style chorus
    addSwitch (chorusIds.on, "Chorus On");
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { chorusIds.controls[0], 1 }, "Chorus Mode",
                                                        StringArray { "I", "II", "I+II" }, 0));
    addFloat (chorusIds.controls[1], "Chorus Rate", 0.05f, 5.0f, 0.01f, 0.5f, "Hz", 0.8f);
    addFloat (chorusIds.controls[2], "Chorus Depth", 0.0f, 100.0f, 1.0f, 60.0f, "%");
    addFloat (chorusIds.controls[3], "Chorus Mix", 0.0f, 100.0f, 1.0f, 50.0f, "%");

    // Analog-style flanger
    addSwitch (flangerIds.on, "Flanger On");
    addFloat (flangerIds.controls[0], "Flanger Rate", 0.02f, 5.0f, 0.01f, 0.2f, "Hz", 0.3f);
    addFloat (flangerIds.controls[1], "Flanger Depth", 0.0f, 100.0f, 1.0f, 70.0f, "%");
    addFloat (flangerIds.controls[2], "Flanger Feedback", -95.0f, 95.0f, 1.0f, 50.0f, "%");
    addFloat (flangerIds.controls[3], "Flanger Mix", 0.0f, 100.0f, 1.0f, 50.0f, "%");

    // Tape echo, synced to the host tempo
    StringArray divisionNames;

    for (const auto& division : echoDivisions)
        divisionNames.add (division.name);

    addSwitch (echoIds.on, "Echo On");
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { echoIds.controls[0], 1 }, "Echo Time",
                                                        divisionNames, 6));
    addFloat (echoIds.controls[1], "Echo Feedback", 0.0f, 100.0f, 1.0f, 40.0f, "%");
    addFloat (echoIds.controls[2], "Echo Tape", 0.0f, 100.0f, 1.0f, 40.0f, "%");
    addFloat (echoIds.controls[3], "Echo Mix", 0.0f, 100.0f, 1.0f, 30.0f, "%");

    // Plate reverb
    addSwitch (plateIds.on, "Plate On");
    addFloat (plateIds.controls[0], "Plate Decay", 0.3f, 12.0f, 0.01f, 2.5f, "s", 2.5f);
    addFloat (plateIds.controls[1], "Plate Tone", 0.0f, 100.0f, 1.0f, 60.0f, "%");
    addFloat (plateIds.controls[2], "Plate Pre-Delay", 0.0f, 200.0f, 1.0f, 20.0f, "ms");
    addFloat (plateIds.controls[3], "Plate Mix", 0.0f, 100.0f, 1.0f, 30.0f, "%");

    return layout;
}

//==============================================================================
const juce::String PadSlicerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool PadSlicerAudioProcessor::acceptsMidi() const
{
    return true;
}

bool PadSlicerAudioProcessor::producesMidi() const
{
    return false;
}

bool PadSlicerAudioProcessor::isMidiEffect() const
{
    return false;
}

double PadSlicerAudioProcessor::getTailLengthSeconds() const
{
    const bool longTail = echoParams.on->load() > 0.5f || plateParams.on->load() > 0.5f;
    return longTail ? 10.0 : releaseSeconds;
}

int PadSlicerAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int PadSlicerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void PadSlicerAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String PadSlicerAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void PadSlicerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void PadSlicerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    grainLength = juce::jmax (64, juce::roundToInt (grainSeconds * sampleRate));
    grainHop = grainLength / 2;
    transientFade = juce::jmax (8, juce::roundToInt (transientFadeSeconds * sampleRate));

    attackStep  = 1.0f / juce::jmax (1.0f, attackSeconds  * (float) sampleRate);
    releaseStep = 1.0f / juce::jmax (1.0f, releaseSeconds * (float) sampleRate);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock,
                                        (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };
    filter.prepare (spec);
    filter.reset();

    for (auto& voice : voices)
    {
        voice.active = false;
        voice.filter.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
        voice.filter.reset();
    }

    cutoffSmoothed.reset (sampleRate, 0.02);
    cutoffSmoothed.setCurrentAndTargetValue (juce::jmin (cutoffParam->load(), (float) sampleRate * 0.45f));
    filter.setCutoffFrequency (cutoffSmoothed.getCurrentValue());

    gainSmoothed.reset (sampleRate, 0.02);
    gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (volumeParam->load(), -60.0f));

    keyboardState.reset();

    chorus.prepare (sampleRate);
    flanger.prepare (sampleRate);
    echo.prepare (sampleRate);
    plate.prepare (sampleRate);
}

void PadSlicerAudioProcessor::releaseResources()
{
}

bool PadSlicerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void PadSlicerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    lastBpm = getHostBpm();
    addPreviewNotes (midiMessages, numSamples);

    // Merge in pads clicked in the editor, and track held notes for the pad lights
    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    {
        const juce::SpinLock::ScopedTryLockType lock (sampleLock);

        // If a sample is being swapped in right now, skip this block rather than wait
        if (lock.isLocked())
        {
            for (int slot = 0; slot < numSlots; ++slot)
            {
                if (slotChanged[(size_t) slot])
                {
                    for (auto& voice : voices)
                        if (voice.slot == slot)
                            voice.active = false;

                    slotChanged[(size_t) slot] = false;
                }
            }

            // Render between MIDI events so note-ons land sample-accurately
            int position = 0;

            for (const auto metadata : midiMessages)
            {
                const int eventPosition = juce::jlimit (0, numSamples, metadata.samplePosition);

                if (eventPosition > position)
                {
                    renderVoices (buffer, position, eventPosition - position);
                    position = eventPosition;
                }

                handleMidiMessage (metadata.getMessage());
            }

            if (position < numSamples)
                renderVoices (buffer, position, numSamples - position);
        }
    }

    int numActive = 0;

    for (const auto& voice : voices)
        if (voice.active)
            ++numActive;

    activeVoiceCount = numActive;

    // Master low-pass filter, then the FX chain, then master volume
    cutoffSmoothed.setTargetValue (juce::jmin (cutoffParam->load(), (float) currentSampleRate * 0.45f));
    gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (volumeParam->load(), -60.0f));

    const int numChannels = buffer.getNumChannels();
    auto* const* channels = buffer.getArrayOfWritePointers();

    // Fully open means no filter at all, so the sound passes through untouched
    const bool filterOpen = ! cutoffSmoothed.isSmoothing() && cutoffParam->load() >= 19900.0f;

    if (filterOpen)
    {
        filter.reset();
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (cutoffSmoothed.isSmoothing())
                filter.setCutoffFrequency (cutoffSmoothed.getNextValue());

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] = filter.processSample (ch, channels[ch][i]);
        }

        filter.snapToZero();
    }

    processEffects (buffer, numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = gainSmoothed.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= gain;
    }

    const float peak = buffer.getMagnitude (0, numSamples);
    float previousPeak = outputPeak.load();

    while (peak > previousPeak && ! outputPeak.compare_exchange_weak (previousPeak, peak)) {}
}

void PadSlicerAudioProcessor::processEffects (juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto isOn = [] (const FxParams& params) { return params.on->load() > 0.5f; };
    auto value = [] (const FxParams& params, size_t index) { return params.controls[index]->load(); };
    auto percent = [&value] (const FxParams& params, size_t index) { return value (params, index) / 100.0f; };

    chorus.setParameters (juce::roundToInt (value (chorusParams, 0)), value (chorusParams, 1), percent (chorusParams, 2));
    chorus.setMix (isOn (chorusParams) ? percent (chorusParams, 3) : 0.0f);
    chorus.process (buffer, numSamples);

    flanger.setParameters (value (flangerParams, 0), percent (flangerParams, 1), percent (flangerParams, 2));
    flanger.setMix (isOn (flangerParams) ? percent (flangerParams, 3) : 0.0f);
    flanger.process (buffer, numSamples);

    const auto& division = echoDivisions[juce::jlimit (0, numEchoDivisions - 1, juce::roundToInt (value (echoParams, 0)))];
    echo.setParameters (division.beats * 60.0 / getHostBpm(), percent (echoParams, 1), percent (echoParams, 2));
    echo.setMix (isOn (echoParams) ? percent (echoParams, 3) : 0.0f);
    echo.process (buffer, numSamples);

    plate.setParameters (value (plateParams, 0), percent (plateParams, 1), value (plateParams, 2));
    plate.setMix (isOn (plateParams) ? percent (plateParams, 3) : 0.0f);
    plate.process (buffer, numSamples);
}

void PadSlicerAudioProcessor::setPreviewPattern (const Generator::Pattern& pattern)
{
    auto newPattern = std::make_unique<Generator::Pattern> (pattern);

    {
        const juce::SpinLock::ScopedLockType lock (patternLock);
        std::swap (previewPattern, newPattern);
    }

    // the old pattern is freed here, off the audio thread
}

void PadSlicerAudioProcessor::addPreviewNotes (juce::MidiBuffer& midi, int numSamples)
{
    if (! previewOn.load())
    {
        if (previewWasOn)
        {
            midi.addEvent (juce::MidiMessage::allNotesOff (previewChannel), 0);
            previewWasOn = false;
        }

        return;
    }

    const juce::SpinLock::ScopedTryLockType lock (patternLock);

    if (! lock.isLocked() || previewPattern == nullptr || previewPattern->lengthBeats <= 0.0)
        return;

    const auto& pattern = *previewPattern;
    const double beatsPerSample = lastBpm.load() / 60.0 / currentSampleRate;

    if (! previewWasOn)
    {
        previewWasOn = true;
        previewBeat = 0.0;
    }

    // While the host plays, line the preview up with its bars; otherwise run a clock of our own
    double start = previewBeat;

    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (position->getIsPlaying())
                if (auto ppq = position->getPpqPosition())
                    start = *ppq;

    const double end = start + numSamples * beatsPerSample;
    const double length = pattern.lengthBeats;

    // Adds an event if its time (repeating every pattern length) falls inside this block
    auto addIfInBlock = [&] (double beat, const juce::MidiMessage& message)
    {
        const double time = beat + std::ceil ((start - beat) / length) * length;

        if (time < end)
            midi.addEvent (message, juce::jlimit (0, numSamples - 1, (int) ((time - start) / beatsPerSample)));
    };

    // Note-offs first, so a note ending exactly where the next one starts doesn't cut it off
    for (const auto& note : pattern.notes)
        addIfInBlock (std::fmod (note.start + note.length, length), juce::MidiMessage::noteOff (previewChannel, note.note));

    for (const auto& note : pattern.notes)
        addIfInBlock (note.start, juce::MidiMessage::noteOn (previewChannel, note.note, note.velocity));

    previewBeat = end;
    previewPosition = std::fmod (juce::jmax (0.0, end), length);
}

double PadSlicerAudioProcessor::getHostBpm() const
{
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (auto bpm = position->getBpm())
                return juce::jlimit (20.0, 999.0, *bpm);

    return 120.0;
}

void PadSlicerAudioProcessor::handleMidiMessage (const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        startVoice (message.getNoteNumber(), message.getFloatVelocity(), message.getChannel() != previewChannel);
    }
    else if (message.isNoteOff())
    {
        // In Trigger mode hits always play to the end, like Drum Rack one-shots
        if (getTrigger() == Trigger::gate)
            releaseNote (message.getNoteNumber());
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        releaseAllVoices();
    }
}

//==============================================================================
const std::vector<int>* PadSlicerAudioProcessor::sliceLines (SliceBy sliceBy, const std::vector<int>& hits,
                                                              const std::vector<int>& manual)
{
    if (sliceBy == SliceBy::hits && ! hits.empty())
        return &hits;

    if (sliceBy == SliceBy::manual && ! manual.empty())
        return &manual;

    return nullptr;   // an even grid
}

int PadSlicerAudioProcessor::sliceCount (int length, SliceBy sliceBy, int gridSlices,
                                         const std::vector<int>& hits, const std::vector<int>& manual)
{
    if (length <= 0)
        return 0;

    if (const auto* lines = sliceLines (sliceBy, hits, manual))
        return juce::jmin ((int) lines->size(), maxSlices);

    return juce::jlimit (1, maxSlices, gridSlices);
}

juce::Range<int> PadSlicerAudioProcessor::sliceRegion (int index, int length, SliceBy sliceBy, int gridSlices,
                                                       const std::vector<int>& hits, const std::vector<int>& manual)
{
    const int count = sliceCount (length, sliceBy, gridSlices, hits, manual);

    if (index < 0 || index >= count)
        return {};

    // Hit and hand-placed slices run from one line to the next; with no lines, fall back to the grid
    if (const auto* lines = sliceLines (sliceBy, hits, manual))
    {
        const int start = juce::jlimit (0, length, (*lines)[(size_t) index]);
        const int end = index + 1 < (int) lines->size() ? (*lines)[(size_t) index + 1] : length;
        return { start, juce::jlimit (start, length, end) };
    }

    const double sliceLength = (double) length / count;
    return { (int) std::round (index * sliceLength), (int) std::round ((index + 1) * sliceLength) };
}

void PadSlicerAudioProcessor::startVoice (int note, float velocity, bool earnsXp)
{
    const int index = note - firstNote;
    int slot = 0, settingsIndex = 0;
    juce::Range<int> region;
    const Sample* source = nullptr;

    if (getMode() == Mode::kit)
    {
        if (index < 0 || index >= numPads || samples[(size_t) index] == nullptr)
            return;

        slot = index;
        settingsIndex = padSettings (index);
        source = samples[(size_t) slot].get();
        region = { 0, source->buffer.getNumSamples() };
    }
    else
    {
        const auto* loop = samples[(size_t) sliceSlot].get();

        if (loop == nullptr)
            return;

        region = sliceRegion (index, loop->buffer.getNumSamples(), getSliceBy(), (int) slicesParam->load(), loop->hits, loop->manual);

        if (region.isEmpty())
            return;

        slot = sliceSlot;
        settingsIndex = sliceSettings (index);
        source = loop;
    }

    const float startPercent = juce::jlimit (0.0f, 95.0f, getSetting (settingsIndex, startControl) + startParam->load());
    const int playStart = region.getStart() + (int) ((float) region.getLength() * startPercent / 100.0f);

    if (playStart >= region.getEnd())
        return;

    // Retriggering a pad chokes the previous hit, like a Drum Rack or MPC pad
    releaseNote (note);

    Voice* target = nullptr;

    for (auto& voice : voices)
    {
        if (! voice.active)
        {
            target = &voice;
            break;
        }
    }

    if (target == nullptr)   // all busy: steal the oldest
    {
        target = &voices[0];

        for (auto& voice : voices)
            if (voice.age < target->age)
                target = &voice;
    }

    target->active = true;
    target->releasing = false;
    target->note = note;
    target->slot = slot;
    target->settingsIndex = settingsIndex;
    target->timePos = playStart;
    target->start = playStart;
    target->end = region.getEnd();

    // The first grain starts at full level right on the hit, so its attack stays sharp
    target->grains = {};
    target->grains[0].active = true;
    target->grains[0].position = playStart;
    target->grains[0].fullStart = true;
    target->grainClock = grainHop;
    target->nextHit = (size_t) (std::upper_bound (source->hits.begin(), source->hits.end(), playStart) - source->hits.begin());
    target->velocityGain = velocity;
    target->envelope = 0.0f;
    target->gain = juce::Decibels::decibelsToGain (getSetting (settingsIndex, volumeControl), -60.0f);
    target->age = ++voiceCounter;
    target->filter.reset();

    lastHitNote = note;
    ++hitCount;

    if (earnsXp)
        knightProgress.addHit();
}

void PadSlicerAudioProcessor::releaseNote (int note)
{
    for (auto& voice : voices)
        if (voice.active && voice.note == note)
            voice.releasing = true;
}

void PadSlicerAudioProcessor::releaseAllVoices()
{
    for (auto& voice : voices)
        if (voice.active)
            voice.releasing = true;
}

void PadSlicerAudioProcessor::renderVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    const int numOutputChannels = juce::jmin (2, output.getNumChannels());
    const float masterSpeed = speedParam->load();
    const float masterPitch = pitchParam->load();
    const bool sync = syncParam->load() > 0.5f;
    const double hostBpm = lastBpm.load();
    const auto twoPi = juce::MathConstants<float>::twoPi;

    // Starts a grain; a full-start grain (on a hit) crossfades from the ones already playing
    auto startGrain = [this] (Voice& voice, double position, bool fullStart)
    {
        const bool crossfade = fullStart && std::any_of (voice.grains.begin(), voice.grains.end(),
                                                         [] (const Grain& g) { return g.active; });

        if (fullStart)
        {
            for (auto& grain : voice.grains)
            {
                if (grain.active && grain.release == 0)
                {
                    grain.release = transientFade;
                    grain.releaseAge = 0;
                }
            }
        }

        auto* slot = &voice.grains[0];

        for (auto& grain : voice.grains)
        {
            if (! grain.active)
            {
                slot = &grain;
                break;
            }

            if (grain.age > slot->age)
                slot = &grain;
        }

        *slot = {};
        slot->active = true;
        slot->position = position;
        slot->fullStart = fullStart;
        slot->fadeIn = crossfade ? transientFade : 0;
    };

    for (auto& voice : voices)
    {
        if (! voice.active)
            continue;

        const auto* s = samples[(size_t) voice.slot].get();

        if (s == nullptr)
        {
            voice.active = false;
            continue;
        }

        // The slice's or pad's own settings, read per sub-block so knob moves affect notes already playing.
        // SPEED is tape-style (pitch and tempo together); PITCH only changes pitch; tempo sync only changes tempo.
        const float pitch = masterPitch + getSetting (voice.settingsIndex, pitchControl);
        const double speed = masterSpeed * getSetting (voice.settingsIndex, speedControl);
        const double sourceBpm = voice.slot == sliceSlot ? (double) loopBpmParam->load() : s->sourceBpm;
        const double tempoRatio = sync && sourceBpm > 0.0 ? hostBpm / sourceBpm : 1.0;
        const double rateToSource = s->sampleRate / currentSampleRate;
        const double timeRate = speed * tempoRatio * rateToSource;
        const double readRate = speed * std::exp2 (pitch / 12.0) * rateToSource;

        const float targetGain = juce::Decibels::decibelsToGain (getSetting (voice.settingsIndex, volumeControl), -60.0f);
        const float gainStep = (targetGain - voice.gain) / (float) juce::jmax (1, numSamples);

        const float cutoff = juce::jmin (getSetting (voice.settingsIndex, cutoffControl), (float) currentSampleRate * 0.45f);
        const bool filtering = cutoff < 19000.0f;

        if (filtering)
            voice.filter.setCutoffFrequency (cutoff);

        const int numSourceChannels = s->buffer.getNumChannels();
        const auto declickLength = (float) juce::jmax (1.0, s->sampleRate * declickSeconds);
        const auto& hits = s->hits;

        for (int i = 0; i < numSamples; ++i)
        {
            if (voice.releasing)
            {
                voice.envelope -= releaseStep;

                if (voice.envelope <= 0.0f)
                {
                    voice.active = false;
                    break;
                }
            }
            else if (voice.envelope < 1.0f)
            {
                voice.envelope = juce::jmin (1.0f, voice.envelope + attackStep);
            }

            // A hit inside the region restarts the grains right on it, so hits stay tight when stretched
            while (voice.nextHit < hits.size() && (double) hits[voice.nextHit] <= voice.timePos)
            {
                const int hit = hits[voice.nextHit++];

                if (hit < voice.end - 1)
                {
                    startGrain (voice, hit, true);
                    voice.grainClock = grainHop;
                }
            }

            if (voice.timePos < voice.end && --voice.grainClock <= 0)
            {
                double position = voice.timePos;

                // When stretching or pitching, nudge the grain to line up with the one it overlaps (WSOLA),
                // which keeps tones smooth. Without either, grains already line up exactly.
                if (std::abs (readRate - timeRate) > 1.0e-6 * readRate)
                {
                    const Grain* previous = nullptr;

                    for (const auto& grain : voice.grains)
                        if (grain.active && grain.release == 0 && (previous == nullptr || grain.age < previous->age))
                            previous = &grain;

                    if (previous != nullptr)
                        position = alignGrain (*s, voice, previous->position, position, readRate);
                }

                startGrain (voice, position, false);
                voice.grainClock = grainHop;
            }

            // Mix the grains. Each one fades in and out (Hann); overlapping by half, they add up to a steady level.
            float mixed[2] = { 0.0f, 0.0f };
            bool anyGrain = false;

            for (auto& grain : voice.grains)
            {
                if (! grain.active)
                    continue;

                const float phase = (float) grain.age / (float) grainLength;
                float weight = grain.fullStart && phase < 0.5f ? 1.0f : 0.5f - 0.5f * std::cos (twoPi * phase);

                if (grain.fadeIn > 0 && grain.age < grain.fadeIn)
                    weight *= (float) grain.age / (float) grain.fadeIn;

                if (grain.release > 0)
                    weight *= 1.0f - (float) grain.releaseAge / (float) grain.release;

                const int index = (int) grain.position;

                if (index >= 0 && index < voice.end - 1)
                {
                    // Fade out just before the region ends, so nothing clicks or spills into the next slice
                    weight *= juce::jmin (1.0f, (float) (voice.end - grain.position) / declickLength);

                    const float frac = (float) (grain.position - index);

                    for (int ch = 0; ch < numOutputChannels; ++ch)
                    {
                        const float* src = s->buffer.getReadPointer (juce::jmin (ch, numSourceChannels - 1));
                        mixed[ch] += weight * (src[index] + frac * (src[index + 1] - src[index]));
                    }
                }

                grain.position += readRate;
                ++grain.age;

                if (grain.age >= grainLength || (grain.release > 0 && ++grain.releaseAge >= grain.release))
                    grain.active = false;

                anyGrain = anyGrain || grain.active;
            }

            voice.timePos += timeRate;

            if (voice.timePos >= voice.end && ! anyGrain)
            {
                voice.active = false;
                break;
            }

            voice.gain += gainStep;
            const float gain = voice.velocityGain * voice.envelope * voice.gain;

            for (int ch = 0; ch < numOutputChannels; ++ch)
            {
                float value = mixed[ch];

                if (filtering)
                    value = voice.filter.processSample (ch, value);

                output.addSample (ch, startSample + i, gain * value);
            }
        }
    }
}

double PadSlicerAudioProcessor::alignGrain (const Sample& s, const Voice& voice, double continuation,
                                           double target, double readRate) const
{
    // Finds the start near `target` whose audio best matches where the previous grain is heading
    constexpr int points = 128;
    const float* data = s.buffer.getReadPointer (0);
    const double step = readRate * 2.0;   // compare every second output sample
    const int end = voice.end;
    const int radius = juce::roundToInt (alignSeconds * s.sampleRate);

    auto sampleAt = [data, end] (double position)
    {
        const int index = (int) position;

        if (index < 0 || index + 1 >= end)
            return 0.0f;

        const float frac = (float) (position - index);
        return data[index] + frac * (data[index + 1] - data[index]);
    };

    std::array<float, points> reference;

    for (int k = 0; k < points; ++k)
        reference[(size_t) k] = sampleAt (continuation + k * step);

    auto score = [&] (double start)
    {
        double dot = 0.0, energy = 1.0e-9;

        for (int k = 0; k < points; ++k)
        {
            const float v = sampleAt (start + k * step);
            dot += v * reference[(size_t) k];
            energy += v * v;
        }

        return dot / std::sqrt (energy);
    };

    const double lowest = voice.start;
    const double highest = end - points * step - 2.0;

    if (highest <= lowest)
        return target;

    double best = juce::jlimit (lowest, highest, target);
    double bestScore = score (best);

    // Coarse search, then refine around the best match
    for (int offset = -radius; offset <= radius; offset += 4)
    {
        const double candidate = target + offset;

        if (candidate < lowest || candidate > highest)
            continue;

        if (const double candidateScore = score (candidate); candidateScore > bestScore)
        {
            bestScore = candidateScore;
            best = candidate;
        }
    }

    const double coarse = best;

    for (int offset = -3; offset <= 3; ++offset)
    {
        const double candidate = coarse + offset;

        if (candidate < lowest || candidate > highest || offset == 0)
            continue;

        if (const double candidateScore = score (candidate); candidateScore > bestScore)
        {
            bestScore = candidateScore;
            best = candidate;
        }
    }

    return best;
}

//==============================================================================
std::unique_ptr<PadSlicerAudioProcessor::Sample> PadSlicerAudioProcessor::readSample (const juce::File& file,
                                                                                      juce::Range<juce::int64> region)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    juce::Range<juce::int64> range (0, reader->lengthInSamples);

    if (! region.isEmpty())
        range = range.getIntersectionWith (region);

    if (range.isEmpty() || range.getLength() > (juce::int64) (reader->sampleRate * maxSampleSeconds))
        return {};

    auto sample = std::make_unique<Sample>();
    const int numChannels = (int) juce::jmin (reader->numChannels, 2u);
    const auto length = (int) range.getLength();

    sample->buffer.setSize (numChannels, length);
    reader->read (&sample->buffer, 0, length, range.getStart(), true, numChannels > 1);
    sample->sampleRate = reader->sampleRate;
    return sample;
}

void PadSlicerAudioProcessor::detectHits (Sample& sample, float sensitivity)
{
    sample.hits = Transients::findHits (sample.buffer, sample.sampleRate, sensitivity, maxSlices);
}

bool PadSlicerAudioProcessor::swapSample (int slot, std::unique_ptr<Sample>& newSample, juce::uint32 generation)
{
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);

        // A newer request owns this slot now
        if (loadGenerations[(size_t) slot] != generation)
            return false;

        std::swap (samples[(size_t) slot], newSample);
        slotChanged[(size_t) slot] = true;
    }

    newSample.reset();   // frees the old buffer here, off the audio thread
    return true;
}

void PadSlicerAudioProcessor::startLoad (int slot, const SlotInfo& info, std::function<void (bool)> onDone)
{
    jassert (slot >= 0 && slot < numSlots);

    const auto generation = ++loadGenerations[(size_t) slot];
    SlotInfo previous;

    {
        const juce::ScopedLock lock (infoLock);
        previous = slotInfos[(size_t) slot];
        slotInfos[(size_t) slot] = info;
    }

    juce::WeakReference<PadSlicerAudioProcessor> weakThis (this);

    loader.addJob ([this, weakThis, slot, info, previous, generation, onDone]
    {
        auto sample = readSample (info.file, info.region);
        const bool ok = sample != nullptr;

        if (ok)
        {
            const int length = sample->buffer.getNumSamples();

            // Hits mark where grains restart (and, for the loop, the slice lines when slicing by hits)
            detectHits (*sample, hitSensParam->load() / 100.0f);
            sample->sourceBpm = info.sourceBpm;

            if (slot == sliceSlot)
                sample->manual = info.manualStarts;

            auto hits = sample->hits;

            const double rate = sample->sampleRate;

            if (swapSample (slot, sample, generation) && slot == sliceSlot)
            {
                const juce::ScopedLock lock (infoLock);
                uiHits = std::move (hits);
                uiSliceLength = length;
                uiSliceRate = rate;
            }
        }
        else if (loadGenerations[(size_t) slot] == generation)
        {
            // Keep showing what is actually still loaded
            const juce::ScopedLock lock (infoLock);
            slotInfos[(size_t) slot] = previous;
        }

        if (onDone != nullptr)
            juce::MessageManager::callAsync ([weakThis, onDone, ok]
            {
                if (weakThis != nullptr)
                    onDone (ok);
            });
    });
}

void PadSlicerAudioProcessor::loadSampleAsync (int slot, const juce::File& file, std::function<void (bool)> onDone)
{
    // A new sample starts with fresh settings
    if (slot == sliceSlot)
    {
        // Hand-placed lines belonged to the old loop
        if (getSliceBy() == SliceBy::manual)
            if (auto* parameter = apvts.getParameter (sliceById))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) SliceBy::grid));

        for (int slice = 0; slice < maxSlices; ++slice)
            resetSettings (sliceSettings (slice));

        markedSlices.fill (false);
        selectedSlice = 0;
    }
    else
    {
        resetSettings (padSettings (slot));
    }

    SlotInfo info;
    info.file = file;

    // A new loop gets its tempo from its length
    if (slot == sliceSlot)
    {
        juce::WeakReference<PadSlicerAudioProcessor> weakThis (this);

        startLoad (slot, info, [weakThis, onDone] (bool ok)
        {
            if (ok && weakThis != nullptr)
                if (const double bpm = weakThis->detectLoopBpm(); bpm > 0.0)
                    if (auto* parameter = weakThis->apvts.getParameter (loopBpmId))
                        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) bpm));

            if (onDone != nullptr)
                onDone (ok);
        });

        return;
    }

    startLoad (slot, info, std::move (onDone));
}

double PadSlicerAudioProcessor::estimateBpm (double seconds)
{
    if (seconds <= 0.0)
        return 0.0;

    double best = 0.0, bestDistance = 1.0e9;

    for (double beats = 1.0; beats <= 64.0; beats *= 2.0)
    {
        const double bpm = beats * 60.0 / seconds;

        if (bpm < 40.0 || bpm > 300.0)
            continue;

        const double distance = std::abs (std::log2 (bpm / 120.0));

        if (distance < bestDistance)
        {
            best = bpm;
            bestDistance = distance;
        }
    }

    return std::round (best * 100.0) / 100.0;
}

double PadSlicerAudioProcessor::detectLoopBpm() const
{
    int length = 0;
    double rate = 0.0;

    {
        const juce::ScopedLock lock (infoLock);
        length = uiSliceLength;
        rate = uiSliceRate;
    }

    return length > 0 && rate > 0.0 ? estimateBpm (length / rate) : 0.0;
}

void PadSlicerAudioProcessor::clearSample (int slot)
{
    jassert (slot >= 0 && slot < numSlots);

    const auto generation = ++loadGenerations[(size_t) slot];

    {
        const juce::ScopedLock lock (infoLock);
        slotInfos[(size_t) slot] = {};

        if (slot == sliceSlot)
        {
            uiHits.clear();
            uiSliceLength = 0;
        }
    }

    loader.addJob ([this, slot, generation]
    {
        std::unique_ptr<Sample> empty;
        swapSample (slot, empty, generation);
    });
}

PadSlicerAudioProcessor::SlotInfo PadSlicerAudioProcessor::getSlotInfo (int slot) const
{
    const juce::ScopedLock lock (infoLock);
    return slotInfos[(size_t) slot];
}

PadSlicerAudioProcessor::SliceLayout PadSlicerAudioProcessor::getSliceLayout() const
{
    SliceLayout layout;
    std::vector<int> hits, manual;

    {
        const juce::ScopedLock lock (infoLock);
        hits = uiHits;
        manual = slotInfos[(size_t) sliceSlot].manualStarts;
        layout.length = uiSliceLength;
        layout.sampleRate = uiSliceRate;
    }

    const auto sliceBy = getSliceBy();
    const int gridSlices = (int) slicesParam->load();
    const int count = sliceCount (layout.length, sliceBy, gridSlices, hits, manual);

    for (int i = 0; i < count; ++i)
        layout.slices.push_back (sliceRegion (i, layout.length, sliceBy, gridSlices, hits, manual));

    return layout;
}

void PadSlicerAudioProcessor::setManualSlices (std::vector<int> starts)
{
    int length = 0;

    {
        const juce::ScopedLock lock (infoLock);
        length = uiSliceLength;
    }

    // Keep the lines in order, inside the loop, and not on top of each other
    std::sort (starts.begin(), starts.end());
    std::vector<int> cleaned;

    for (const int start : starts)
        if (start >= 0 && start < length && (cleaned.empty() || start - cleaned.back() >= 16) && (int) cleaned.size() < maxSlices)
            cleaned.push_back (start);

    {
        const juce::ScopedLock lock (infoLock);
        slotInfos[(size_t) sliceSlot].manualStarts = cleaned;
    }

    // Hand the new lines to the audio thread; the old list is freed here, not there
    {
        const juce::SpinLock::ScopedLockType lock (sampleLock);

        if (auto* loop = samples[(size_t) sliceSlot].get())
            loop->manual.swap (cleaned);
    }

    if (getSliceBy() != SliceBy::manual)
        if (auto* parameter = apvts.getParameter (sliceById))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) SliceBy::manual));
}

void PadSlicerAudioProcessor::sendSlicesToPads (const juce::Array<int>& slices, std::function<void (int)> onDone)
{
    const auto source = getSlotInfo (sliceSlot);
    const auto layout = getSliceLayout();

    struct Transfer
    {
        int pad;
        juce::Range<int> region;
        juce::uint32 generation;
        double bpm;
    };

    std::vector<Transfer> transfers;
    int pad = 0;

    for (const int slice : slices)
    {
        if (source.isEmpty() || slice < 0 || slice >= (int) layout.slices.size())
            continue;

        // Only fill empty pads, so nothing already in the kit gets replaced
        while (pad < numPads && ! getSlotInfo (pad).isEmpty())
            ++pad;

        if (pad >= numPads)
            break;

        const auto region = layout.slices[(size_t) slice];
        const auto generation = ++loadGenerations[(size_t) pad];

        SlotInfo info;
        info.file = source.file;
        info.region = { source.region.getStart() + region.getStart(), source.region.getStart() + region.getEnd() };
        info.chopNumber = slice + 1;
        info.sourceBpm = loopBpmParam->load();

        {
            const juce::ScopedLock lock (infoLock);
            slotInfos[(size_t) pad] = info;
        }

        // The pad keeps the slice's own start, pitch, speed, cutoff and volume
        for (int c = 0; c < numSlotControls; ++c)
            setSetting (padSettings (pad), (SlotControl) c, getSetting (sliceSettings (slice), (SlotControl) c));

        transfers.push_back ({ pad, region, generation, info.sourceBpm });
        ++pad;
    }

    const auto loopGeneration = loadGenerations[(size_t) sliceSlot].load();
    juce::WeakReference<PadSlicerAudioProcessor> weakThis (this);

    loader.addJob ([this, weakThis, transfers, loopGeneration, onDone]
    {
        // Only this thread swaps samples, so the loop can be read directly
        const auto* loop = samples[(size_t) sliceSlot].get();
        const bool loopIsCurrent = loop != nullptr && loadGenerations[(size_t) sliceSlot] == loopGeneration;
        int sent = 0;

        for (const auto& transfer : transfers)
        {
            std::unique_ptr<Sample> chop;

            if (loopIsCurrent && ! transfer.region.isEmpty() && transfer.region.getEnd() <= loop->buffer.getNumSamples())
            {
                chop = std::make_unique<Sample>();
                chop->sampleRate = loop->sampleRate;
                chop->buffer.setSize (loop->buffer.getNumChannels(), transfer.region.getLength());

                for (int ch = 0; ch < loop->buffer.getNumChannels(); ++ch)
                    chop->buffer.copyFrom (ch, 0, loop->buffer, ch, transfer.region.getStart(), transfer.region.getLength());

                chop->sourceBpm = transfer.bpm;

                for (const int hit : loop->hits)
                    if (hit > transfer.region.getStart() && hit < transfer.region.getEnd())
                        chop->hits.push_back (hit - transfer.region.getStart());
            }

            if (chop != nullptr && swapSample (transfer.pad, chop, transfer.generation))
            {
                ++sent;
            }
            else if (loadGenerations[(size_t) transfer.pad] == transfer.generation)
            {
                const juce::ScopedLock lock (infoLock);
                slotInfos[(size_t) transfer.pad] = {};
            }
        }

        if (onDone != nullptr)
            juce::MessageManager::callAsync ([weakThis, onDone, sent]
            {
                if (weakThis != nullptr)
                    onDone (sent);
            });
    });
}

void PadSlicerAudioProcessor::timerCallback()
{
    // Re-detect the loop's hits when the sensitivity changes
    const float sensitivity = hitSensParam->load() / 100.0f;

    if (juce::approximatelyEqual (sensitivity, detectedSensitivity))
        return;

    detectedSensitivity = sensitivity;
    const auto generation = loadGenerations[(size_t) sliceSlot].load();

    loader.addJob ([this, sensitivity, generation]
    {
        // Only this thread swaps samples, so the loop can be read directly
        auto* loop = samples[(size_t) sliceSlot].get();

        if (loop == nullptr || loadGenerations[(size_t) sliceSlot] != generation)
            return;

        auto hits = Transients::findHits (loop->buffer, loop->sampleRate, sensitivity, maxSlices);
        auto uiCopy = hits;

        {
            const juce::SpinLock::ScopedLockType lock (sampleLock);
            loop->hits.swap (hits);   // the old list is freed below, off the audio thread
        }

        const juce::ScopedLock lock (infoLock);
        uiHits = std::move (uiCopy);
    });
}

//==============================================================================
bool PadSlicerAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PadSlicerAudioProcessor::createEditor()
{
    return new PadSlicerAudioProcessorEditor (*this);
}

//==============================================================================
void PadSlicerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (uiScaleProperty, uiScale.load(), nullptr);

    juce::ValueTree slotsTree (slotsTag);

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto info = getSlotInfo (slot);

        if (info.isEmpty())
            continue;

        juce::ValueTree slotTree (slotTag);
        slotTree.setProperty ("index", slot, nullptr);
        slotTree.setProperty ("path", info.file.getFullPathName(), nullptr);
        slotTree.setProperty ("start", info.region.getStart(), nullptr);
        slotTree.setProperty ("end", info.region.getEnd(), nullptr);
        slotTree.setProperty ("chop", info.chopNumber, nullptr);

        if (info.sourceBpm > 0.0)
            slotTree.setProperty ("bpm", info.sourceBpm, nullptr);

        if (! info.manualStarts.empty())
        {
            juce::StringArray lines;

            for (const int start : info.manualStarts)
                lines.add (juce::String (start));

            slotTree.setProperty ("manual", lines.joinIntoString (","), nullptr);
        }
        slotsTree.appendChild (slotTree, nullptr);
    }

    state.appendChild (slotsTree, nullptr);

    juce::ValueTree settingsTree (settingsTag);

    for (int index = 0; index < numSettingSlots; ++index)
    {
        bool changed = false;

        for (int c = 0; c < numSlotControls; ++c)
            changed = changed || ! juce::approximatelyEqual (getSetting (index, (SlotControl) c), defaultSetting ((SlotControl) c));

        if (! changed)
            continue;

        juce::ValueTree settingTree (settingTag);
        settingTree.setProperty ("index", index, nullptr);

        for (int c = 0; c < numSlotControls; ++c)
            settingTree.setProperty (controlProperties[c], getSetting (index, (SlotControl) c), nullptr);

        settingsTree.appendChild (settingTree, nullptr);
    }

    state.appendChild (settingsTree, nullptr);

    juce::ValueTree generatorTree (generatorTag);
    generatorTree.setProperty ("seed", generatorSettings.seed, nullptr);

    for (int c = 0; c < Generator::numControls; ++c)
        generatorTree.setProperty (juce::String ("c") + juce::String (c), generatorSettings.values[(size_t) c], nullptr);

    for (const auto& note : generatorSettings.added)
    {
        juce::ValueTree added ("ADD");
        added.setProperty ("start", note.start, nullptr);
        added.setProperty ("length", note.length, nullptr);
        added.setProperty ("note", note.note, nullptr);
        added.setProperty ("velocity", note.velocity, nullptr);
        generatorTree.appendChild (added, nullptr);
    }

    for (const auto& key : generatorSettings.removed)
    {
        juce::ValueTree removed ("DEL");
        removed.setProperty ("start", key.start, nullptr);
        removed.setProperty ("note", key.note, nullptr);
        generatorTree.appendChild (removed, nullptr);
    }

    state.appendChild (generatorTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PadSlicerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    std::array<SlotInfo, numSlots> restored;

    if (auto slotsTree = state.getChildWithName (slotsTag); slotsTree.isValid())
    {
        for (const auto& slotTree : slotsTree)
        {
            const int slot = slotTree.getProperty ("index", -1);

            if (slot < 0 || slot >= numSlots)
                continue;

            auto& info = restored[(size_t) slot];
            info.file = juce::File (slotTree.getProperty ("path").toString());
            info.region = { (juce::int64) slotTree.getProperty ("start", 0), (juce::int64) slotTree.getProperty ("end", 0) };
            info.chopNumber = slotTree.getProperty ("chop", 0);
            info.sourceBpm = slotTree.getProperty ("bpm", 0.0);

            for (const auto& line : juce::StringArray::fromTokens (slotTree.getProperty ("manual").toString(), ",", {}))
                info.manualStarts.push_back (line.getIntValue());
        }
    }
    else
    {
        // Sets saved by earlier versions stored paths as properties
        const auto legacyPath = [&state] (const juce::Identifier& name) { return state.getProperty (name).toString(); };

        if (legacyPath ("samplePath").isNotEmpty())
            restored[(size_t) sliceSlot].file = juce::File (legacyPath ("samplePath"));

        for (int pad = 0; pad < numPads; ++pad)
            if (const auto path = legacyPath ("pad" + juce::String (pad)); path.isNotEmpty())
                restored[(size_t) pad].file = juce::File (path);
    }

    for (int index = 0; index < numSettingSlots; ++index)
        resetSettings (index);

    if (auto settingsTree = state.getChildWithName (settingsTag); settingsTree.isValid())
    {
        for (const auto& settingTree : settingsTree)
        {
            const int index = settingTree.getProperty ("index", -1);

            if (index < 0 || index >= numSettingSlots)
                continue;

            for (int c = 0; c < numSlotControls; ++c)
                setSetting (index, (SlotControl) c,
                            (float) settingTree.getProperty (controlProperties[c], defaultSetting ((SlotControl) c)));
        }
    }

    uiScale = (float) state.getProperty (uiScaleProperty, 1.0f);

    generatorSettings = {};

    if (auto generatorTree = state.getChildWithName (generatorTag); generatorTree.isValid())
    {
        generatorSettings.seed = (juce::int64) generatorTree.getProperty ("seed", 0);

        for (int c = 0; c < Generator::numControls; ++c)
            generatorSettings.values[(size_t) c] = (float) generatorTree.getProperty (juce::String ("c") + juce::String (c),
                                                                                       Generator::getControlInfo (c).defaultValue);

        for (const auto& edit : generatorTree)
        {
            if (edit.hasType ("ADD"))
                generatorSettings.added.push_back ({ (double) edit.getProperty ("start"), (double) edit.getProperty ("length"),
                                                     (int) edit.getProperty ("note"), (float) edit.getProperty ("velocity", 0.8f) });
            else if (edit.hasType ("DEL"))
                generatorSettings.removed.push_back ({ (double) edit.getProperty ("start"), (int) edit.getProperty ("note") });
        }
    }

    // Keep only the parameters in the APVTS state
    state.removeChild (state.getChildWithName (slotsTag), nullptr);
    state.removeChild (state.getChildWithName (settingsTag), nullptr);
    state.removeChild (state.getChildWithName (generatorTag), nullptr);
    state.removeProperty (uiScaleProperty, nullptr);
    state.removeProperty ("samplePath", nullptr);

    for (int pad = 0; pad < numPads; ++pad)
        state.removeProperty ("pad" + juce::String (pad), nullptr);

    apvts.replaceState (state);

    for (int slot = 0; slot < numSlots; ++slot)
    {
        const auto& info = restored[(size_t) slot];

        if (info.isEmpty() || ! info.file.existsAsFile())
            clearSample (slot);
        else
            startLoad (slot, info, {});
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PadSlicerAudioProcessor();
}
