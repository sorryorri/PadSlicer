#include "PatternEditor.h"
#include "Retro.h"

namespace
{
    using namespace Retro;

    constexpr float labelWidth = 38.0f;
    constexpr float rulerHeight = 10.0f;
    constexpr float minimumRowHeight = 12.0f;
    constexpr size_t maxUndoSteps = 64;

    Generator::EditKey keyOf (const Generator::Note& note)
    {
        return { note.start, note.note };
    }
}

//==============================================================================
PatternEditor::PatternEditor (PadSlicerAudioProcessor& p)
    : processorRef (p)
{
    setWantsKeyboardFocus (true);
}

//==============================================================================
juce::Rectangle<float> PatternEditor::getGrid() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (labelWidth).withTrimmedTop (rulerHeight + 2.0f);
}

juce::Rectangle<float> PatternEditor::getRuler() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (labelWidth).withHeight (rulerHeight);
}

juce::Rectangle<float> PatternEditor::getModeButton() const
{
    return { 0.0f, 0.0f, labelWidth - 4.0f, rulerHeight };
}

std::vector<int> PatternEditor::getRows() const
{
    // One row per slice or pad, plus any note in the pattern that isn't one of them any more
    std::vector<int> rows;

    if (getSourceNotes != nullptr)
        rows = getSourceNotes();

    if (pattern != nullptr)
        for (const auto& note : pattern->notes)
            if (std::find (rows.begin(), rows.end(), note.note) == rows.end())
                rows.push_back (note.note);

    std::sort (rows.begin(), rows.end());
    return rows;
}

int PatternEditor::getVisibleRows (int numRows) const
{
    return juce::jlimit (1, juce::jmax (1, numRows), (int) (getGrid().getHeight() / minimumRowHeight));
}

float PatternEditor::getRowHeight (int numRows) const
{
    return getGrid().getHeight() / (float) getVisibleRows (numRows);
}

double PatternEditor::getStepBeats() const
{
    return Generator::getStepBeats (processorRef.generatorSettings);
}

double PatternEditor::getLengthBeats() const
{
    return pattern != nullptr ? pattern->lengthBeats : 4.0;
}

float PatternEditor::xForBeat (double beat) const
{
    const auto grid = getGrid();
    return grid.getX() + grid.getWidth() * (float) (beat / getLengthBeats());
}

double PatternEditor::beatAt (float x) const
{
    const auto grid = getGrid();
    return (double) ((x - grid.getX()) / grid.getWidth()) * getLengthBeats();
}

double PatternEditor::snapDown (double beat) const
{
    const double step = getStepBeats();
    return juce::jlimit (0.0, getLengthBeats() - step, std::floor ((beat + 1.0e-9) / step) * step);
}

int PatternEditor::rowAt (float y, int numRows) const
{
    const auto grid = getGrid();
    const int visibleRow = (int) std::floor ((grid.getBottom() - y) / getRowHeight (numRows));

    if (visibleRow < 0 || visibleRow >= getVisibleRows (numRows))
        return -1;

    const int row = scrollRow + visibleRow;
    return row < numRows ? row : -1;
}

juce::Rectangle<float> PatternEditor::getNoteBounds (const Generator::Note& note, const std::vector<int>& rows) const
{
    const auto grid = getGrid();
    const auto numRows = (int) rows.size();
    const int visibleRow = (int) (std::find (rows.begin(), rows.end(), note.note) - rows.begin()) - scrollRow;

    if (visibleRow < 0 || visibleRow >= getVisibleRows (numRows))
        return {};

    const float rowHeight = getRowHeight (numRows);
    const float x = xForBeat (note.start);
    const float width = juce::jmax (3.0f, xForBeat (note.start + note.length) - x - 1.0f);

    return { x, grid.getBottom() - (float) (visibleRow + 1) * rowHeight + 1.0f, width, juce::jmax (2.0f, rowHeight - 2.0f) };
}

int PatternEditor::noteAt (juce::Point<float> position) const
{
    if (pattern == nullptr)
        return -1;

    const auto rows = getRows();

    for (int i = (int) pattern->notes.size(); --i >= 0;)
        if (getNoteBounds (pattern->notes[(size_t) i], rows).contains (position))
            return i;

    return -1;
}

//==============================================================================
bool PatternEditor::isSelected (const Generator::Note& note) const
{
    return std::any_of (selected.begin(), selected.end(),
                        [&note] (const Generator::Note& s) { return Generator::isSameNote (note, keyOf (s)); });
}

std::vector<Generator::Note> PatternEditor::getSelectedOrInTime() const
{
    if (! selected.empty() || pattern == nullptr || timeSelection.isEmpty())
        return selected;

    std::vector<Generator::Note> inTime;

    for (const auto& note : pattern->notes)
        if (note.start >= timeSelection.start - 1.0e-6 && note.start < timeSelection.end - 1.0e-6)
            inTime.push_back (note);

    return inTime;
}

PatternEditor::Span PatternEditor::getSelectionSpan() const
{
    if (! timeSelection.isEmpty() || selected.empty())
        return timeSelection;

    // The steps the selected notes cover
    double first = selected.front().start, last = 0.0;

    for (const auto& note : selected)
    {
        first = juce::jmin (first, note.start);
        last = juce::jmax (last, note.start + note.length);
    }

    const double step = getStepBeats();
    return { snapDown (first), std::ceil ((last - 1.0e-6) / step) * step };
}

void PatternEditor::selectNotesIn (Span span)
{
    selected.clear();

    if (pattern != nullptr)
        for (const auto& note : pattern->notes)
            if (note.start >= span.start - 1.0e-6 && note.start < span.end - 1.0e-6)
                selected.push_back (note);
}

void PatternEditor::clearSelection()
{
    selected.clear();
    timeSelection = {};
    repaint();
}

void PatternEditor::getMoveDelta (int& steps, int& rowDelta) const
{
    const auto rows = getRows();
    const auto numRows = (int) rows.size();
    const double step = getStepBeats();
    const float stepWidth = xForBeat (step) - xForBeat (0.0);

    steps = juce::roundToInt ((dragNow.x - dragStart.x) / stepWidth);
    rowDelta = juce::roundToInt ((dragStart.y - dragNow.y) / getRowHeight (numRows));

    if (selected.empty() || numRows == 0)
        return;

    // Keep every moved note inside the clip and on a row
    double firstStart = getLengthBeats(), lastStart = 0.0;
    int lowestRow = numRows, highestRow = -1;

    for (const auto& note : selected)
    {
        firstStart = juce::jmin (firstStart, note.start);
        lastStart = juce::jmax (lastStart, note.start);
        const auto row = (int) (std::find (rows.begin(), rows.end(), note.note) - rows.begin());
        lowestRow = juce::jmin (lowestRow, row);
        highestRow = juce::jmax (highestRow, row);
    }

    steps = juce::jlimit ((int) std::ceil (-firstStart / step - 1.0e-6),
                          (int) std::floor ((getLengthBeats() - 1.0e-6 - lastStart) / step), steps);
    rowDelta = juce::jlimit (-lowestRow, numRows - 1 - highestRow, rowDelta);
}

//==============================================================================
void PatternEditor::pushUndoState()
{
    undoStack.push_back (processorRef.generatorSettings);

    if (undoStack.size() > maxUndoSteps)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

void PatternEditor::removeNote (const Generator::Note& note)
{
    auto& settings = processorRef.generatorSettings;
    const auto key = keyOf (note);
    const auto drawn = std::find_if (settings.added.begin(), settings.added.end(),
                                     [&key] (const Generator::Note& added) { return Generator::isSameNote (added, key); });

    // A hand-drawn note just goes away; a generated one is remembered as erased
    if (drawn != settings.added.end())
        settings.added.erase (drawn);
    else
        settings.removed.push_back (key);
}

void PatternEditor::addNote (const Generator::Note& note, const std::vector<Generator::Note>& ignoring)
{
    auto& settings = processorRef.generatorSettings;
    const auto key = keyOf (note);
    auto matches = [&key] (const Generator::Note& other) { return Generator::isSameNote (other, key); };

    // Like Ableton, landing on an identical note doesn't stack a second one
    const bool alreadyThere = (pattern != nullptr && std::any_of (pattern->notes.begin(), pattern->notes.end(), matches)
                                                  && ! std::any_of (ignoring.begin(), ignoring.end(), matches))
                              || std::any_of (settings.added.begin(), settings.added.end(), matches);

    if (alreadyThere)
        return;

    // Putting a note back exactly where a generated one was erased just un-erases it
    const auto erased = std::find_if (settings.removed.begin(), settings.removed.end(),
                                      [&note] (const Generator::EditKey& erasedKey) { return Generator::isSameNote (note, erasedKey); });

    if (erased != settings.removed.end())
        settings.removed.erase (erased);
    else
        settings.added.push_back (note);
}

void PatternEditor::growToFit (double endBeat)
{
    // Pasting or duplicating past the end makes the clip longer, up to 8 bars
    auto& settings = processorRef.generatorSettings;

    while (settings.getInt (Generator::bars) < 3 && endBeat > Generator::getPatternBeats (settings) + 1.0e-6)
        settings.values[Generator::bars] += 1.0f;
}

void PatternEditor::edited (const juce::String& status)
{
    if (onEdited != nullptr)
        onEdited();

    if (status.isNotEmpty() && onStatus != nullptr)
        onStatus (status);

    repaint();
}

void PatternEditor::paintAt (juce::Point<float> position)
{
    const auto rows = getRows();
    const int row = rowAt (position.y, (int) rows.size());

    if (row < 0 || pattern == nullptr || ! getGrid().contains (position))
        return;

    const double step = getStepBeats();
    const int stepIndex = (int) std::floor (beatAt (position.x) / step);
    const std::pair<int, int> cell { stepIndex, row };

    if (std::find (paintedCells.begin(), paintedCells.end(), cell) != paintedCells.end())
        return;

    paintedCells.push_back (cell);

    std::vector<Generator::Note> inCell;

    for (const auto& note : pattern->notes)
        if (note.note == rows[(size_t) row] && (int) std::floor ((note.start + 1.0e-6) / step) == stepIndex)
            inCell.push_back (note);

    if (drag == Drag::paintErase)
    {
        for (const auto& note : inCell)
            removeNote (note);
    }
    else if (inCell.empty())
    {
        const auto& settings = processorRef.generatorSettings;
        addNote ({ Generator::getStepStart (settings, stepIndex), Generator::getDrawnNoteBeats (settings), rows[(size_t) row], 0.8f });
    }
    else
    {
        return;
    }

    edited();
}

void PatternEditor::deleteSelection()
{
    const auto notes = getSelectedOrInTime();

    if (notes.empty())
        return;

    pushUndoState();

    for (const auto& note : notes)
        removeNote (note);

    selected.clear();
    edited ("DELETED " + juce::String ((int) notes.size()) + (notes.size() == 1 ? " NOTE" : " NOTES"));
}

void PatternEditor::duplicateSelection()
{
    const auto notes = getSelectedOrInTime();
    const auto span = getSelectionSpan();

    if (span.isEmpty())
    {
        if (onStatus != nullptr)
            onStatus ("SELECT NOTES OR BARS FIRST");

        return;
    }

    pushUndoState();

    // The copy goes right after the selection, like Cmd+D in Ableton
    const double offset = span.end - span.start;
    growToFit (span.end + offset);
    const double length = Generator::getPatternBeats (processorRef.generatorSettings);

    std::vector<Generator::Note> copies;

    for (auto note : notes)
    {
        note.start += offset;

        if (note.start < length - 1.0e-6)
        {
            addNote (note);
            copies.push_back (note);
        }
    }

    selected = copies;

    if (! timeSelection.isEmpty())
        timeSelection = { span.start + offset, juce::jmin (length, span.end + offset) };

    edited ("DUPLICATED");
}

void PatternEditor::copySelection()
{
    const auto notes = getSelectedOrInTime();
    const auto span = getSelectionSpan();

    if (notes.empty() && span.isEmpty())
        return;

    clipboard.clear();

    for (auto note : notes)
    {
        note.start -= span.start;
        clipboard.push_back (note);
    }

    clipboardLength = span.end - span.start;

    if (onStatus != nullptr)
        onStatus ("COPIED " + juce::String ((int) notes.size()) + (notes.size() == 1 ? " NOTE" : " NOTES"));
}

void PatternEditor::cutSelection()
{
    copySelection();
    deleteSelection();
}

void PatternEditor::paste()
{
    if (clipboard.empty() && clipboardLength <= 0.0)
        return;

    pushUndoState();

    // Paste after the current selection, or where you last clicked
    const double at = ! timeSelection.isEmpty() ? timeSelection.end : insertBeat;
    growToFit (at + clipboardLength);
    const double length = Generator::getPatternBeats (processorRef.generatorSettings);

    selected.clear();

    for (auto note : clipboard)
    {
        note.start += at;

        if (note.start < length - 1.0e-6)
        {
            addNote (note);
            selected.push_back (note);
        }
    }

    timeSelection = { at, juce::jmin (length, at + clipboardLength) };
    edited ("PASTED");
}

void PatternEditor::selectAll()
{
    if (pattern == nullptr)
        return;

    selected = pattern->notes;
    timeSelection = { 0.0, getLengthBeats() };
    repaint();
}

void PatternEditor::moveSelection (int steps, int rowDelta, bool copy)
{
    if (selected.empty())
        return;

    const auto rows = getRows();

    // Clamp the move the same way dragging does
    {
        const double step = getStepBeats();
        double firstStart = getLengthBeats(), lastStart = 0.0;
        int lowestRow = (int) rows.size(), highestRow = -1;

        for (const auto& note : selected)
        {
            firstStart = juce::jmin (firstStart, note.start);
            lastStart = juce::jmax (lastStart, note.start);
            const auto row = (int) (std::find (rows.begin(), rows.end(), note.note) - rows.begin());
            lowestRow = juce::jmin (lowestRow, row);
            highestRow = juce::jmax (highestRow, row);
        }

        steps = juce::jlimit ((int) std::ceil (-firstStart / step - 1.0e-6),
                              (int) std::floor ((getLengthBeats() - 1.0e-6 - lastStart) / step), steps);
        rowDelta = juce::jlimit (-lowestRow, (int) rows.size() - 1 - highestRow, rowDelta);
    }

    if (steps == 0 && rowDelta == 0)
        return;

    pushUndoState();

    const double shift = steps * getStepBeats();
    std::vector<Generator::Note> moved;

    for (const auto& note : selected)
    {
        auto newNote = note;
        const auto row = (size_t) (std::find (rows.begin(), rows.end(), note.note) - rows.begin());
        newNote.start += shift;
        newNote.note = rows[(size_t) ((int) row + rowDelta)];
        moved.push_back (newNote);
    }

    if (! copy)
        for (const auto& note : selected)
            removeNote (note);

    for (const auto& note : moved)
        addNote (note, copy ? std::vector<Generator::Note>() : selected);

    selected = moved;

    if (! timeSelection.isEmpty())
        timeSelection = { timeSelection.start + shift, timeSelection.end + shift };

    edited();
}

void PatternEditor::undo()
{
    if (undoStack.empty())
        return;

    redoStack.push_back (processorRef.generatorSettings);
    processorRef.generatorSettings = undoStack.back();
    undoStack.pop_back();
    clearSelection();
    edited ("UNDO");
}

void PatternEditor::redo()
{
    if (redoStack.empty())
        return;

    undoStack.push_back (processorRef.generatorSettings);
    processorRef.generatorSettings = redoStack.back();
    redoStack.pop_back();
    clearSelection();
    edited ("REDO");
}

void PatternEditor::toggleDrawMode()
{
    drawMode = ! drawMode;

    if (onStatus != nullptr)
        onStatus (drawMode ? "DRAW MODE: CLICK TO ADD/REMOVE" : "SELECT MODE: DRAG TO SELECT");

    repaint();
}

//==============================================================================
void PatternEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStart = dragNow = e.position;
    drag = Drag::none;

    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (getModeButton().contains (e.position))
    {
        toggleDrawMode();
        return;
    }

    if (pattern == nullptr || getRows().empty())
        return;

    // The bar ruler selects time: whole steps, beats or bars
    if (getRuler().contains (e.position))
    {
        const double beat = snapDown (beatAt (e.position.x));
        timeSelection = { beat, beat + getStepBeats() };
        insertBeat = beat;
        selectNotesIn (timeSelection);
        drag = Drag::ruler;
        repaint();
        return;
    }

    if (! getGrid().contains (e.position))
        return;

    const int hit = noteAt (e.position);

    if (drawMode)
    {
        pushUndoState();
        paintedCells.clear();
        drag = hit >= 0 ? Drag::paintErase : Drag::paintAdd;
        paintAt (e.position);
        return;
    }

    if (hit >= 0)
    {
        const auto note = pattern->notes[(size_t) hit];

        if (e.mods.isShiftDown())
        {
            if (isSelected (note))
                selected.erase (std::remove_if (selected.begin(), selected.end(),
                                                [&note] (const Generator::Note& s) { return Generator::isSameNote (note, keyOf (s)); }),
                                selected.end());
            else
                selected.push_back (note);
        }
        else if (! isSelected (note))
        {
            selected = { note };
        }

        timeSelection = {};
        drag = Drag::move;
        dragCopies = e.mods.isAltDown();
        repaint();
        return;
    }

    // Empty space: start a selection box
    if (! e.mods.isShiftDown())
        selected.clear();

    selectionBeforeDrag = selected;
    timeSelection = {};
    insertBeat = snapDown (beatAt (e.position.x));
    drag = Drag::marquee;
    repaint();
}

void PatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    dragNow = e.position;

    switch (drag)
    {
        case Drag::marquee:
        {
            const juce::Rectangle<float> box (dragStart, dragNow);
            const auto rows = getRows();
            selected = selectionBeforeDrag;

            if (pattern != nullptr)
                for (const auto& note : pattern->notes)
                    if (! isSelected (note) && getNoteBounds (note, rows).intersects (box))
                        selected.push_back (note);

            break;
        }

        case Drag::ruler:
        {
            const double a = snapDown (beatAt (dragStart.x));
            const double b = snapDown (beatAt (dragNow.x));
            timeSelection = { juce::jmin (a, b), juce::jmin (getLengthBeats(), juce::jmax (a, b) + getStepBeats()) };
            selectNotesIn (timeSelection);
            break;
        }

        case Drag::paintAdd:
        case Drag::paintErase:
            paintAt (e.position);
            break;

        case Drag::move:
        case Drag::none:
            break;
    }

    repaint();
}

void PatternEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::move)
    {
        int steps = 0, rowDelta = 0;
        getMoveDelta (steps, rowDelta);

        if (steps != 0 || rowDelta != 0)
            moveSelection (steps, rowDelta, dragCopies);
    }

    drag = Drag::none;
    paintedCells.clear();
    repaint();
}

void PatternEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // In select mode, double-click adds a note, or deletes the one under the pointer
    if (drawMode || pattern == nullptr || ! getGrid().contains (e.position))
        return;

    if (const int hit = noteAt (e.position); hit >= 0)
    {
        pushUndoState();
        removeNote (pattern->notes[(size_t) hit]);
        selected.clear();
        edited();
        return;
    }

    const auto rows = getRows();
    const int row = rowAt (e.position.y, (int) rows.size());

    if (row < 0)
        return;

    const auto& settings = processorRef.generatorSettings;
    const int stepIndex = (int) std::floor (beatAt (e.position.x) / getStepBeats());
    const Generator::Note note { Generator::getStepStart (settings, stepIndex), Generator::getDrawnNoteBeats (settings),
                                 rows[(size_t) row], 0.8f };

    pushUndoState();
    addNote (note);
    selected = { note };
    edited();
}

void PatternEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int numRows = (int) getRows().size();
    const int maxScroll = juce::jmax (0, numRows - getVisibleRows (numRows));

    if (maxScroll == 0)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    // Scroll through the rows when there are more slices than fit
    scrollRow = juce::jlimit (0, maxScroll, scrollRow + (wheel.deltaY > 0.0f ? 1 : -1));
    repaint();
}

bool PatternEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool command = mods.isCommandDown();
    const auto character = juce::CharacterFunctions::toUpperCase (key.getTextCharacter() != 0 ? key.getTextCharacter()
                                                                                               : (juce::juce_wchar) key.getKeyCode());

    if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }

    if (command)
    {
        switch (character)
        {
            case 'D': duplicateSelection(); return true;
            case 'C': copySelection(); return true;
            case 'X': cutSelection(); return true;
            case 'V': paste(); return true;
            case 'A': selectAll(); return true;
            case 'Z': if (mods.isShiftDown()) redo(); else undo(); return true;
            default: break;
        }

        return false;
    }

    // Arrows move the selection: a step or a row, or a bar with Shift+Left/Right
    const int stepsPerBar = juce::roundToInt (4.0 / getStepBeats());

    if (key.getKeyCode() == juce::KeyPress::leftKey)  { moveSelection (mods.isShiftDown() ? -stepsPerBar : -1, 0, false); return true; }
    if (key.getKeyCode() == juce::KeyPress::rightKey) { moveSelection (mods.isShiftDown() ? stepsPerBar : 1, 0, false); return true; }
    if (key.getKeyCode() == juce::KeyPress::upKey)    { moveSelection (0, 1, false); return true; }
    if (key.getKeyCode() == juce::KeyPress::downKey)  { moveSelection (0, -1, false); return true; }

    if (character == 'B' && ! mods.isAnyModifierKeyDown())
    {
        toggleDrawMode();
        return true;
    }

    return false;
}

void PatternEditor::showMenu()
{
    const bool hasSelection = ! getSelectedOrInTime().empty() || ! timeSelection.isEmpty();

    auto item = [] (juce::PopupMenu& menu, int id, const juce::String& text, const juce::String& shortcut, bool enabled, bool ticked = false)
    {
        juce::PopupMenu::Item entry (text);
        entry.itemID = id;
        entry.shortcutKeyDescription = shortcut;
        entry.isEnabled = enabled;
        entry.isTicked = ticked;
        menu.addItem (entry);
    };

    juce::PopupMenu menu;
    item (menu, 1, "Duplicate", "Cmd+D", hasSelection);
    item (menu, 2, "Delete", "Delete", hasSelection);
    item (menu, 3, "Copy", "Cmd+C", hasSelection);
    item (menu, 4, "Cut", "Cmd+X", hasSelection);
    item (menu, 5, "Paste", "Cmd+V", ! clipboard.empty() || clipboardLength > 0.0);
    item (menu, 6, "Select All", "Cmd+A", pattern != nullptr && ! pattern->notes.empty());
    menu.addSeparator();
    item (menu, 7, "Undo", "Cmd+Z", ! undoStack.empty());
    item (menu, 8, "Redo", "Cmd+Shift+Z", ! redoStack.empty());
    menu.addSeparator();
    item (menu, 9, "Draw Mode", "B", true, drawMode);
    menu.addSeparator();
    item (menu, 10, "Clear Pattern (Draw From Scratch)", {}, true);
    item (menu, 11, "Remove All My Edits", {}, processorRef.generatorSettings.hasEdits());
    item (menu, 12, "Import MIDI File...", {}, true);
    item (menu, 13, "Done Editing", {}, true);

    juce::Component::SafePointer<PatternEditor> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safeThis] (int result)
    {
        if (safeThis == nullptr)
            return;

        auto& self = *safeThis;
        auto& settings = self.processorRef.generatorSettings;

        switch (result)
        {
            case 1:  self.duplicateSelection(); break;
            case 2:  self.deleteSelection(); break;
            case 3:  self.copySelection(); break;
            case 4:  self.cutSelection(); break;
            case 5:  self.paste(); break;
            case 6:  self.selectAll(); break;
            case 7:  self.undo(); break;
            case 8:  self.redo(); break;
            case 9:  self.toggleDrawMode(); break;

            case 10:
            case 11:
                self.pushUndoState();

                if (result == 10)
                    settings.seed = 0;

                settings.added.clear();
                settings.removed.clear();
                self.clearSelection();
                self.edited (result == 10 ? "EMPTY PATTERN: DOUBLE-CLICK TO DRAW" : "EDITS REMOVED");
                break;

            case 12: if (self.onImport != nullptr) self.onImport(); break;
            case 13: if (self.onClose != nullptr) self.onClose(); break;
            default: break;
        }
    });
}

//==============================================================================
void PatternEditor::paint (juce::Graphics& g)
{
    const auto rows = getRows();
    const auto grid = getGrid();
    const auto ruler = getRuler();

    // SEL / DRAW switch in the corner
    const auto modeButton = getModeButton();

    if (drawMode)
    {
        g.setColour (Palette::lcdBright);
        g.fillRect (modeButton);
    }
    else
    {
        g.setColour (Palette::lcdMid);
        g.drawRect (modeButton, 1.0f);
    }

    drawPixelText (g, drawMode ? "DRAW" : "SEL", modeButton, 1, drawMode ? Palette::lcdBackground : Palette::lcdMid);

    g.setColour (Palette::lcdDim);
    g.drawRect (grid, 1.0f);

    if (pattern == nullptr || rows.empty())
    {
        drawPixelText (g, "LOAD A LOOP OR PADS FIRST", grid, 1, Palette::lcdMid);
        return;
    }

    const auto numRows = (int) rows.size();
    const int visible = getVisibleRows (numRows);
    scrollRow = juce::jlimit (0, juce::jmax (0, numRows - visible), scrollRow);

    const float rowHeight = getRowHeight (numRows);
    const double length = getLengthBeats();
    const double step = getStepBeats();
    const int totalSteps = juce::jmax (1, juce::roundToInt (length / step));
    const int stepsPerBeat = juce::roundToInt (1.0 / step);

    // Rows, lowest note at the bottom, with their note names
    for (int r = 0; r < visible && scrollRow + r < numRows; ++r)
    {
        const int row = scrollRow + r;
        const float y = grid.getBottom() - (float) (r + 1) * rowHeight;

        if (row % 2 == 0)
        {
            g.setColour (Palette::lcdDim.withAlpha (0.25f));
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);
        }

        drawPixelText (g, juce::MidiMessage::getMidiNoteName (rows[(size_t) row], true, true, 3),
                       juce::Rectangle<float> (2.0f, y, labelWidth - 4.0f, rowHeight), 1, Palette::lcdMid,
                       juce::Justification::centredLeft);
    }

    // Time selection, in the ruler and across the notes
    if (! timeSelection.isEmpty())
    {
        const float x0 = xForBeat (timeSelection.start);
        const float x1 = xForBeat (timeSelection.end);

        g.setColour (Palette::lcdDim.withAlpha (0.55f));
        g.fillRect (x0, grid.getY(), x1 - x0, grid.getHeight());
        g.setColour (Palette::lcdBright.withAlpha (0.5f));
        g.fillRect (x0, ruler.getY(), x1 - x0, ruler.getHeight());
    }

    // Bar numbers
    for (int bar = 0; bar * 4.0 < length; ++bar)
    {
        const float x = xForBeat (bar * 4.0);
        g.setColour (Palette::lcdMid);
        g.fillRect (std::round (x), ruler.getY(), 1.0f, ruler.getHeight());
        drawPixelText (g, juce::String (bar + 1), juce::Rectangle<float> (x + 3.0f, ruler.getY(), 24.0f, ruler.getHeight()), 1,
                       Palette::lcdBright, juce::Justification::centredLeft);
    }

    // Step, beat and bar lines
    for (int s = 1; s < totalSteps; ++s)
    {
        const bool bar = s % (stepsPerBeat * 4) == 0;
        const bool beat = s % stepsPerBeat == 0;
        g.setColour (bar ? Palette::lcdMid : (beat ? Palette::lcdDim : Palette::lcdDim.withAlpha (0.35f)));
        g.fillRect (std::round (xForBeat (s * step)), grid.getY(), 1.0f, grid.getHeight());
    }

    // Notes; while dragging, the selected ones show where they'll land
    int moveSteps = 0, moveRows = 0;

    if (drag == Drag::move)
        getMoveDelta (moveSteps, moveRows);

    const bool moving = drag == Drag::move && (moveSteps != 0 || moveRows != 0);
    const auto moveOffset = juce::Point<float> (xForBeat (moveSteps * step) - xForBeat (0.0), -(float) moveRows * rowHeight);

    for (const auto& note : pattern->notes)
    {
        const auto bounds = getNoteBounds (note, rows);

        if (bounds.isEmpty())
            continue;

        const bool isSel = isSelected (note);

        if (isSel && moving)
        {
            if (! dragCopies)
            {
                g.setColour (Palette::lcdDim);
                g.fillRect (bounds);
            }

            g.setColour (Palette::lcdBright);
            g.fillRect (bounds + moveOffset);
            continue;
        }

        if (isSel)
        {
            g.setColour (Palette::lcdBright);
            g.fillRect (bounds);
            g.setColour (Palette::lcdBackground);
            g.drawRect (bounds.reduced (1.0f), 1.0f);
        }
        else
        {
            g.setColour (Palette::lcdBright.withAlpha (0.35f + 0.45f * note.velocity));
            g.fillRect (bounds);
        }
    }

    // Selection box
    if (drag == Drag::marquee)
    {
        const juce::Rectangle<float> box (dragStart, dragNow);
        g.setColour (Palette::lcdBright);

        for (float x = box.getX(); x < box.getRight(); x += 4.0f)
        {
            g.fillRect (x, box.getY(), 2.0f, 1.0f);
            g.fillRect (x, box.getBottom(), 2.0f, 1.0f);
        }

        for (float y = box.getY(); y < box.getBottom(); y += 4.0f)
        {
            g.fillRect (box.getX(), y, 1.0f, 2.0f);
            g.fillRect (box.getRight(), y, 1.0f, 2.0f);
        }
    }

    if (processorRef.previewOn.load())
    {
        g.setColour (Palette::lcdBright);
        g.fillRect (std::round (xForBeat (processorRef.getPreviewPosition())), grid.getY(), 2.0f, grid.getHeight());
    }

    if (numRows > visible)
        drawPixelText (g, "SCROLL", juce::Rectangle<float> (0.0f, grid.getBottom() - 8.0f, labelWidth - 4.0f, 8.0f), 1, Palette::lcdDim);
}
