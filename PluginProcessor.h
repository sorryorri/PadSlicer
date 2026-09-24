#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

#include "Effects.h"
#include "Generator.h"
#include "KnightProgress.h"

//==============================================================================
// Two ways to play, both starting at MIDI note 36 (C1 in Ableton, the bottom-left pad):
//  - Slice mode: one loop cut into slices (an even grid or its detected hits), slice N on note 36 + N.
//  - Kit mode:   16 pads, each holding its own one-shot sample, like a Drum Rack.
// Every slice and pad has its own start/pitch/speed/cutoff/volume; the master parameters act on top.
class PadSlicerAudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer
{
public:
    static constexpr int firstNote = 36;
    static constexpr int maxSlices = 64;
    static constexpr int numPads = 16;
    static constexpr int maxVoices = 16;

    // Sample slots: 0..numPads-1 are the kit pads, sliceSlot is the loop that gets sliced
    static constexpr int sliceSlot = numPads;
    static constexpr int numSlots = numPads + 1;

    enum class Mode { slice = 0, kit = 1 };
    enum class Trigger { oneShot = 0, gate = 1 };
    enum class SliceBy { grid = 0, hits = 1, manual = 2 };

    // Parameter IDs, shared with the editor
    static constexpr const char* modeId     = "mode";
    static constexpr const char* triggerId  = "trigger";
    static constexpr const char* slicesId   = "slices";
    static constexpr const char* sliceById  = "sliceBy";
    static constexpr const char* hitSensId  = "hitSens";
    static constexpr const char* startId    = "start";
    static constexpr const char* cutoffId   = "cutoff";
    static constexpr const char* speedId    = "speed";
    static constexpr const char* pitchId    = "pitch";
    static constexpr const char* volumeId   = "volume";
    static constexpr const char* syncId     = "sync";
    static constexpr const char* loopBpmId  = "loopBpm";

    // FX parameter IDs, in signal-chain order. Each effect has an on switch and four
    // controls; the last control is always the mix.
    struct FxIds
    {
        const char* on;
        std::array<const char*, 4> controls;
    };

    static constexpr FxIds chorusIds  { "chorusOn",  { { "chorusMode",  "chorusRate",      "chorusDepth",     "chorusMix"  } } };
    static constexpr FxIds flangerIds { "flangerOn", { { "flangerRate", "flangerDepth",    "flangerFeedback", "flangerMix" } } };
    static constexpr FxIds echoIds    { "echoOn",    { { "echoTime",    "echoFeedback",    "echoTape",        "echoMix"    } } };
    static constexpr FxIds plateIds   { "plateOn",   { { "plateDecay",  "plateTone",       "platePredelay",   "plateMix"   } } };

    //==============================================================================
    // Per-slice and per-pad settings. Each one uses the same range as its master parameter.
    enum SlotControl { startControl, pitchControl, speedControl, cutoffControl, volumeControl, numSlotControls };

    static constexpr int numSettingSlots = maxSlices + numPads;
    static int sliceSettings (int slice)  { return slice; }
    static int padSettings (int pad)      { return maxSlices + pad; }
    static const char* masterParameterFor (SlotControl);
    static float defaultSetting (SlotControl);

    float getSetting (int settingsIndex, SlotControl) const;
    void setSetting (int settingsIndex, SlotControl, float value);
    void resetSettings (int settingsIndex);

    //==============================================================================
    PadSlicerAudioProcessor();
    ~PadSlicerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // What each slot holds. A region means only part of the file (a chop sent from the slicer).
    struct SlotInfo
    {
        juce::File file;
        juce::Range<juce::int64> region;
        int chopNumber = 0;
        std::vector<int> manualStarts;   // slice lines placed by hand (slicer only)
        double sourceBpm = 0.0;          // tempo of the loop a chop came from; 0 = not tempo-synced

        bool isEmpty() const { return file == juce::File(); }
        juce::String getLabel() const;
    };

    // Files are read on a background thread, so Ableton's UI never waits on the disk.
    // The slot shows the new file straight away; onDone (message thread) says whether it worked.
    void loadSampleAsync (int slot, const juce::File& file, std::function<void (bool)> onDone = {});
    void clearSample (int slot);
    SlotInfo getSlotInfo (int slot) const;
    juce::File getSampleFile (int slot) const   { return getSlotInfo (slot).file; }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // Where each slice of the loop starts and ends, in samples, as the editor should draw it
    struct SliceLayout
    {
        std::vector<juce::Range<int>> slices;
        int length = 0;
        double sampleRate = 44100.0;
    };

    SliceLayout getSliceLayout() const;

    // Guesses the loop's tempo from its length (a power-of-two number of beats, as close to 120 BPM as possible)
    double detectLoopBpm() const;
    static double estimateBpm (double seconds);

    // Sets the slicer's hand-placed slice lines (start samples) and switches to the EDIT layout
    void setManualSlices (std::vector<int> starts);

    // Copies slices of the loop onto the empty pads, keeping their settings. onDone gets how many were sent.
    void sendSlicesToPads (const juce::Array<int>& slices, std::function<void (int sent)> onDone);

    Mode getMode() const       { return (Mode) juce::roundToInt (modeParam->load()); }
    Trigger getTrigger() const { return (Trigger) juce::roundToInt (triggerParam->load()); }
    SliceBy getSliceBy() const { return (SliceBy) juce::roundToInt (sliceByParam->load()); }

    // Read by the editor for its displays
    juce::uint32 getHitCount() const  { return hitCount.load(); }
    int getLastHitNote() const        { return lastHitNote.load(); }
    int getActiveVoiceCount() const   { return activeVoiceCount.load(); }
    float takeOutputPeak()            { return outputPeak.exchange (0.0f); }
    double getHostSampleRate() const  { return currentSampleRate; }
    KnightProgress& getKnightProgress() { return knightProgress; }

    //==============================================================================
    // Pattern generator (GEN page). Settings are only touched on the message thread.
    Generator::Settings generatorSettings;

    // Plays a generated pattern through the plugin, following the host's position while it plays
    void setPreviewPattern (const Generator::Pattern&);
    std::atomic<bool> previewOn { false };
    double getPreviewPosition() const { return previewPosition.load(); }
    double getLastBpm() const         { return lastBpm.load(); }

    // Editor state that should survive closing the window
    std::atomic<int> selectedSlice { 0 }, selectedPad { 0 };
    std::atomic<float> uiScale { 1.0f };
    std::array<bool, maxSlices> markedSlices {};   // message thread only

    juce::AudioProcessorValueTreeState apvts;

    // Lets the editor audition pads, and shows which notes are held
    juce::MidiKeyboardState keyboardState;

private:
    //==============================================================================
    struct Sample
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 44100.0;
        std::vector<int> hits;     // detected transients: slice lines when slicing by hits, and where grains restart
        std::vector<int> manual;   // slice lines placed by hand
        double sourceBpm = 0.0;    // for pads holding a chop: the tempo it was cut at
    };

    // Voices play through short overlapping grains, so pitch and tempo can change independently.
    // With no pitch shift and no tempo change the grains line up exactly and the sound is untouched.
    struct Grain
    {
        bool active = false;
        double position = 0.0;    // read position in source samples
        int age = 0;              // output samples since it started
        bool fullStart = false;   // starts at full level, to keep a hit's attack
        int fadeIn = 0;           // when > 0, fades in over this many samples (crossfading from the grains before)
        int release = 0;          // when > 0, fading out quickly over this many samples
        int releaseAge = 0;
    };

    struct Voice
    {
        bool active = false;
        bool releasing = false;
        int note = -1;
        int slot = 0;             // which sample slot this voice reads from
        int settingsIndex = 0;    // which slice or pad settings apply
        double timePos = 0.0;     // where the voice is in the source; moves at the tempo rate
        int start = 0;            // where it started playing
        int end = 0;              // exclusive end of the region being played
        std::array<Grain, 4> grains;
        int grainClock = 0;       // output samples until the next grain
        size_t nextHit = 0;       // next transient (index into the sample's hits) where grains restart
        float velocityGain = 0.0f;
        float envelope = 0.0f;    // short attack/release ramp to avoid clicks
        float gain = 1.0f;        // per-slot volume, ramped between blocks
        juce::uint32 age = 0;
        juce::dsp::StateVariableTPTFilter<float> filter;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static const std::vector<int>* sliceLines (SliceBy, const std::vector<int>& hits, const std::vector<int>& manual);
    static juce::Range<int> sliceRegion (int index, int length, SliceBy, int gridSlices,
                                         const std::vector<int>& hits, const std::vector<int>& manual);
    static int sliceCount (int length, SliceBy, int gridSlices, const std::vector<int>& hits, const std::vector<int>& manual);

    void timerCallback() override;
    std::unique_ptr<Sample> readSample (const juce::File&, juce::Range<juce::int64> region);
    void startLoad (int slot, const SlotInfo&, std::function<void (bool)> onDone);
    bool swapSample (int slot, std::unique_ptr<Sample>& newSample, juce::uint32 generation);
    void detectHits (Sample&, float sensitivity);

    void handleMidiMessage (const juce::MidiMessage&);
    void startVoice (int note, float velocity, bool earnsXp);
    void releaseNote (int note);
    void releaseAllVoices();
    void renderVoices (juce::AudioBuffer<float>&, int startSample, int numSamples);
    double alignGrain (const Sample&, const Voice&, double continuation, double target, double readRate) const;
    void processEffects (juce::AudioBuffer<float>&, int numSamples);
    void addPreviewNotes (juce::MidiBuffer&, int numSamples);
    double getHostBpm() const;

    juce::AudioFormatManager formatManager;

    // Only the loader thread swaps samples; the audio thread only try-locks, so it never waits
    std::array<std::unique_ptr<Sample>, numSlots> samples;
    std::array<bool, numSlots> slotChanged {};   // guarded by sampleLock
    juce::SpinLock sampleLock;

    std::array<SlotInfo, numSlots> slotInfos;    // guarded by infoLock
    std::vector<int> uiHits;                     // guarded by infoLock
    int uiSliceLength = 0;                       // guarded by infoLock
    double uiSliceRate = 44100.0;                // guarded by infoLock
    juce::CriticalSection infoLock;

    // Bumped for every new request, so a slow old load can never overwrite a newer one
    std::array<std::atomic<juce::uint32>, numSlots> loadGenerations {};
    float detectedSensitivity = -1.0f;           // message thread only

    std::array<std::array<std::atomic<float>, numSlotControls>, numSettingSlots> settings;

    std::array<Voice, maxVoices> voices;
    juce::uint32 voiceCounter = 0;

    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> cutoffSmoothed;
    juce::SmoothedValue<float> gainSmoothed;

    double currentSampleRate = 44100.0;
    int grainLength = 2048, grainHop = 1024, transientFade = 128;   // in output samples
    float attackStep = 1.0f;
    float releaseStep = 1.0f;

    std::atomic<juce::uint32> hitCount { 0 };
    std::atomic<int> lastHitNote { -1 };
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<float> outputPeak { 0.0f };

    KnightProgress knightProgress;

    // The generator's preview plays on its own MIDI channel, so its notes don't earn the knight XP
    static constexpr int previewChannel = 16;

    std::unique_ptr<Generator::Pattern> previewPattern;   // guarded by patternLock
    juce::SpinLock patternLock;
    double previewBeat = 0.0;
    bool previewWasOn = false;
    std::atomic<double> previewPosition { 0.0 };
    std::atomic<double> lastBpm { 120.0 };

    std::atomic<float>* modeParam     = nullptr;
    std::atomic<float>* triggerParam  = nullptr;
    std::atomic<float>* slicesParam   = nullptr;
    std::atomic<float>* sliceByParam  = nullptr;
    std::atomic<float>* hitSensParam  = nullptr;
    std::atomic<float>* startParam    = nullptr;
    std::atomic<float>* cutoffParam   = nullptr;
    std::atomic<float>* speedParam    = nullptr;
    std::atomic<float>* pitchParam    = nullptr;
    std::atomic<float>* volumeParam   = nullptr;
    std::atomic<float>* syncParam     = nullptr;
    std::atomic<float>* loopBpmParam  = nullptr;

    struct FxParams
    {
        std::atomic<float>* on = nullptr;
        std::array<std::atomic<float>*, 4> controls {};
    };

    FxParams chorusParams, flangerParams, echoParams, plateParams;

    fx::Chorus chorus;
    fx::Flanger flanger;
    fx::TapeEcho echo;
    fx::PlateReverb plate;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PadSlicerAudioProcessor)

    // Declared last so it is destroyed first: pending loads finish before anything they touch goes away
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withThreadName ("PadSlicer loader").withNumberOfThreads (1) };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadSlicerAudioProcessor)
};
