#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Knight.h"
#include "PluginProcessor.h"
#include "Retro.h"

//==============================================================================
// 4x4 Drum Rack style pad grid for Kit mode, drawn on the LCD. Pad 1 (note 36) is bottom-left.
// Click to select and audition, drop files to load (several files fill consecutive pads),
// right-click to load or clear.
class PadGrid final : public juce::Component,
                      public juce::FileDragAndDropTarget
{
public:
    explicit PadGrid (PadSlicerAudioProcessor&);

    void setLitPads (juce::uint32 mask);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Loads files onto consecutive pads, starting at firstPad
    void loadFiles (const juce::StringArray& files, int firstPad);

    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onPadSelected;

private:
    int padAt (juce::Point<int> position) const;
    juce::Rectangle<float> padBounds (int pad) const;
    void showPadMenu (int pad);

    PadSlicerAudioProcessor& processorRef;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::uint32 litPads = 0;
    int heldPad = -1;
    int dragPad = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
};

//==============================================================================
// The FX tab: four effect modules drawn on the LCD, left to right in signal-chain order.
class FxPanel final : public juce::Component
{
public:
    explicit FxPanel (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Dims the knobs of effects that are switched off
    void updateEnabledStates();

private:
    struct MiniKnob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Module
    {
        juce::String title;
        std::atomic<float>* on = nullptr;
        Retro::LedButton power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
        std::array<MiniKnob, 4> knobs;
    };

    std::array<Module, 4> modules;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxPanel)
};

//==============================================================================
// The whole interface, laid out at a fixed design size. The editor scales it.
class PadSlicerUI final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    static constexpr int designWidth = 900;
    static constexpr int designHeight = 600;

    explicit PadSlicerUI (PadSlicerAudioProcessor&);
    ~PadSlicerUI() override;

    // Called with the size picked from the size menu (1.0 = 100%)
    std::function<void (float)> onScaleChosen;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    using Processor = PadSlicerAudioProcessor;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    enum class Page { slicer, pads, fx };

    void timerCallback() override;

    void renderBackground (float scale);
    void drawKnightScene (juce::Graphics&);
    void drawLcd (juce::Graphics&);
    void drawWaveform (juce::Graphics&, juce::Rectangle<float> area);
    void drawMeter (juce::Graphics&);
    void drawStatusBar (juce::Graphics&);

    void setPage (Page);
    void updatePage();
    void setMode (Processor::Mode);
    void toggleChoice (const char* paramId);

    // The knobs edit the selected slice or pad, or the master parameters
    int getEditTarget() const;
    void bindKnobs (bool force = false);
    void refreshKnobValues();
    void updateEditChips();

    void chooseFiles();
    void loadSliceFile (const juce::File& file);
    void loadKitFiles (const juce::Array<juce::File>& files);
    void sendToPads();
    void showSizeMenu();
    void showSliceMenu();
    void setStatus (const juce::String& message);

    juce::Rectangle<float> waveformArea() const;
    juce::Rectangle<float> markStripArea() const;
    int sliceAt (float x, const Processor::SliceLayout&) const;
    bool isNoteLit (int note) const;

    Processor& processorRef;
    Retro::LookAndFeel lookAndFeel;

    juce::AudioThumbnailCache thumbnailCache { 1 };
    juce::AudioThumbnail thumbnail;

    Retro::ArcadeButton transferButton { "TRANSFER", Retro::Palette::mint };
    Retro::ArcadeButton loadButton    { "LOAD", Retro::Palette::gold };
    Retro::ArcadeButton triggerButton { "TRIG", Retro::Palette::coral };
    Retro::TabButton slicerTab { "SLICER" }, padsTab { "PADS" }, fxTab { "FX" };
    Retro::ChipButton gridChip { "GRID", Retro::ChipButton::Style::lcd }, hitsChip { "HITS", Retro::ChipButton::Style::lcd };
    Retro::ChipButton slotChip { "", Retro::ChipButton::Style::panel }, masterChip { "MASTER", Retro::ChipButton::Style::panel };
    Retro::ChipButton sizeChip { "", Retro::ChipButton::Style::menu };
    std::unique_ptr<juce::FileChooser> chooser;

    PadGrid padGrid;
    FxPanel fxPanel;
    std::array<Knob, 6> knobs;

    // Layout
    juce::Rectangle<int> titleArea, sceneArea, bezelArea, lcdArea, lcdContentArea, knobPanelArea, meterArea, statusArea;
    juce::Image background;
    float backgroundScale = 0.0f;

    // Display state
    Page page = Page::slicer;
    Processor::Mode shownMode = Processor::Mode::slice;
    juce::File shownFile;
    juce::uint32 lastHitCount = 0;
    int flashNote = -1;
    double flashTime = 0.0;
    float meterLevel = 0.0f;
    int heldSliceNote = -1;
    int shownLevel = 0;
    double levelUpTime = -1.0e9;
    juce::String statusMessage { "READY." };
    double statusTime = 0.0;

    // What the knobs are currently connected to
    bool editMaster = false;
    int boundTarget = -2;
    Processor::SliceBy boundSliceBy = Processor::SliceBy::grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadSlicerUI)
};

//==============================================================================
// Hosts the interface and scales it to the size picked in the size menu.
class PadSlicerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PadSlicerAudioProcessorEditor (PadSlicerAudioProcessor&);

    void resized() override;
    void setUiScale (float scale);

private:
    PadSlicerAudioProcessor& processorRef;
    PadSlicerUI ui;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadSlicerAudioProcessorEditor)
};
