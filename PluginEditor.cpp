#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    using namespace Retro;
    using Processor = PadSlicerAudioProcessor;

    bool isAudioFile (Processor& processor, const juce::File& file)
    {
        return processor.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr;
    }

    double nowMs()
    {
        return juce::Time::getMillisecondCounterHiRes();
    }

    void setChoice (Processor& processor, const char* paramId, int index)
    {
        if (auto* parameter = processor.apvts.getParameter (paramId))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
            parameter->endChangeGesture();
        }
    }

    juce::String twoDigits (int number)
    {
        return juce::String (number).paddedLeft ('0', 2);
    }

    constexpr double flashMs = 160.0;
    constexpr double attackMs = 220.0;
    constexpr double statusMs = 4000.0;
    constexpr double levelUpMs = 2600.0;

    constexpr int tabWidth = 80;
    constexpr int tabHeight = 26;

    constexpr std::array<float, 5> uiSizes { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
}

//==============================================================================
PadGrid::PadGrid (Processor& p)
    : processorRef (p)
{
}

void PadGrid::setLitPads (juce::uint32 mask)
{
    litPads = mask;
}

juce::Rectangle<float> PadGrid::padBounds (int pad) const
{
    const float cellWidth  = (float) getWidth() / 4.0f;
    const float cellHeight = (float) getHeight() / 4.0f;
    const int column = pad % 4;
    const int row = 3 - pad / 4;   // pad 0 sits bottom-left

    return juce::Rectangle<float> ((float) column * cellWidth, (float) row * cellHeight, cellWidth, cellHeight)
               .reduced (3.0f).toNearestInt().toFloat();
}

int PadGrid::padAt (juce::Point<int> position) const
{
    for (int pad = 0; pad < Processor::numPads; ++pad)
        if (padBounds (pad).contains (position.toFloat()))
            return pad;

    return -1;
}

void PadGrid::paint (juce::Graphics& g)
{
    const int selected = processorRef.selectedPad.load();

    for (int pad = 0; pad < Processor::numPads; ++pad)
    {
        const auto bounds = padBounds (pad);
        const int note = Processor::firstNote + pad;
        const auto info = processorRef.getSlotInfo (pad);
        const bool loaded = ! info.isEmpty();
        const bool lit = (litPads >> pad) & 1;

        if (lit)
            g.setColour (Palette::lcdBright);
        else
            g.setColour (loaded ? Palette::lcdDim.withAlpha (0.55f) : juce::Colours::transparentBlack);

        g.fillRect (bounds);

        g.setColour (pad == dragPad || lit || pad == selected ? Palette::lcdBright : (loaded ? Palette::lcdMid : Palette::lcdDim));
        g.drawRect (bounds, 2.0f);

        // The selected pad (the one the knobs edit) gets a second outline
        if (pad == selected)
            g.drawRect (bounds.reduced (4.0f), 1.0f);

        const auto ink = lit ? Palette::lcdBackground : Palette::lcdBright;
        auto textArea = bounds.reduced (7.0f, 6.0f);

        drawPixelText (g, twoDigits (pad + 1) + " " + juce::MidiMessage::getMidiNoteName (note, true, true, 3),
                       textArea.removeFromTop (8.0f), 1, lit ? ink : Palette::lcdMid, juce::Justification::centredLeft);

        drawPixelText (g, loaded ? info.getLabel() : juce::String ("EMPTY"), textArea, 2,
                       loaded ? ink : Palette::lcdDim, juce::Justification::centred);
    }
}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());

    if (pad < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        showPadMenu (pad);
        return;
    }

    processorRef.selectedPad = pad;

    if (onPadSelected != nullptr)
        onPadSelected();

    heldPad = pad;
    processorRef.keyboardState.noteOn (1, Processor::firstNote + pad, 1.0f);
    repaint();
}

void PadGrid::mouseUp (const juce::MouseEvent&)
{
    if (heldPad >= 0)
        processorRef.keyboardState.noteOff (1, Processor::firstNote + heldPad, 0.0f);

    heldPad = -1;
}

void PadGrid::showPadMenu (int pad)
{
    juce::PopupMenu menu;
    menu.addItem (1, "Load Sample...");
    menu.addItem (2, "Clear Pad", ! processorRef.getSlotInfo (pad).isEmpty());
    menu.addSeparator();
    menu.addItem (3, "Clear All Pads");

    juce::Component::SafePointer<PadGrid> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safeThis, pad] (int result)
    {
        if (safeThis == nullptr)
            return;

        auto& self = *safeThis;

        if (result == 1)
        {
            self.chooser = std::make_unique<juce::FileChooser> ("Load a sample onto pad " + juce::String (pad + 1),
                                                                self.processorRef.getSampleFile (pad).getParentDirectory(),
                                                                self.processorRef.getFormatManager().getWildcardForAllFormats());

            self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                       [safeThis, pad] (const juce::FileChooser& fc)
                                       {
                                           if (safeThis != nullptr && fc.getResult() != juce::File())
                                               safeThis->loadFiles ({ fc.getResult().getFullPathName() }, pad);
                                       });
        }
        else if (result == 2 || result == 3)
        {
            for (int p = 0; p < Processor::numPads; ++p)
                if (result == 3 || p == pad)
                    self.processorRef.clearSample (p);

            if (self.onStatus != nullptr)
                self.onStatus (result == 3 ? "CLEARED ALL PADS" : "CLEARED PAD " + juce::String (pad + 1));
        }
    });
}

void PadGrid::loadFiles (const juce::StringArray& files, int firstPad)
{
    struct Progress
    {
        int pending = 0, loaded = 0;
        juce::String lastFailure;
    };

    auto progress = std::make_shared<Progress>();
    juce::Component::SafePointer<PadGrid> safeThis (this);
    int pad = firstPad;
    juce::String firstName;

    for (const auto& path : files)
    {
        const juce::File file (path);

        if (pad < 0 || pad >= Processor::numPads)
            break;

        if (! isAudioFile (processorRef, file))
            continue;

        if (firstName.isEmpty())
            firstName = file.getFileName();

        ++progress->pending;

        processorRef.loadSampleAsync (pad++, file, [safeThis, progress, name = file.getFileName()] (bool ok)
        {
            --progress->pending;

            if (ok)
                ++progress->loaded;
            else
                progress->lastFailure = name;

            if (progress->pending > 0 || safeThis == nullptr || safeThis->onStatus == nullptr)
                return;

            if (progress->lastFailure.isNotEmpty())
                safeThis->onStatus ("CAN'T LOAD " + progress->lastFailure);
            else
                safeThis->onStatus (progress->loaded == 1 ? "LOADED " + name
                                                          : "LOADED " + juce::String (progress->loaded) + " SAMPLES");
        });
    }

    if (onStatus != nullptr && progress->pending > 0)
        onStatus (progress->pending == 1 ? "LOADING " + firstName
                                         : "LOADING " + juce::String (progress->pending) + " SAMPLES");
}

bool PadGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isAudioFile (processorRef, juce::File (path)))
            return true;

    return false;
}

void PadGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    dragPad = padAt ({ x, y });
}

void PadGrid::fileDragExit (const juce::StringArray&)
{
    dragPad = -1;
}

void PadGrid::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragPad = -1;

    // Dropping several files fills consecutive pads, like dropping onto a Drum Rack
    loadFiles (files, padAt ({ x, y }));
}

//==============================================================================
FxPanel::FxPanel (juce::AudioProcessorValueTreeState& state)
{
    struct Spec
    {
        const char* title;
        const Processor::FxIds* ids;
        std::array<const char*, 4> labels, suffixes;
    };

    const std::array<Spec, 4> specs { {
        { "CHORUS",  &Processor::chorusIds,  { { "MODE",  "RATE",  "DEPTH",   "MIX" } }, { { "",    " Hz", " %",  " %" } } },
        { "FLANGER", &Processor::flangerIds, { { "RATE",  "DEPTH", "FDBK",    "MIX" } }, { { " Hz", " %",  " %",  " %" } } },
        { "ECHO",    &Processor::echoIds,    { { "TIME",  "FDBK",  "TAPE",    "MIX" } }, { { "",    " %",  " %",  " %" } } },
        { "PLATE",   &Processor::plateIds,   { { "DECAY", "TONE",  "PRE-DLY", "MIX" } }, { { " s",  " %",  " ms", " %" } } },
    } };

    for (size_t m = 0; m < modules.size(); ++m)
    {
        auto& module = modules[m];
        const auto& spec = specs[m];

        module.title = spec.title;
        module.on = state.getRawParameterValue (spec.ids->on);
        addAndMakeVisible (module.power);
        module.powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, spec.ids->on, module.power);

        for (size_t k = 0; k < module.knobs.size(); ++k)
        {
            auto& knob = module.knobs[k];

            // Small amber knobs that look like part of the screen
            knob.slider.getProperties().set ("pixelCell", 3.0f);
            knob.slider.getProperties().set ("pixelSize", 1);
            knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 12);
            knob.slider.setTextValueSuffix (spec.suffixes[k]);
            knob.slider.setColour (juce::Slider::thumbColourId, Palette::lcdBright);
            knob.slider.setColour (juce::Slider::textBoxTextColourId, Palette::lcdBright);
            addAndMakeVisible (knob.slider);

            knob.label.setText (spec.labels[k], juce::dontSendNotification);
            knob.label.setJustificationType (juce::Justification::centred);
            knob.label.setColour (juce::Label::textColourId, Palette::lcdMid);
            knob.label.getProperties().set ("pixelSize", 1);
            addAndMakeVisible (knob.label);

            knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, spec.ids->controls[k], knob.slider);
        }
    }
}

void FxPanel::resized()
{
    auto area = getLocalBounds();
    const int moduleWidth = area.getWidth() / (int) modules.size();

    for (auto& module : modules)
    {
        auto column = area.removeFromLeft (moduleWidth).reduced (6, 0);
        auto header = column.removeFromTop (18);
        module.power.setBounds (header.removeFromRight (28).withSizeKeepingCentre (28, 13));
        column.removeFromTop (4);

        const int knobWidth = column.getWidth() / 2;
        const int knobHeight = column.getHeight() / 2;

        for (int k = 0; k < 4; ++k)
        {
            auto cell = juce::Rectangle<int> (column.getX() + (k % 2) * knobWidth, column.getY() + (k / 2) * knobHeight,
                                              knobWidth, knobHeight);
            auto& knob = module.knobs[(size_t) k];
            knob.label.setBounds (cell.removeFromTop (10));
            knob.slider.setBounds (cell);
        }
    }
}

void FxPanel::paint (juce::Graphics& g)
{
    const float moduleWidth = (float) getWidth() / (float) modules.size();

    for (size_t m = 0; m < modules.size(); ++m)
    {
        const float x = moduleWidth * (float) m;
        const bool on = modules[m].on->load() > 0.5f;

        drawPixelText (g, modules[m].title, juce::Rectangle<float> (x + 6.0f, 0.0f, moduleWidth - 40.0f, 18.0f), 2,
                       on ? Palette::lcdBright : Palette::lcdMid, juce::Justification::centredLeft);

        if (m > 0)
        {
            // Dotted divider with an arrow showing the signal flow
            g.setColour (Palette::lcdDim);

            for (float y = 22.0f; y < (float) getHeight(); y += 6.0f)
                g.fillRect (std::round (x), y, 2.0f, 3.0f);

            const float arrowY = (float) getHeight() * 0.5f - 7.0f;
            g.setColour (Palette::lcdBackground);
            g.fillRect (x - 5.0f, arrowY - 2.0f, 12.0f, 18.0f);
            drawPixelText (g, ">", juce::Rectangle<float> (x - 5.0f, arrowY, 12.0f, 14.0f), 2, Palette::lcdMid);
        }
    }
}

void FxPanel::updateEnabledStates()
{
    for (auto& module : modules)
    {
        const float alpha = module.on->load() > 0.5f ? 1.0f : 0.45f;

        for (auto& knob : module.knobs)
        {
            knob.slider.setAlpha (alpha);
            knob.label.setAlpha (alpha);
        }
    }
}

//==============================================================================
MidiDragButton::MidiDragButton()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MidiDragButton::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto colour = ! isEnabled() ? Palette::lcdDim : (isMouseOver() || dragging ? Palette::lcdBright : Palette::lcdMid);

    // A dashed outline says "drag me"
    g.setColour (colour);

    for (float x = bounds.getX(); x < bounds.getRight(); x += 6.0f)
    {
        g.fillRect (x, bounds.getY(), 3.0f, 1.0f);
        g.fillRect (x, bounds.getBottom() - 1.0f, 3.0f, 1.0f);
    }

    for (float y = bounds.getY(); y < bounds.getBottom(); y += 6.0f)
    {
        g.fillRect (bounds.getX(), y, 1.0f, 3.0f);
        g.fillRect (bounds.getRight() - 1.0f, y, 1.0f, 3.0f);
    }

    auto textArea = bounds.reduced (5.0f, 0.0f);
    drawPixelText (g, ">", textArea.removeFromRight (12.0f), 2, colour);
    drawPixelText (g, "DRAG TO DAW", textArea, 2, colour);
}

void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! isEnabled() || e.getDistanceFromDragStart() < 4 || createFile == nullptr)
        return;

    const auto file = createFile();

    if (! file.existsAsFile())
        return;

    dragging = true;
    repaint();

    juce::Component::SafePointer<MidiDragButton> safeThis (this);

    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [safeThis]
    {
        if (safeThis != nullptr)
        {
            safeThis->dragging = false;
            safeThis->repaint();
        }
    });
}

void MidiDragButton::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
GenPanel::GenPanel (Processor& p)
    : processorRef (p)
{
    generateChip.onClick = [this] { generateNew(); };
    saveChip.onClick = [this] { saveMidiFile(); };

    playChip.onClick = [this]
    {
        if (pattern.notes.empty())
            generateNew();

        processorRef.previewOn = ! processorRef.previewOn.load() && ! pattern.notes.empty();

        if (onStatus != nullptr)
            onStatus (processorRef.previewOn ? "PREVIEW PLAYING" : "PREVIEW STOPPED");
    };

    dragButton.createFile = [this]
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PadSlicer MIDI");
        folder.createDirectory();
        const auto file = writeMidiFile (folder.getChildFile (getClipName() + ".mid"));

        if (onStatus != nullptr && file.existsAsFile())
            onStatus ("DROP IT ON A MIDI TRACK");

        return file;
    };

    for (juce::Component* child : std::initializer_list<juce::Component*> { &generateChip, &playChip, &saveChip, &dragButton })
        addAndMakeVisible (child);

    for (int c = 0; c < Generator::numControls; ++c)
    {
        auto& knob = knobs[(size_t) c];
        const auto& info = Generator::getControlInfo (c);

        // Small amber knobs that look like part of the screen
        knob.slider.getProperties().set ("pixelCell", 3.0f);
        knob.slider.getProperties().set ("pixelSize", 1);
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 12);
        knob.slider.setColour (juce::Slider::thumbColourId, Palette::lcdBright);
        knob.slider.setColour (juce::Slider::textBoxTextColourId, Palette::lcdBright);
        knob.slider.setRange (info.min, info.max, 1.0);
        knob.slider.textFromValueFunction = [c] (double value) { return Generator::getValueText (c, (float) value); };
        knob.slider.valueFromTextFunction = [] (const juce::String& text) { return text.getDoubleValue(); };
        knob.slider.setDoubleClickReturnValue (true, info.defaultValue);
        knob.slider.setValue (processorRef.generatorSettings.values[(size_t) c], juce::dontSendNotification);
        knob.slider.updateText();

        // Turning a knob reshapes the current pattern; only GENERATE rolls a new one
        knob.slider.onValueChange = [this, c, &knob]
        {
            processorRef.generatorSettings.values[(size_t) c] = (float) knob.slider.getValue();
            regenerate();
        };

        addAndMakeVisible (knob.slider);

        knob.label.setText (info.name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setColour (juce::Label::textColourId, Palette::lcdMid);
        knob.label.getProperties().set ("pixelSize", 1);
        addAndMakeVisible (knob.label);
    }
}

void GenPanel::resized()
{
    auto area = getLocalBounds();

    auto top = area.removeFromTop (20);
    generateChip.setBounds (top.removeFromLeft (112));
    top.removeFromLeft (6);
    playChip.setBounds (top.removeFromLeft (60));
    top.removeFromLeft (6);
    dragButton.setBounds (top.removeFromLeft (156));
    top.removeFromLeft (6);
    saveChip.setBounds (top.removeFromLeft (60));

    area.removeFromTop (6);
    patternArea = area.removeFromLeft (236);
    area.removeFromLeft (8);

    // Two rows of five knobs
    const int knobWidth = area.getWidth() / 5;
    const int knobHeight = area.getHeight() / 2;

    for (int c = 0; c < Generator::numControls; ++c)
    {
        auto cell = juce::Rectangle<int> (area.getX() + (c % 5) * knobWidth, area.getY() + (c / 5) * knobHeight,
                                          knobWidth, knobHeight);
        auto& knob = knobs[(size_t) c];
        knob.label.setBounds (cell.removeFromTop (10));
        knob.slider.setBounds (cell);
    }
}

void GenPanel::paint (juce::Graphics& g)
{
    const auto info = pattern.notes.empty()
                        ? juce::String()
                        : Generator::getValueText (Generator::bars, processorRef.generatorSettings.values[Generator::bars])
                              + "  " + juce::String ((int) pattern.notes.size()) + " NOTES";

    drawPixelText (g, info, juce::Rectangle<float> ((float) saveChip.getRight() + 8.0f, 0.0f,
                                                    (float) (getWidth() - saveChip.getRight() - 8), 20.0f),
                   1, Palette::lcdMid, juce::Justification::centredRight);

    // Pattern view: one row per slice or pad used, lowest at the bottom
    const auto area = patternArea.toFloat();
    g.setColour (Palette::lcdDim);
    g.drawRect (area, 1.0f);

    if (pattern.notes.empty())
    {
        drawPixelText (g, sourcesKey.endsWith (":") ? "LOAD A LOOP OR PADS" : "PRESS GENERATE", area.reduced (6.0f), 1, Palette::lcdMid);
        return;
    }

    const auto inner = area.reduced (3.0f);
    const double length = pattern.lengthBeats;

    std::vector<int> rows;

    for (const auto& note : pattern.notes)
        if (std::find (rows.begin(), rows.end(), note.note) == rows.end())
            rows.push_back (note.note);

    std::sort (rows.begin(), rows.end());
    const float rowHeight = inner.getHeight() / (float) rows.size();

    // Beat and bar lines
    for (int beat = 1; beat < (int) length; ++beat)
    {
        g.setColour (beat % 4 == 0 ? Palette::lcdMid.withAlpha (0.8f) : Palette::lcdDim.withAlpha (0.6f));
        g.fillRect (std::round (inner.getX() + inner.getWidth() * (float) (beat / length)), inner.getY(), 1.0f, inner.getHeight());
    }

    for (const auto& note : pattern.notes)
    {
        const auto row = (float) (std::find (rows.begin(), rows.end(), note.note) - rows.begin());
        const float gap = rowHeight > 4.0f ? 1.0f : 0.0f;
        const float x = inner.getX() + inner.getWidth() * (float) (note.start / length);
        const float width = juce::jmax (2.0f, inner.getWidth() * (float) (note.length / length));

        g.setColour (Palette::lcdBright.withAlpha (0.45f + 0.55f * note.velocity));
        g.fillRect (juce::Rectangle<float> (x, inner.getBottom() - (row + 1.0f) * rowHeight + gap,
                                            width, juce::jmax (1.5f, rowHeight - 2.0f * gap)));
    }

    // Playhead while previewing
    if (processorRef.previewOn.load())
    {
        const float x = inner.getX() + inner.getWidth() * (float) (processorRef.getPreviewPosition() / length);
        g.setColour (Palette::lcdBright);
        g.fillRect (std::round (x), area.getY(), 2.0f, area.getHeight());
    }
}

std::vector<Generator::Source> GenPanel::collectSources (double& loopBeats) const
{
    std::vector<Generator::Source> sources;
    loopBeats = 4.0;

    // Kit mode plays the loaded pads
    if (processorRef.getMode() == Processor::Mode::kit)
    {
        std::vector<int> loaded;

        for (int pad = 0; pad < Processor::numPads; ++pad)
            if (! processorRef.getSlotInfo (pad).isEmpty())
                loaded.push_back (pad);

        for (size_t i = 0; i < loaded.size(); ++i)
            sources.push_back ({ Processor::firstNote + loaded[i], (double) i / (double) loaded.size() });

        return sources;
    }

    // Slice mode plays the slices; only the ticked ones if any are ticked
    const auto layout = processorRef.getSliceLayout();

    if (layout.length <= 0 || layout.slices.empty())
        return sources;

    // How many beats the loop lasts at the current tempo, rounded to a musical length
    const double beats = layout.length / layout.sampleRate * processorRef.getLastBpm() / 60.0;

    if (beats > 0.0)
        for (const double candidate : { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
            if (std::abs (std::log2 (candidate / beats)) < std::abs (std::log2 (loopBeats / beats)))
                loopBeats = candidate;

    bool anyTicked = false;

    for (size_t i = 0; i < layout.slices.size(); ++i)
        anyTicked = anyTicked || processorRef.markedSlices[i];

    for (size_t i = 0; i < layout.slices.size(); ++i)
        if (! anyTicked || processorRef.markedSlices[i])
            sources.push_back ({ Processor::firstNote + (int) i, (double) layout.slices[i].getStart() / layout.length });

    return sources;
}

void GenPanel::update()
{
    // Rebuild the pattern when the slices or pads it plays change
    double loopBeats = 4.0;
    const auto sources = collectSources (loopBeats);

    juce::String key;
    key << loopBeats << ":";

    for (const auto& source : sources)
        key << source.note << ",";

    if (key != sourcesKey)
    {
        sourcesKey = key;
        regenerate();
    }

    // The settings can also change from outside (loading a set)
    for (int c = 0; c < Generator::numControls; ++c)
    {
        auto& slider = knobs[(size_t) c].slider;
        const float value = processorRef.generatorSettings.values[(size_t) c];

        if (! slider.isMouseButtonDown() && ! juce::approximatelyEqual ((float) slider.getValue(), value))
            slider.setValue (value, juce::dontSendNotification);
    }

    const bool playing = processorRef.previewOn.load();
    playChip.setButtonText (playing ? "STOP" : "PLAY");
    playChip.setToggleState (playing, juce::dontSendNotification);
    dragButton.setEnabled (! pattern.notes.empty());
    saveChip.setEnabled (! pattern.notes.empty());
}

void GenPanel::generateNew()
{
    double loopBeats = 4.0;

    if (collectSources (loopBeats).empty())
    {
        if (onStatus != nullptr)
            onStatus (processorRef.getMode() == Processor::Mode::kit ? "LOAD SOME PADS FIRST" : "LOAD A LOOP FIRST");

        return;
    }

    processorRef.generatorSettings.seed = juce::Random::getSystemRandom().nextInt64() | 1;
    regenerate();

    if (onStatus != nullptr)
        onStatus ("NEW PATTERN: " + juce::String ((int) pattern.notes.size()) + " NOTES");
}

void GenPanel::regenerate()
{
    double loopBeats = 4.0;
    const auto sources = collectSources (loopBeats);

    pattern = Generator::generate (processorRef.generatorSettings, sources, loopBeats);
    processorRef.setPreviewPattern (pattern);

    if (pattern.notes.empty())
        processorRef.previewOn = false;

    repaint();
}

juce::String GenPanel::getClipName() const
{
    const auto id = juce::String::toHexString (processorRef.generatorSettings.seed).getLastCharacters (4).toUpperCase();
    return "PadSlicer " + id;
}

juce::File GenPanel::writeMidiFile (const juce::File& file) const
{
    if (pattern.notes.empty())
        return {};

    file.deleteFile();
    juce::FileOutputStream out (file);

    if (! out.openedOk())
        return {};

    Generator::toMidiFile (pattern).writeTo (out);
    out.flush();
    return file;
}

void GenPanel::saveMidiFile()
{
    if (pattern.notes.empty())
        return;

    chooser = std::make_unique<juce::FileChooser> ("Save MIDI clip",
                                                   juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                       .getChildFile (getClipName() + ".mid"),
                                                   "*.mid");

    juce::Component::SafePointer<GenPanel> safeThis (this);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safeThis] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr || fc.getResult() == juce::File())
                                  return;

                              const auto file = safeThis->writeMidiFile (fc.getResult().withFileExtension (".mid"));

                              if (safeThis->onStatus != nullptr)
                                  safeThis->onStatus (file.existsAsFile() ? "SAVED " + file.getFileName() : juce::String ("COULDN'T SAVE THE FILE"));
                          });
}

//==============================================================================
PadSlicerUI::PadSlicerUI (Processor& p)
    : processorRef (p),
      thumbnail (16, p.getFormatManager(), thumbnailCache),
      padGrid (p),
      fxPanel (p.apvts),
      genPanel (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    transferButton.onClick = [this] { sendToPads(); };
    loadButton.onClick    = [this] { chooseFiles(); };
    triggerButton.onClick = [this] { toggleChoice (Processor::triggerId); };
    slicerTab.onClick     = [this] { setPage (Page::slicer); };
    padsTab.onClick       = [this] { setPage (Page::pads); };
    fxTab.onClick         = [this] { setPage (Page::fx); };
    genTab.onClick        = [this] { setPage (Page::gen); };
    gridChip.onClick      = [this] { setChoice (processorRef, Processor::sliceById, 0); };
    hitsChip.onClick      = [this] { setChoice (processorRef, Processor::sliceById, 1); };
    zoomOutChip.onClick   = [this] { zoomAround ((viewStart + viewEnd) * 0.5, 0.5); };
    zoomInChip.onClick    = [this] { zoomAround ((viewStart + viewEnd) * 0.5, 2.0); };
    zoomAllChip.onClick   = [this] { showWholeLoop(); };
    slotChip.onClick      = [this] { editMaster = false; bindKnobs(); };
    masterChip.onClick    = [this] { editMaster = true; bindKnobs(); };
    sizeChip.onClick      = [this] { showSizeMenu(); };

    for (juce::Button* button : std::initializer_list<juce::Button*> { &transferButton, &loadButton, &triggerButton,
                                                                       &slicerTab, &padsTab, &fxTab, &genTab, &gridChip, &hitsChip,
                                                                       &zoomOutChip, &zoomInChip, &zoomAllChip,
                                                                       &slotChip, &masterChip, &sizeChip })
        addAndMakeVisible (button);

    padGrid.onStatus = [this] (const juce::String& message) { setStatus (message); };
    padGrid.onPadSelected = [this] { editMaster = false; bindKnobs(); };
    addChildComponent (padGrid);
    addChildComponent (fxPanel);

    genPanel.onStatus = [this] (const juce::String& message) { setStatus (message); };
    addChildComponent (genPanel);

    struct KnobSpec { const char* name; const char* suffix; juce::Colour colour; };
    const std::array<KnobSpec, 6> specs { { { "SLICES", "",    Palette::gold   },
                                            { "START",  " %",  Palette::mint   },
                                            { "PITCH",  "",    Palette::violet },
                                            { "SPEED",  "x",   Palette::sky    },
                                            { "CUTOFF", " Hz", Palette::coral  },
                                            { "VOLUME", " dB", Palette::silver } } };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 120, 18);
        knob.slider.setTextValueSuffix (specs[i].suffix);
        knob.slider.setColour (juce::Slider::thumbColourId, specs[i].colour);
        addAndMakeVisible (knob.slider);

        knob.label.setText (specs[i].name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setColour (juce::Label::textColourId, specs[i].colour);
        addAndMakeVisible (knob.label);
    }

    setSize (designWidth, designHeight);

    // Recreate child text boxes now that our look-and-feel is in place, so they pick up its pixel sizes
    sendLookAndFeelChange();

    shownMode = processorRef.getMode();
    page = shownMode == Processor::Mode::kit ? Page::pads : Page::slicer;
    lastHitCount = processorRef.getHitCount();
    bindKnobs (true);
    timerCallback();
    startTimerHz (30);
}

PadSlicerUI::~PadSlicerUI()
{
    stopTimer();

    // Closing the window stops the pattern preview
    processorRef.previewOn = false;

    for (auto& knob : knobs)
        knob.attachment.reset();

    setLookAndFeel (nullptr);
}

//==============================================================================
void PadSlicerUI::resized()
{
    auto bounds = getLocalBounds().reduced (22, 20);

    titleArea = bounds.removeFromTop (58);
    bounds.removeFromTop (16);
    statusArea = bounds.removeFromBottom (24);
    bounds.removeFromBottom (12);
    auto knobRow = bounds.removeFromBottom (160);
    bounds.removeFromBottom (14);

    sceneArea = bounds.removeFromLeft (210);
    bounds.removeFromLeft (14);
    bezelArea = bounds;
    lcdArea = bezelArea.reduced (14);
    lcdContentArea = lcdArea.reduced (10).withTrimmedTop (24);
    padGrid.setBounds (lcdContentArea);
    fxPanel.setBounds (lcdContentArea);
    genPanel.setBounds (lcdContentArea);

    // GRID / HITS switch in the screen header, after the page name
    const auto header = lcdArea.reduced (10).withHeight (16);
    gridChip.setBounds (header.getX() + 82, header.getY() + 1, 34, 14);
    hitsChip.setBounds (gridChip.getRight() + 4, gridChip.getY(), 34, 14);

    // Zoom buttons just left of TRIG/GATE
    zoomAllChip.setBounds (header.getRight() - 56 - 30, gridChip.getY(), 30, 14);
    zoomInChip.setBounds (zoomAllChip.getX() - 20, gridChip.getY(), 16, 14);
    zoomOutChip.setBounds (zoomInChip.getX() - 20, gridChip.getY(), 16, 14);

    meterArea = knobRow.removeFromRight (64);
    knobRow.removeFromRight (12);
    knobPanelArea = knobRow;

    // What the knobs are editing, on the knob panel's top edge
    slotChip.setBounds (knobPanelArea.getX() + 14, knobPanelArea.getY() - 9, 132, 18);
    masterChip.setBounds (slotChip.getRight() + 6, slotChip.getY(), 70, 18);

    auto buttonRow = titleArea;
    triggerButton.setBounds (buttonRow.removeFromRight (72).withSizeKeepingCentre (72, 54));
    buttonRow.removeFromRight (10);
    loadButton.setBounds (buttonRow.removeFromRight (72).withSizeKeepingCentre (72, 54));
    buttonRow.removeFromRight (10);
    transferButton.setBounds (buttonRow.removeFromRight (104).withSizeKeepingCentre (104, 54));

    // Folder tabs sit on top of the screen bezel
    slicerTab.setBounds (bezelArea.getX() + 14, bezelArea.getY() - tabHeight, tabWidth, tabHeight);
    padsTab.setBounds (slicerTab.getRight() + 4, slicerTab.getY(), tabWidth, tabHeight);
    fxTab.setBounds (padsTab.getRight() + 4, slicerTab.getY(), tabWidth, tabHeight);
    genTab.setBounds (fxTab.getRight() + 4, slicerTab.getY(), tabWidth, tabHeight);

    sizeChip.setBounds (statusArea.withTrimmedLeft (statusArea.getWidth() - 146).reduced (3));

    auto knobArea = knobPanelArea.reduced (8, 10);
    const int knobWidth = knobArea.getWidth() / (int) knobs.size();

    for (auto& knob : knobs)
    {
        auto cell = knobArea.removeFromLeft (knobWidth);
        knob.label.setBounds (cell.removeFromTop (18));
        knob.slider.setBounds (cell);
    }

    backgroundScale = 0.0f;   // re-render at the next paint
}

void PadSlicerUI::renderBackground (float scale)
{
    // Everything that never changes is drawn once, at the screen's real resolution
    scale = juce::jlimit (1.0f, 4.0f, scale);
    backgroundScale = scale;
    background = juce::Image (juce::Image::ARGB, juce::roundToInt ((float) getWidth() * scale),
                              juce::roundToInt ((float) getHeight() * scale), true);

    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (scale));

    // Shell
    g.fillAll (Palette::shellShadow);
    const auto shell = getLocalBounds().toFloat().reduced (3.0f);

    g.setGradientFill (juce::ColourGradient (Palette::shellLight, 0.0f, shell.getY(),
                                             Palette::shellDark, 0.0f, shell.getBottom(), false));
    g.fillRoundedRectangle (shell, 22.0f);
    g.setColour (Palette::shellRim);
    g.drawRoundedRectangle (shell.reduced (1.5f), 21.0f, 2.0f);

    // Pixel stars across the shell
    juce::Random random (1987);

    for (int i = 0; i < 110; ++i)
    {
        const float size = random.nextFloat() < 0.8f ? 2.0f : 3.0f;
        g.setColour (Palette::ice.withAlpha (0.1f + random.nextFloat() * 0.35f));
        g.fillRect (std::round (shell.getX() + 12.0f + random.nextFloat() * (shell.getWidth() - 24.0f)),
                    std::round (shell.getY() + 12.0f + random.nextFloat() * (shell.getHeight() - 24.0f)),
                    size, size);
    }

    // Title
    auto title = titleArea.toFloat();
    drawPixelText (g, "PADSLICER", title.removeFromTop (32.0f).withTrimmedBottom (4.0f), 4, Palette::gold,
                   juce::Justification::topLeft, Palette::coral.darker (0.3f));
    title.removeFromTop (8.0f);
    drawPixelText (g, "8-BIT CHOP SAMPLER", title.withWidth (400.0f), 2, Palette::ice.withAlpha (0.75f),
                   juce::Justification::centredLeft);

    // Frame for the knight's scene (the scene itself is animated)
    g.setColour (Palette::bezel);
    g.fillRoundedRectangle (sceneArea.toFloat(), 10.0f);

    // Screen bezel with castle battlements along the top
    const auto bezel = bezelArea.toFloat();
    g.setColour (Palette::bezel);

    for (float x = (float) genTab.getRight() + 16.0f; x + 16.0f <= bezel.getRight() - 14.0f; x += 32.0f)
        g.fillRect (x, bezel.getY() - 8.0f, 16.0f, 10.0f);

    g.fillRoundedRectangle (bezel, 10.0f);

    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (lcdArea.toFloat().expanded (2.0f), 6.0f);
    g.setColour (Palette::lcdBackground);
    g.fillRoundedRectangle (lcdArea.toFloat(), 5.0f);

    // Knob and meter panels
    for (const auto& area : { knobPanelArea, meterArea })
    {
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (area.toFloat(), 10.0f);
        g.setColour (Palette::shellRim.withAlpha (0.5f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 10.0f, 1.0f);
    }

    drawPixelText (g, "LVL", meterArea.toFloat().reduced (8.0f).withHeight (16.0f), 2, Palette::ice);

    // Status bar
    const auto status = statusArea.toFloat();
    g.setColour (Palette::statusBar.darker (0.5f));
    g.fillRoundedRectangle (status.translated (0.0f, 2.0f), 4.0f);
    g.setColour (Palette::statusBar);
    g.fillRoundedRectangle (status, 4.0f);

    // Credits, in small print under the status bar
    drawPixelText (g, "BY SORRYORRI & FRANKO",
                   juce::Rectangle<float> (status.getX(), status.getBottom() + 5.0f, status.getWidth(), 8.0f), 1,
                   Palette::ice.withAlpha (0.5f), juce::Justification::centred);
}

//==============================================================================
void PadSlicerUI::paint (juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();

    if (background.isNull() || ! juce::approximatelyEqual (juce::jlimit (1.0f, 4.0f, scale), backgroundScale))
        renderBackground (scale);

    g.drawImage (background, getLocalBounds().toFloat());

    drawKnightScene (g);
    drawLcd (g);
    drawMeter (g);
    drawStatusBar (g);
}

void PadSlicerUI::paintOverChildren (juce::Graphics& g)
{
    // LCD scanlines and a little glass glare
    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (lcdArea);

    g.setColour (juce::Colours::black.withAlpha (0.16f));

    for (int y = lcdArea.getY(); y < lcdArea.getBottom(); y += 3)
        g.fillRect (lcdArea.getX(), y, lcdArea.getWidth(), 1);

    const auto lcd = lcdArea.toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), lcd.getX(), lcd.getY(),
                                             juce::Colours::transparentWhite, lcd.getX() + lcd.getWidth() * 0.45f,
                                             lcd.getY() + lcd.getHeight() * 0.6f, false));
    g.fillRect (lcd);
}

void PadSlicerUI::drawKnightScene (juce::Graphics& g)
{
    constexpr float pixelSize = 6.0f;

    const auto sky = sceneArea.toFloat().reduced (8.0f);
    const float groundY = sky.getBottom() - 56.0f;
    const double now = nowMs();
    const bool attacking = flashNote >= 0 && now - flashTime < attackMs;
    const float bob = (! attacking && ((int) (now / 600.0)) % 2 == 0) ? -3.0f : 0.0f;

    const juce::Point<float> topLeft (std::round (sky.getCentreX() - 9.0f * pixelSize),
                                      groundY - (float) Knight::height * pixelSize + 4.0f + bob);

    const juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (sky.toNearestInt());

    Knight::drawScenery (g, sky, groundY, shownLevel, now);

    // Name and rank, on a dark band so they read on bright skies
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (sky.withHeight (44.0f));
    drawPixelText (g, "SIR SLICEALOT", sky.withHeight (24.0f), 2, Palette::gold,
                   juce::Justification::centred, juce::Colours::black.withAlpha (0.5f));
    drawPixelText (g, "LV" + juce::String (shownLevel) + " " + Knight::getRankName (shownLevel),
                   sky.withTrimmedTop (24.0f).withHeight (14.0f), 2, Palette::ice,
                   juce::Justification::centred, juce::Colours::black.withAlpha (0.5f));

    Knight::draw (g, topLeft, pixelSize, shownLevel, attacking, now);

    if (attacking)
        drawPixelText (g, juce::MidiMessage::getMidiNoteName (flashNote, true, true, 3) + "!",
                       juce::Rectangle<float> (topLeft.x + 13.0f * pixelSize, topLeft.y - 8.0f, 80.0f, 16.0f),
                       2, Palette::gold, juce::Justification::centredLeft, juce::Colours::black.withAlpha (0.6f));

    // XP towards the next level
    const auto hits = processorRef.getKnightProgress().getTotalHits();
    const int progress = KnightProgress::hitsIntoLevel (hits);
    const int needed = KnightProgress::hitsToAdvance (shownLevel);
    const bool finalRank = shownLevel >= Knight::getNumRanks();

    auto xpArea = juce::Rectangle<float> (sky.getX() + 8.0f, groundY + 12.0f, sky.getWidth() - 16.0f, 36.0f);
    const auto textRow = xpArea.removeFromTop (8.0f);

    drawPixelText (g, "XP " + juce::String (progress) + "/" + juce::String (needed), textRow, 1,
                   Palette::ice, juce::Justification::centredLeft, juce::Colours::black.withAlpha (0.7f));
    drawPixelText (g, finalRank ? "NEXT LV" + juce::String (shownLevel + 1) : "NEXT " + Knight::getRankName (shownLevel + 1),
                   textRow, 1, Palette::gold, juce::Justification::centredRight, juce::Colours::black.withAlpha (0.7f));

    xpArea.removeFromTop (5.0f);
    const auto bar = xpArea.removeFromTop (14.0f);
    g.setColour (Palette::panelHeader);
    g.fillRect (bar);
    g.setColour (Palette::ice.withAlpha (0.5f));
    g.drawRect (bar, 1.0f);

    constexpr int numSegments = 10;
    const auto inner = bar.reduced (3.0f);
    const float segmentWidth = inner.getWidth() / (float) numSegments;
    const int litSegments = progress * numSegments / needed;

    for (int i = 0; i < numSegments; ++i)
    {
        g.setColour (i < litSegments ? Palette::gold : Palette::gold.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (inner.getX() + (float) i * segmentWidth, inner.getY(),
                                            segmentWidth - 2.0f, inner.getHeight()).toNearestInt());
    }

    // Level-up celebration
    const double sinceLevelUp = now - levelUpTime;

    if (sinceLevelUp < levelUpMs)
    {
        const auto knightBounds = juce::Rectangle<float> (topLeft.x, topLeft.y, (float) Knight::width * pixelSize,
                                                          (float) Knight::height * pixelSize);
        Knight::drawSparkles (g, knightBounds.expanded (pixelSize * 2.0f), pixelSize, 14, now, 99);

        if (((int) (sinceLevelUp / 160.0)) % 2 == 0)
            drawPixelText (g, "LEVEL UP!", juce::Rectangle<float> (sky.getX(), sky.getY() + 50.0f, sky.getWidth(), 22.0f), 3,
                           Palette::gold, juce::Justification::centred, Palette::coral.darker (0.3f));

        const float flash = juce::jmax (0.0f, 0.6f - (float) sinceLevelUp / 700.0f);

        if (flash > 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (flash));
            g.fillRect (sky);
        }
    }
}

void PadSlicerUI::drawLcd (juce::Graphics& g)
{
    auto lcd = lcdArea.toFloat().reduced (10.0f);
    const auto header = lcd.removeFromTop (16.0f);

    const auto pageName = page == Page::gen ? "GEN" : (page == Page::fx ? "FX" : (page == Page::pads ? "PADS" : "SLICER"));
    drawPixelText (g, pageName, header, 2, Palette::lcdBright, juce::Justification::centredLeft);
    drawPixelText (g, processorRef.getTrigger() == Processor::Trigger::oneShot ? "TRIG" : "GATE",
                   header, 2, Palette::lcdBright, juce::Justification::centredRight);

    juce::String centreText;
    auto centreArea = header.reduced (90.0f, 0.0f);

    if (page == Page::fx)
    {
        centreText = "SIGNAL FLOW >";
    }
    else if (page == Page::gen)
    {
        centreText = processorRef.getMode() == Processor::Mode::kit ? "PATTERN FOR PADS" : "PATTERN FOR SLICES";
    }
    else if (page == Page::pads)
    {
        centreText = "16 PADS";
    }
    else
    {
        centreText = shownFile == juce::File() ? juce::String ("NO SAMPLE") : shownFile.getFileName();
        centreArea = header.withTrimmedLeft (170.0f).withTrimmedRight (144.0f);
    }

    drawPixelText (g, centreText, centreArea, 2, Palette::lcdMid);

    if (page == Page::slicer && viewEnd - viewStart < 0.999)
    {
        // Overview bar: the bright part is the section of the loop on screen. Drag it to move around.
        const auto overview = overviewArea();
        const float x0 = overview.getX() + overview.getWidth() * (float) viewStart;
        const float x1 = overview.getX() + overview.getWidth() * (float) viewEnd;

        g.setColour (Palette::lcdDim);
        g.fillRect (juce::Rectangle<float> (overview.getX(), overview.getY() + 3.0f, overview.getWidth(), 2.0f).toNearestInt());
        g.setColour (Palette::lcdBright);
        g.fillRect (juce::Rectangle<float> (x0, overview.getY() + 1.0f, juce::jmax (4.0f, x1 - x0), 6.0f).toNearestInt());
    }
    else
    {
        g.setColour (Palette::lcdDim);

        for (float x = header.getX(); x < header.getRight(); x += 4.0f)
            g.fillRect (x, header.getBottom() + 3.0f, 2.0f, 2.0f);
    }

    if (page == Page::slicer)
        drawWaveform (g, lcdContentArea.toFloat());
}

juce::Rectangle<float> PadSlicerUI::waveformArea() const
{
    return lcdContentArea.toFloat().withTrimmedTop (12.0f).withTrimmedBottom (18.0f);
}

juce::Rectangle<float> PadSlicerUI::overviewArea() const
{
    const auto header = lcdArea.toFloat().reduced (10.0f).withHeight (16.0f);
    return { header.getX(), header.getBottom(), header.getWidth(), 8.0f };
}

double PadSlicerUI::positionAt (float x) const
{
    const auto wave = waveformArea();
    return viewStart + (double) ((x - wave.getX()) / wave.getWidth()) * (viewEnd - viewStart);
}

float PadSlicerUI::xForPosition (double position) const
{
    const auto wave = waveformArea();
    return wave.getX() + (float) ((position - viewStart) / (viewEnd - viewStart)) * wave.getWidth();
}

bool PadSlicerUI::canZoom() const
{
    return page == Page::slicer && thumbnail.getTotalLength() > 0.0;
}

void PadSlicerUI::zoomAround (double anchor, double factor)
{
    if (! canZoom())
        return;

    // Zoom in until about 1000 samples fill the screen
    const int length = processorRef.getSliceLayout().length;
    const double minimumWidth = length > 0 ? juce::jlimit (1.0 / 512.0, 1.0, 1024.0 / length) : 1.0 / 64.0;

    const double width = viewEnd - viewStart;
    const double newWidth = juce::jlimit (minimumWidth, 1.0, width / factor);
    const double relative = (anchor - viewStart) / width;   // keep the anchor under the pointer
    const double start = juce::jlimit (0.0, 1.0 - newWidth, anchor - relative * newWidth);

    viewStart = start;
    viewEnd = start + newWidth;
    setStatus (newWidth >= 0.999 ? juce::String ("WHOLE LOOP") : "ZOOM X" + juce::String (1.0 / newWidth, 1));
    repaint (lcdArea);
}

void PadSlicerUI::scrollView (double amount)
{
    const double width = viewEnd - viewStart;
    viewStart = juce::jlimit (0.0, 1.0 - width, viewStart + amount * width);
    viewEnd = viewStart + width;
    repaint (lcdArea);
}

void PadSlicerUI::showWholeLoop()
{
    viewStart = 0.0;
    viewEnd = 1.0;
    setStatus ("WHOLE LOOP");
    repaint (lcdArea);
}

juce::Rectangle<float> PadSlicerUI::markStripArea() const
{
    return lcdContentArea.toFloat().removeFromBottom (14.0f);
}

int PadSlicerUI::sliceAt (float x, const Processor::SliceLayout& layout) const
{
    if (layout.length <= 0)
        return -1;

    const auto position = (float) (positionAt (x) * layout.length);

    for (size_t i = 0; i < layout.slices.size(); ++i)
        if (position >= (float) layout.slices[i].getStart() && position < (float) layout.slices[i].getEnd())
            return (int) i;

    return -1;
}

void PadSlicerUI::drawWaveform (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (thumbnail.getTotalLength() <= 0.0)
    {
        if (((int) (nowMs() / 600.0)) % 2 == 0)
            drawPixelText (g, "DROP A LOOP HERE", area.withTrimmedBottom (40.0f), 3, Palette::lcdBright);

        drawPixelText (g, "OR PRESS LOAD", area.withTrimmedTop (40.0f), 2, Palette::lcdMid);
        return;
    }

    constexpr float cell = 4.0f;

    const auto layout = processorRef.getSliceLayout();
    const auto wave = waveformArea();
    const auto labelRow = area.withHeight (10.0f);
    const auto strip = markStripArea();
    const auto length = (float) juce::jmax (1, layout.length);
    const int numSlices = (int) layout.slices.size();
    const int selected = processorRef.selectedSlice.load();
    const float masterStart = processorRef.apvts.getRawParameterValue (Processor::startId)->load();

    auto xFor = [&] (int sample) { return xForPosition ((double) sample / length); };

    // Only draw inside the screen when zoomed in
    const juce::Graphics::ScopedSaveState clipState (g);
    g.reduceClipRegion (area.toNearestInt());

    // Selected and playing slices
    for (int i = 0; i < numSlices; ++i)
    {
        const bool lit = isNoteLit (Processor::firstNote + i);

        if (lit || i == selected)
        {
            const float x0 = xFor (layout.slices[(size_t) i].getStart());
            const float x1 = xFor (layout.slices[(size_t) i].getEnd());
            g.setColour (Palette::lcdDim.withAlpha (lit ? 0.75f : 0.4f));
            g.fillRect (juce::Rectangle<float> (x0, wave.getY(), x1 - x0, wave.getHeight()).toNearestInt());
        }
    }

    // Chunky pixel waveform
    const int columns = (int) (wave.getWidth() / cell);
    const float midY = std::round (wave.getCentreY() / cell) * cell;
    const float halfHeight = wave.getHeight() * 0.5f - cell;
    const double seconds = thumbnail.getTotalLength();
    const double viewWidth = viewEnd - viewStart;
    int slice = 0;

    for (int column = 0; column < columns; ++column)
    {
        const double p0 = viewStart + viewWidth * column / columns;
        const double p1 = viewStart + viewWidth * (column + 1) / columns;
        const double t0 = seconds * p0;
        const double t1 = seconds * p1;

        float minValue = 0.0f, maxValue = 0.0f;
        thumbnail.getApproximateMinMax (t0, t1, 0, minValue, maxValue);

        if (thumbnail.getNumChannels() > 1)
        {
            float minRight = 0.0f, maxRight = 0.0f;
            thumbnail.getApproximateMinMax (t0, t1, 1, minRight, maxRight);
            minValue = juce::jmin (minValue, minRight);
            maxValue = juce::jmax (maxValue, maxRight);
        }

        const auto position = (float) ((p0 + p1) * 0.5) * length;

        while (slice < numSlices && position >= (float) layout.slices[(size_t) slice].getEnd())
            ++slice;

        const bool inSlice = slice < numSlices && position >= (float) layout.slices[(size_t) slice].getStart();

        if (! inSlice)
        {
            g.setColour (Palette::lcdDim);
        }
        else
        {
            // Dim the part each slice skips because of its start offset
            const auto region = layout.slices[(size_t) slice];
            const float startPercent = juce::jlimit (0.0f, 95.0f, masterStart + processorRef.getSetting (Processor::sliceSettings (slice),
                                                                                                          Processor::startControl));
            const float within = (position - (float) region.getStart()) / (float) juce::jmax (1, region.getLength());

            if (within < startPercent / 100.0f)
                g.setColour (Palette::lcdDim);
            else
                g.setColour (isNoteLit (Processor::firstNote + slice) ? Palette::lcdBright : Palette::lcdMid);
        }

        const int cellsUp   = juce::jmax (1, (int) std::ceil (juce::jlimit (0.0f, 1.0f, maxValue) * halfHeight / cell));
        const int cellsDown = juce::jmax (0, (int) std::ceil (juce::jlimit (0.0f, 1.0f, -minValue) * halfHeight / cell));

        g.fillRect (wave.getX() + (float) column * cell, midY - (float) cellsUp * cell,
                    cell - 1.0f, (float) (cellsUp + cellsDown) * cell);
    }

    // Slice markers, note names and the tick boxes for sending slices to pads
    for (int i = 0; i < numSlices; ++i)
    {
        const auto region = layout.slices[(size_t) i];
        const float x = std::round (xFor (region.getStart()));
        const float width = xFor (region.getEnd()) - xFor (region.getStart());
        const bool lit = isNoteLit (Processor::firstNote + i);

        if (x + width < wave.getX() || x > wave.getRight())
            continue;

        if (region.getStart() > 0)
        {
            g.setColour (Palette::lcdMid.withAlpha (0.7f));

            for (float y = labelRow.getY(); y < strip.getY() - 2.0f; y += 6.0f)
                g.fillRect (x, y, 2.0f, 3.0f);
        }

        if (width >= 26.0f)
            drawPixelText (g, juce::MidiMessage::getMidiNoteName (Processor::firstNote + i, true, true, 3),
                           juce::Rectangle<float> (x + 4.0f, labelRow.getY(), width - 6.0f, 8.0f), 1,
                           lit || i == selected ? Palette::lcdBright : Palette::lcdMid, juce::Justification::centredLeft);

        if (i == selected)
        {
            g.setColour (Palette::lcdBright);
            g.fillRect (juce::Rectangle<float> (x, wave.getY() - 2.0f, width, 2.0f).toNearestInt());
        }

        const bool marked = processorRef.markedSlices[(size_t) i];
        const float box = juce::jmin (10.0f, width - 4.0f);

        if (box >= 5.0f)
        {
            const auto square = juce::Rectangle<float> (box, box).withCentre ({ x + width * 0.5f, strip.getCentreY() }).toNearestInt().toFloat();
            g.setColour (marked ? Palette::lcdBright : Palette::lcdMid);

            if (marked)
                g.fillRect (square);
            else
                g.drawRect (square, 1.0f);
        }
        else
        {
            g.setColour (marked ? Palette::lcdBright : Palette::lcdDim);
            g.fillRect (juce::Rectangle<float> (x + 1.0f, strip.getCentreY() - 1.0f, juce::jmax (1.0f, width - 2.0f), 3.0f).toNearestInt());
        }
    }
}

void PadSlicerUI::drawMeter (juce::Graphics& g)
{
    auto area = meterArea.toFloat().reduced (14.0f, 10.0f).withTrimmedTop (24.0f);

    constexpr int numSegments = 16;
    const float segmentHeight = area.getHeight() / (float) numSegments;
    const float level = juce::jmap (juce::Decibels::gainToDecibels (meterLevel, -48.0f), -48.0f, 0.0f, 0.0f, 1.0f);
    const int lit = juce::roundToInt (level * (float) numSegments);

    for (int i = 0; i < numSegments; ++i)
    {
        const auto colour = i >= 13 ? Palette::coral : (i >= 10 ? Palette::gold : Palette::mint);
        g.setColour (i < lit ? colour : colour.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getBottom() - (float) (i + 1) * segmentHeight + 1.0f,
                                            area.getWidth(), segmentHeight - 2.0f).toNearestInt());
    }
}

void PadSlicerUI::drawStatusBar (juce::Graphics& g)
{
    auto area = statusArea.toFloat().reduced (12.0f, 0.0f).withTrimmedRight (146.0f);
    const bool kit = shownMode == Processor::Mode::kit;

    juce::String modeInfo;

    if (kit)
    {
        int loadedPads = 0;

        for (int pad = 0; pad < Processor::numPads; ++pad)
            if (! processorRef.getSlotInfo (pad).isEmpty())
                ++loadedPads;

        modeInfo = "PADS " + twoDigits (loadedPads) + "/16";
    }
    else
    {
        modeInfo = (processorRef.getSliceBy() == Processor::SliceBy::hits ? "HITS " : "SLICES ")
                 + juce::String ((int) processorRef.getSliceLayout().slices.size());
    }

    const std::array<juce::String, 2> segments
    {
        modeInfo,
        "VOICES " + twoDigits (processorRef.getActiveVoiceCount()) + "/" + juce::String (Processor::maxVoices),
    };

    // Right-aligned, with a divider between items
    for (int i = (int) segments.size(); --i >= 0;)
    {
        const auto& segment = segments[(size_t) i];
        const auto width = (float) pixelTextWidth (segment, 2);
        drawPixelText (g, segment, area.removeFromRight (width), 2, Palette::statusText, juce::Justification::centredRight);

        const auto divider = area.removeFromRight (30.0f);
        g.setColour (Palette::statusText.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (4.0f, 12.0f).withCentre (divider.getCentre()).toNearestInt());
    }

    drawPixelText (g, nowMs() - statusTime < statusMs ? statusMessage : juce::String ("READY."),
                   area, 2, Palette::statusText, juce::Justification::centredLeft);
}

//==============================================================================
void PadSlicerUI::timerCallback()
{
    const double now = nowMs();
    const auto mode = processorRef.getMode();

    if (mode != shownMode)
    {
        // Follow mode changes from automation, unless the FX page is open
        shownMode = mode;

        if (page == Page::slicer || page == Page::pads)
            page = mode == Processor::Mode::kit ? Page::pads : Page::slicer;

        repaint();
    }

    const auto file = processorRef.getSampleFile (Processor::sliceSlot);

    if (file != shownFile)
    {
        shownFile = file;
        viewStart = 0.0;
        viewEnd = 1.0;
        thumbnail.clear();

        if (file.existsAsFile())
            thumbnail.setSource (new juce::FileInputSource (file));
    }

    // Keep the selection inside the current slices
    const int numSlices = (int) processorRef.getSliceLayout().slices.size();

    if (numSlices > 0 && processorRef.selectedSlice.load() >= numSlices)
        processorRef.selectedSlice = numSlices - 1;

    updatePage();
    bindKnobs();
    refreshKnobValues();

    const auto hitCount = processorRef.getHitCount();

    if (hitCount != lastHitCount)
    {
        lastHitCount = hitCount;
        flashNote = processorRef.getLastHitNote();
        flashTime = now;
    }

    meterLevel = juce::jmax (processorRef.takeOutputPeak(), meterLevel * 0.85f);

    const int level = KnightProgress::levelForHits (processorRef.getKnightProgress().getTotalHits());

    if (shownLevel > 0 && level > shownLevel)
    {
        levelUpTime = now;
        setStatus (level <= Knight::getNumRanks() ? "LEVEL UP! RANK: " + Knight::getRankName (level)
                                                  : "LEVEL UP! LV " + juce::String (level));
    }

    shownLevel = level;

    juce::uint32 litPads = 0;

    for (int pad = 0; pad < Processor::numPads; ++pad)
        if (isNoteLit (Processor::firstNote + pad))
            litPads |= (1u << pad);

    padGrid.setLitPads (litPads);
    sizeChip.setButtonText ("SIZE " + juce::String (juce::roundToInt (processorRef.uiScale.load() * 100.0f)) + "%");

    repaint (sceneArea);
    repaint (lcdArea);
    repaint (meterArea);
    repaint (statusArea);
}

bool PadSlicerUI::isNoteLit (int note) const
{
    return processorRef.keyboardState.isNoteOnForChannels (0xffff, note)
        || (note == flashNote && nowMs() - flashTime < flashMs);
}

void PadSlicerUI::setStatus (const juce::String& message)
{
    statusMessage = message;
    statusTime = nowMs();
}

void PadSlicerUI::setPage (Page newPage)
{
    page = newPage;

    if (page == Page::slicer)
        setMode (Processor::Mode::slice);
    else if (page == Page::pads)
        setMode (Processor::Mode::kit);

    updatePage();
    bindKnobs();
    repaint();
}

void PadSlicerUI::updatePage()
{
    padGrid.setVisible (page == Page::pads);
    fxPanel.setVisible (page == Page::fx);
    fxPanel.updateEnabledStates();
    genPanel.setVisible (page == Page::gen);

    if (page == Page::gen)
        genPanel.update();

    slicerTab.setToggleState (page == Page::slicer, juce::dontSendNotification);
    padsTab.setToggleState (page == Page::pads, juce::dontSendNotification);
    fxTab.setToggleState (page == Page::fx, juce::dontSendNotification);
    genTab.setToggleState (page == Page::gen, juce::dontSendNotification);

    const bool byHits = processorRef.getSliceBy() == Processor::SliceBy::hits;
    gridChip.setVisible (page == Page::slicer);
    hitsChip.setVisible (page == Page::slicer);
    for (auto* chip : { &zoomOutChip, &zoomInChip, &zoomAllChip })
    {
        chip->setVisible (page == Page::slicer);
        chip->setEnabled (canZoom());
    }

    zoomAllChip.setToggleState (viewEnd - viewStart < 0.999, juce::dontSendNotification);
    gridChip.setToggleState (! byHits, juce::dontSendNotification);
    hitsChip.setToggleState (byHits, juce::dontSendNotification);

    // Slice count and hit sensitivity only matter in Slice mode
    const bool kit = processorRef.getMode() == Processor::Mode::kit;
    knobs[0].slider.setEnabled (! kit);
    knobs[0].label.setEnabled (! kit);
}

void PadSlicerUI::setMode (Processor::Mode mode)
{
    setChoice (processorRef, Processor::modeId, mode == Processor::Mode::kit ? 1 : 0);
}

void PadSlicerUI::toggleChoice (const char* paramId)
{
    if (auto* parameter = processorRef.apvts.getParameter (paramId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getValue() < 0.5f ? 1.0f : 0.0f);
        parameter->endChangeGesture();
    }
}

//==============================================================================
int PadSlicerUI::getEditTarget() const
{
    if (editMaster)
        return -1;

    const bool kit = page == Page::pads || (page != Page::slicer && processorRef.getMode() == Processor::Mode::kit);

    return kit ? Processor::padSettings (processorRef.selectedPad.load())
               : Processor::sliceSettings (processorRef.selectedSlice.load());
}

void PadSlicerUI::bindKnobs (bool force)
{
    const int target = getEditTarget();
    const auto sliceBy = processorRef.getSliceBy();

    if (! force && target == boundTarget && sliceBy == boundSliceBy)
    {
        updateEditChips();
        return;
    }

    boundTarget = target;
    boundSliceBy = sliceBy;

    // First knob: the slice count, or the hit sensitivity when slicing by hits
    auto& first = knobs[0];
    const bool byHits = sliceBy == Processor::SliceBy::hits;
    first.attachment.reset();
    first.label.setText (byHits ? "SENS" : "SLICES", juce::dontSendNotification);
    first.slider.setTextValueSuffix (byHits ? " %" : "");
    first.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, byHits ? Processor::hitSensId : Processor::slicesId, first.slider);

    // The rest edit the selected slice or pad, or the master parameters
    for (int c = 0; c < Processor::numSlotControls; ++c)
    {
        auto& knob = knobs[(size_t) c + 1];
        const auto control = (Processor::SlotControl) c;
        const auto* masterId = Processor::masterParameterFor (control);
        auto* parameter = processorRef.apvts.getParameter (masterId);

        knob.attachment.reset();
        knob.slider.onValueChange = nullptr;

        if (target < 0)
        {
            knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processorRef.apvts, masterId, knob.slider);
            knob.slider.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
            continue;
        }

        const auto range = parameter->getNormalisableRange();
        knob.slider.setNormalisableRange ({ (double) range.start, (double) range.end, (double) range.interval, (double) range.skew });
        knob.slider.textFromValueFunction = [parameter] (double value) { return parameter->getText (parameter->convertTo0to1 ((float) value), 0); };
        knob.slider.valueFromTextFunction = [parameter] (const juce::String& text) { return (double) parameter->convertFrom0to1 (parameter->getValueForText (text)); };
        knob.slider.setDoubleClickReturnValue (true, Processor::defaultSetting (control));
        knob.slider.setValue (processorRef.getSetting (target, control), juce::dontSendNotification);
        knob.slider.updateText();

        knob.slider.onValueChange = [this, target, control, &knob]
        {
            processorRef.setSetting (target, control, (float) knob.slider.getValue());
        };
    }

    updateEditChips();
}

void PadSlicerUI::refreshKnobValues()
{
    // Settings can change from outside the knobs (loading a set, sending slices to pads)
    if (boundTarget < 0)
        return;

    for (int c = 0; c < Processor::numSlotControls; ++c)
    {
        auto& slider = knobs[(size_t) c + 1].slider;
        const float value = processorRef.getSetting (boundTarget, (Processor::SlotControl) c);

        if (! slider.isMouseButtonDown() && ! juce::approximatelyEqual ((float) slider.getValue(), value))
            slider.setValue (value, juce::dontSendNotification);
    }
}

void PadSlicerUI::updateEditChips()
{
    const bool kit = page == Page::pads || (page != Page::slicer && processorRef.getMode() == Processor::Mode::kit);
    const int index = kit ? processorRef.selectedPad.load() : processorRef.selectedSlice.load();
    const auto note = juce::MidiMessage::getMidiNoteName (Processor::firstNote + index, true, true, 3);

    slotChip.setButtonText ((kit ? "PAD " : "SLICE ") + twoDigits (index + 1) + "  " + note);
    slotChip.setToggleState (! editMaster, juce::dontSendNotification);
    masterChip.setToggleState (editMaster, juce::dontSendNotification);
}

//==============================================================================
void PadSlicerUI::mouseDown (const juce::MouseEvent& e)
{
    // Dragging the overview bar moves the zoomed view
    if (canZoom() && viewEnd - viewStart < 0.999 && overviewArea().contains (e.position))
    {
        draggingOverview = true;
        mouseDrag (e);
        return;
    }

    if (page != Page::slicer || shownFile == juce::File() || ! lcdContentArea.toFloat().contains (e.position))
        return;

    if (e.mods.isPopupMenu())
    {
        showSliceMenu();
        return;
    }

    const auto layout = processorRef.getSliceLayout();
    const int slice = sliceAt (e.position.x, layout);

    if (slice < 0)
        return;

    // The strip under the waveform ticks slices for sending to pads
    if (markStripArea().contains (e.position))
    {
        auto& marked = processorRef.markedSlices[(size_t) slice];
        marked = ! marked;

        int count = 0;

        for (size_t i = 0; i < layout.slices.size(); ++i)
            count += processorRef.markedSlices[i] ? 1 : 0;

        setStatus (juce::String (count) + (count == 1 ? " SLICE" : " SLICES") + " TICKED FOR PADS");
        repaint (lcdArea);
        return;
    }

    // Anywhere else selects the slice for the knobs and plays it
    processorRef.selectedSlice = slice;
    editMaster = false;
    bindKnobs();

    heldSliceNote = Processor::firstNote + slice;
    processorRef.keyboardState.noteOn (1, heldSliceNote, 1.0f);
}

void PadSlicerUI::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingOverview)
        return;

    const auto overview = overviewArea();
    const double width = viewEnd - viewStart;
    const double centre = (double) ((e.position.x - overview.getX()) / overview.getWidth());

    viewStart = juce::jlimit (0.0, 1.0 - width, centre - width * 0.5);
    viewEnd = viewStart + width;
    repaint (lcdArea);
}

void PadSlicerUI::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! canZoom() || ! lcdContentArea.toFloat().contains (e.position))
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    // Sideways (or Shift+scroll) moves along the loop; up and down zooms around the pointer
    const float sideways = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
        scrollView (-(double) sideways * 2.0);
    else
        zoomAround (positionAt (e.position.x), std::pow (2.0, (double) wheel.deltaY * 4.0));
}

void PadSlicerUI::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    // Trackpad pinch
    if (canZoom() && lcdContentArea.toFloat().contains (e.position))
        zoomAround (positionAt (e.position.x), (double) scaleFactor);
}

void PadSlicerUI::mouseUp (const juce::MouseEvent&)
{
    draggingOverview = false;

    if (heldSliceNote >= 0)
        processorRef.keyboardState.noteOff (1, heldSliceNote, 0.0f);

    heldSliceNote = -1;
}

void PadSlicerUI::showSliceMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Tick All Slices");
    menu.addItem (2, "Untick All Slices");
    menu.addSeparator();
    menu.addItem (3, "Transfer Ticked Slices To Pads");

    juce::Component::SafePointer<PadSlicerUI> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safeThis] (int result)
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto& self = *safeThis;

        if (result == 3)
        {
            self.sendToPads();
            return;
        }

        const int numSlices = (int) self.processorRef.getSliceLayout().slices.size();

        for (int i = 0; i < Processor::maxSlices; ++i)
            self.processorRef.markedSlices[(size_t) i] = result == 1 && i < numSlices;

        self.setStatus (result == 1 ? "TICKED ALL " + juce::String (numSlices) + " SLICES" : "UNTICKED ALL SLICES");
    });
}

void PadSlicerUI::sendToPads()
{
    if (processorRef.getSlotInfo (Processor::sliceSlot).isEmpty())
    {
        setStatus ("LOAD A LOOP IN THE SLICER FIRST");
        return;
    }

    const int numSlices = (int) processorRef.getSliceLayout().slices.size();
    juce::Array<int> chosen;

    for (int i = 0; i < numSlices; ++i)
        if (processorRef.markedSlices[(size_t) i])
            chosen.add (i);

    // Nothing ticked: send the selected slice
    if (chosen.isEmpty())
        chosen.add (juce::jlimit (0, juce::jmax (0, numSlices - 1), processorRef.selectedSlice.load()));

    int emptyPads = 0;

    for (int pad = 0; pad < Processor::numPads; ++pad)
        emptyPads += processorRef.getSlotInfo (pad).isEmpty() ? 1 : 0;

    if (emptyPads == 0)
    {
        setStatus ("NO EMPTY PADS - RIGHT-CLICK A PAD TO CLEAR");
        return;
    }

    setStatus ("TRANSFERRING " + juce::String (chosen.size()) + " TO PADS...");

    juce::Component::SafePointer<PadSlicerUI> safeThis (this);
    const int requested = chosen.size();

    processorRef.sendSlicesToPads (chosen, [safeThis, requested] (int sent)
    {
        if (safeThis == nullptr)
            return;

        safeThis->processorRef.markedSlices.fill (false);
        safeThis->setStatus (sent < requested ? "SENT " + juce::String (sent) + " OF " + juce::String (requested) + " - PADS FULL"
                                              : "TRANSFERRED " + juce::String (sent) + (sent == 1 ? " SLICE" : " SLICES") + " TO PADS");

        if (sent > 0)
            safeThis->setPage (Page::pads);
    });
}

void PadSlicerUI::showSizeMenu()
{
    juce::PopupMenu menu;
    const float current = processorRef.uiScale.load();

    for (size_t i = 0; i < uiSizes.size(); ++i)
        menu.addItem ((int) i + 1, juce::String (juce::roundToInt (uiSizes[i] * 100.0f)) + "%", true,
                      juce::approximatelyEqual (current, uiSizes[i]));

    juce::Component::SafePointer<PadSlicerUI> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeChip), [safeThis] (int result)
    {
        if (safeThis != nullptr && result > 0 && safeThis->onScaleChosen != nullptr)
            safeThis->onScaleChosen (uiSizes[(size_t) result - 1]);
    });
}

//==============================================================================
bool PadSlicerUI::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (page != Page::slicer)
        return false;

    for (const auto& path : files)
        if (isAudioFile (processorRef, juce::File (path)))
            return true;

    return false;
}

void PadSlicerUI::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& path : files)
    {
        if (isAudioFile (processorRef, juce::File (path)))
        {
            loadSliceFile (juce::File (path));
            return;
        }
    }
}

void PadSlicerUI::chooseFiles()
{
    const bool kit = processorRef.getMode() == Processor::Mode::kit;
    const auto startFolder = processorRef.getSampleFile (kit ? 0 : Processor::sliceSlot).getParentDirectory();

    chooser = std::make_unique<juce::FileChooser> (kit ? "Select one-shots for the kit" : "Select a loop to slice",
                                                   startFolder,
                                                   processorRef.getFormatManager().getWildcardForAllFormats());

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    if (kit)
        flags |= juce::FileBrowserComponent::canSelectMultipleItems;

    juce::Component::SafePointer<PadSlicerUI> safeThis (this);

    chooser->launchAsync (flags, [safeThis, kit] (const juce::FileChooser& fc)
    {
        const auto results = fc.getResults();

        if (safeThis == nullptr || results.isEmpty())
            return;

        if (kit)
            safeThis->loadKitFiles (results);
        else
            safeThis->loadSliceFile (results.getFirst());
    });
}

void PadSlicerUI::loadSliceFile (const juce::File& file)
{
    setStatus ("LOADING " + file.getFileName());

    juce::Component::SafePointer<PadSlicerUI> safeThis (this);

    processorRef.loadSampleAsync (Processor::sliceSlot, file, [safeThis, name = file.getFileName()] (bool ok)
    {
        if (safeThis != nullptr)
            safeThis->setStatus ((ok ? "LOADED " : "CAN'T LOAD ") + name);
    });
}

void PadSlicerUI::loadKitFiles (const juce::Array<juce::File>& files)
{
    // Fill from the first empty pad, or from pad 1 if the kit is full
    int pad = 0;

    while (pad < Processor::numPads && ! processorRef.getSlotInfo (pad).isEmpty())
        ++pad;

    if (pad == Processor::numPads)
        pad = 0;

    juce::StringArray paths;

    for (const auto& file : files)
        paths.add (file.getFullPathName());

    padGrid.loadFiles (paths, pad);
}

//==============================================================================
PadSlicerAudioProcessorEditor::PadSlicerAudioProcessorEditor (PadSlicerAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      ui (p)
{
    addAndMakeVisible (ui);
    ui.onScaleChosen = [this] (float scale) { setUiScale (scale); };
    setUiScale (processorRef.uiScale.load());
}

void PadSlicerAudioProcessorEditor::resized()
{
    // The interface keeps its design size; its transform does the scaling
    ui.setBounds (0, 0, PadSlicerUI::designWidth, PadSlicerUI::designHeight);
}

void PadSlicerAudioProcessorEditor::setUiScale (float scale)
{
    scale = juce::jlimit (0.5f, 2.0f, scale);
    processorRef.uiScale = scale;
    ui.setTransform (juce::AffineTransform::scale (scale));
    setSize (juce::roundToInt ((float) PadSlicerUI::designWidth * scale),
             juce::roundToInt ((float) PadSlicerUI::designHeight * scale));
}
