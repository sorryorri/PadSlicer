#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Generator.h"
#include "PluginProcessor.h"

//==============================================================================
// Piano roll for the generator's pattern, edited the way Ableton's MIDI editor works.
//
// Mouse, select mode (default): drag on empty space to select notes, drag in the bar ruler to select time,
//                               click a note to select it (Shift adds), drag notes to move them (Option-drag copies),
//                               double-click to add or delete a note.
// Mouse, draw mode (B):         click to add or remove notes, drag to paint.
// Keys: Delete/Backspace, Cmd+D duplicate, Cmd+C / Cmd+X / Cmd+V, Cmd+A, Cmd+Z / Cmd+Shift+Z,
//       arrows move notes (Shift+Left/Right by a bar), B toggles draw mode, Esc deselects.
class PatternEditor final : public juce::Component
{
public:
    explicit PatternEditor (PadSlicerAudioProcessor&);

    // Set up by the GEN page
    const Generator::Pattern* pattern = nullptr;
    std::function<std::vector<int>()> getSourceNotes;   // the slices or pads the pattern can play
    std::function<void()> onEdited;                     // rebuild the pattern after an edit
    std::function<void (const juce::String&)> onStatus;
    std::function<void()> onImport;
    std::function<void()> onClose;

    // Call before changing the pattern from outside (GENERATE, import) so Cmd+Z can undo it
    void pushUndoState();
    void clearSelection();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Drag { none, marquee, ruler, move, paintAdd, paintErase };

    struct Span
    {
        double start = 0.0, end = 0.0;
        bool isEmpty() const { return end <= start; }
    };

    // Layout
    juce::Rectangle<float> getGrid() const;
    juce::Rectangle<float> getRuler() const;
    juce::Rectangle<float> getModeButton() const;
    std::vector<int> getRows() const;
    int getVisibleRows (int numRows) const;
    float getRowHeight (int numRows) const;
    double getStepBeats() const;
    double getLengthBeats() const;
    float xForBeat (double beat) const;
    double beatAt (float x) const;
    double snapDown (double beat) const;
    int rowAt (float y, int numRows) const;   // index into getRows(), or -1
    juce::Rectangle<float> getNoteBounds (const Generator::Note&, const std::vector<int>& rows) const;
    int noteAt (juce::Point<float>) const;    // index into the pattern, or -1

    // Selection
    bool isSelected (const Generator::Note&) const;
    std::vector<Generator::Note> getSelectedOrInTime() const;
    Span getSelectionSpan() const;
    void selectNotesIn (Span);
    void getMoveDelta (int& steps, int& rows) const;

    // Edits
    void removeNote (const Generator::Note&);
    // Adds a note unless an identical one is already there (notes in `ignoring` are about to move away)
    void addNote (const Generator::Note&, const std::vector<Generator::Note>& ignoring = {});
    void growToFit (double endBeat);
    void edited (const juce::String& status = {});
    void paintAt (juce::Point<float>);
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void selectAll();
    void moveSelection (int steps, int rows, bool copy);
    void undo();
    void redo();
    void toggleDrawMode();
    void showMenu();

    PadSlicerAudioProcessor& processorRef;

    std::vector<Generator::Note> selected, selectionBeforeDrag;
    Span timeSelection;
    double insertBeat = 0.0;

    std::vector<Generator::Note> clipboard;   // times relative to the start of what was copied
    double clipboardLength = 0.0;

    std::vector<Generator::Settings> undoStack, redoStack;

    bool drawMode = false;
    int scrollRow = 0;
    Drag drag = Drag::none;
    juce::Point<float> dragStart, dragNow;
    bool dragCopies = false;
    std::vector<std::pair<int, int>> paintedCells;   // (step, row) already painted in this drag

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternEditor)
};
