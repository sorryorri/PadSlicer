#include "Retro.h"

namespace Retro
{
//==============================================================================
namespace
{
    struct Glyph
    {
        char character;
        juce::uint8 rows[glyphHeight];   // bit 4 is the leftmost column
    };

    constexpr Glyph glyphs[] =
    {
        { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
        { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
        { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
        { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
        { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
        { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
        { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
        { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
        { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
        { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
        { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
        { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
        { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
        { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
        { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
        { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
        { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
        { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
        { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } },
        { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
        { 'Y', { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 } },
        { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
        { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
        { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
        { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
        { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
        { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
        { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
        { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
        { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
        { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
        { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
        { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
        { ',', { 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 } },
        { ':', { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 } },
        { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
        { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
        { '=', { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 } },
        { '%', { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 } },
        { '/', { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 } },
        { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
        { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
        { '[', { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E } },
        { ']', { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E } },
        { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
        { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
        { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } },
        { '?', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 } },
        { '\'', { 0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
        { '#', { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A } },
        { '&', { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D } },
        { '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
    };

    const Glyph& glyphFor (juce::juce_wchar c)
    {
        for (const auto& glyph : glyphs)
            if ((juce::juce_wchar) glyph.character == c)
                return glyph;

        for (const auto& glyph : glyphs)
            if (glyph.character == '?')
                return glyph;

        return glyphs[0];
    }

    // Upper-cases and folds Latin-1 accents (Icelandic included) onto the glyphs we have
    juce::String toFontText (const juce::String& text)
    {
        juce::String result;
        result.preallocateBytes (text.getNumBytesAsUTF8());

        for (auto c : text)
        {
            if (c >= 0xC0 && c <= 0xDE && c != 0xD7)
                c += 0x20;   // Latin-1 upper case -> lower case

            switch (c)
            {
                case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: result << 'A'; break;
                case 0xE8: case 0xE9: case 0xEA: case 0xEB:                       result << 'E'; break;
                case 0xEC: case 0xED: case 0xEE: case 0xEF:                       result << 'I'; break;
                case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: result << 'O'; break;
                case 0xF9: case 0xFA: case 0xFB: case 0xFC:                       result << 'U'; break;
                case 0xFD: case 0xFF:                                             result << 'Y'; break;
                case 0xF0: result << 'D';  break;
                case 0xFE: result << "TH"; break;
                case 0xE6: result << "AE"; break;
                case 0xDF: result << "SS"; break;
                case 0xF1: result << 'N';  break;
                case 0xE7: result << 'C';  break;
                default:   result << juce::String::charToString (juce::CharacterFunctions::toUpperCase (c)); break;
            }
        }

        return result;
    }

    int widthOfFontText (const juce::String& fontText, int pixelSize)
    {
        const int numChars = fontText.length();
        return numChars == 0 ? 0 : (numChars * 6 - 1) * pixelSize;
    }

    void drawFontText (juce::Graphics& g, const juce::String& fontText, float x, float y, int pixelSize)
    {
        const auto size = (float) pixelSize;

        for (auto c : fontText)
        {
            const auto& glyph = glyphFor (c);

            for (int row = 0; row < glyphHeight; ++row)
            {
                // Fill horizontal runs in one go
                for (int column = 0; column < 5;)
                {
                    if (((glyph.rows[row] >> (4 - column)) & 1) == 0)
                    {
                        ++column;
                        continue;
                    }

                    int runEnd = column + 1;

                    while (runEnd < 5 && ((glyph.rows[row] >> (4 - runEnd)) & 1) != 0)
                        ++runEnd;

                    g.fillRect (x + (float) column * size, y + (float) row * size, (float) (runEnd - column) * size, size);
                    column = runEnd;
                }
            }

            x += 6.0f * size;
        }
    }
}

//==============================================================================
int pixelTextWidth (const juce::String& text, int pixelSize)
{
    return widthOfFontText (toFontText (text), pixelSize);
}

void drawPixelText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                    int pixelSize, juce::Colour colour, juce::Justification justification, juce::Colour shadow)
{
    auto fontText = toFontText (text);
    int size = juce::jmax (1, pixelSize);

    const auto fits = [&area] (const juce::String& t, int s) { return (float) widthOfFontText (t, s) <= area.getWidth(); };

    while (size > 1 && ! fits (fontText, size))
        --size;

    if (! fits (fontText, size))
    {
        while (fontText.length() > 1 && ! fits (fontText + "..", size))
            fontText = fontText.dropLastCharacters (1);

        fontText += "..";
    }

    const juce::Rectangle<float> textBounds ((float) widthOfFontText (fontText, size), (float) (glyphHeight * size));
    const auto placed = justification.appliedToRectangle (textBounds, area);
    const float x = std::round (placed.getX());
    const float y = std::round (placed.getY());

    if (! shadow.isTransparent())
    {
        g.setColour (shadow);
        drawFontText (g, fontText, x + (float) size, y + (float) size, size);
    }

    g.setColour (colour);
    drawFontText (g, fontText, x, y, size);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, Palette::panelHeader);
    setColour (juce::PopupMenu::textColourId, Palette::ice);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::blue);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::Label::textColourId, Palette::ice);
    setColour (juce::Slider::textBoxTextColourId, Palette::ice);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, Palette::panelHeader);
    setColour (juce::TextEditor::textColourId, Palette::ice);
    setColour (juce::TextEditor::highlightColourId, Palette::blue);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    // Drawn on a coarse grid so the knob reads as pixel art. Small knobs can ask for finer cells.
    const auto cell = (float) slider.getProperties().getWithDefault ("pixelCell", 4.0f);

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centre = juce::Point<float> (std::round (bounds.getCentreX()), std::round (bounds.getCentreY()));
    const int radiusCells = juce::jmax (4, (int) ((juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 10.0f) / cell));
    const float radius = (float) radiusCells * cell;
    const auto colour = slider.findColour (juce::Slider::thumbColourId);
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    if (! slider.isEnabled())
        g.beginTransparencyLayer (0.35f);

    // Tick pixels around the knob, lit up to the current value
    constexpr int numTicks = 11;
    const int litTicks = juce::roundToInt (sliderPos * (float) (numTicks - 1));

    for (int i = 0; i < numTicks; ++i)
    {
        const float tickAngle = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * (float) i / (float) (numTicks - 1);
        const auto position = centre.getPointOnCircumference (radius + 6.0f, tickAngle);

        g.setColour (i <= litTicks ? colour : juce::Colours::white.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (cell, cell).withCentre (position).toNearestInt().toFloat());
    }

    auto cellRect = [&] (int column, int row)
    {
        return juce::Rectangle<float> (centre.x + (float) column * cell, centre.y + (float) row * cell, cell, cell);
    };

    const auto outline = juce::Colour (0xff050914);

    for (int row = -radiusCells; row < radiusCells; ++row)
    {
        for (int column = -radiusCells; column < radiusCells; ++column)
        {
            const float cx = ((float) column + 0.5f) / (float) radiusCells;
            const float cy = ((float) row + 0.5f) / (float) radiusCells;
            const float distance = std::sqrt (cx * cx + cy * cy);

            if (distance > 1.0f)
                continue;

            juce::Colour fill;

            if (distance > 1.0f - 1.2f / (float) radiusCells)
            {
                fill = outline;
            }
            else
            {
                // Light from the top-left, in three flat bands
                const float light = -0.6f * cx - 0.8f * cy;
                fill = light > 0.45f ? colour.brighter (0.45f)
                     : light < -0.35f ? colour.darker (0.55f)
                                      : colour;
            }

            // One-cell drop shadow; rows below paint over it except at the bottom edge
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (cellRect (column, row + 1));

            g.setColour (fill);
            g.fillRect (cellRect (column, row));
        }
    }

    // Pointer: a line of cells from the middle towards the rim
    g.setColour (outline);

    for (float t = 0.15f; t <= 0.8f; t += 0.03f)
    {
        const auto p = juce::Point<float> (0.0f, 0.0f).getPointOnCircumference (radius * t, angle);
        g.fillRect (cellRect (juce::roundToInt (p.x / cell - 0.5f), juce::roundToInt (p.y / cell - 0.5f)));
    }

    if (! slider.isEnabled())
        g.endTransparencyLayer();
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
        return;

    const int pixelSize = label.getProperties().getWithDefault ("pixelSize", 2);
    auto colour = label.findColour (juce::Label::textColourId);

    if (! label.isEnabled())
        colour = colour.withMultipliedAlpha (0.4f);

    drawPixelText (g, label.getText(), label.getLocalBounds().toFloat(), pixelSize, colour,
                   label.getJustificationType(), juce::Colours::black.withAlpha (0.35f));
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->getProperties().set ("pixelSize", slider.getProperties().getWithDefault ("pixelSize", 2));
    return label;
}

//==============================================================================
ArcadeButton::ArcadeButton (const juce::String& caption, juce::Colour c)
    : juce::Button (caption), colour (c)
{
}

void ArcadeButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds().toFloat();
    const auto captionArea = bounds.removeFromBottom (16.0f);
    bounds.removeFromBottom (4.0f);

    constexpr float depth = 5.0f;
    const auto base = bounds.withTrimmedTop (depth).reduced (2.0f, 0.0f);
    auto face = base.translated (0.0f, -depth);

    if (isDown)
        face = face.translated (0.0f, depth - 1.0f);

    constexpr float corner = 4.0f;

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRoundedRectangle (base.translated (0.0f, 2.0f), corner);

    g.setColour (colour.darker (0.6f));
    g.fillRoundedRectangle (base, corner);

    const auto faceColour = isHighlighted ? colour.brighter (0.15f) : colour;
    g.setGradientFill (juce::ColourGradient (faceColour.brighter (0.35f), 0.0f, face.getY(),
                                             faceColour.darker (0.1f), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, corner);

    g.setColour (juce::Colours::white.withAlpha (0.45f));
    g.fillRect (face.reduced (6.0f, 0.0f).withHeight (3.0f).translated (0.0f, 3.0f));

    drawPixelText (g, getButtonText(), captionArea, 2, Palette::ice,
                   juce::Justification::centred, juce::Colours::black.withAlpha (0.35f));
}

//==============================================================================
TabButton::TabButton (const juce::String& name)
    : juce::Button (name)
{
    setClickingTogglesState (false);
}

void TabButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    const bool active = getToggleState();
    auto bounds = getLocalBounds().toFloat();

    // Active tab merges into the screen bezel below it
    if (! active)
        bounds = bounds.withTrimmedTop (4.0f).withTrimmedBottom (2.0f);

    juce::Path tab;
    tab.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                             6.0f, 6.0f, true, true, false, false);

    g.setColour (active ? Palette::bezel : Palette::panelHeader.brighter (isHighlighted ? 0.25f : 0.1f));
    g.fillPath (tab);

    if (active)
    {
        g.setColour (Palette::lcdBright);
        g.fillRect (bounds.withHeight (3.0f).reduced (8.0f, 0.0f).translated (0.0f, 3.0f));
    }

    drawPixelText (g, getButtonText(), bounds.withTrimmedTop (4.0f), 2,
                   active ? Palette::lcdBright : Palette::ice.withAlpha (isHighlighted ? 0.8f : 0.5f));
}

//==============================================================================
LedButton::LedButton()
    : juce::Button ({})
{
    setClickingTogglesState (true);
}

void LedButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();

    if (getToggleState())
    {
        g.setColour (Palette::lcdBright);
        g.fillRect (bounds);
        drawPixelText (g, "ON", bounds, 1, Palette::lcdBackground);
    }
    else
    {
        const auto colour = isHighlighted ? Palette::lcdBright : Palette::lcdMid;
        g.setColour (colour);
        g.drawRect (bounds, 1.0f);
        drawPixelText (g, "OFF", bounds, 1, colour);
    }
}

//==============================================================================
ChipButton::ChipButton (const juce::String& text, Style chipStyle)
    : juce::Button (text), style (chipStyle)
{
}

void ChipButton::paintButton (juce::Graphics& g, bool isHighlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();
    const bool selected = getToggleState();

    switch (style)
    {
        case Style::lcd:
        {
            const auto colour = selected || isHighlighted ? Palette::lcdBright : Palette::lcdMid;

            if (selected)
            {
                g.setColour (Palette::lcdBright);
                g.fillRect (bounds);
            }
            else
            {
                g.setColour (colour);
                g.drawRect (bounds, 1.0f);
            }

            drawPixelText (g, getButtonText(), bounds, 1, selected ? Palette::lcdBackground : colour);
            break;
        }

        case Style::panel:
        {
            g.setColour (selected ? Palette::gold : Palette::panelHeader);
            g.fillRoundedRectangle (bounds, 4.0f);
            g.setColour (selected ? Palette::gold.darker (0.4f) : Palette::shellRim.withAlpha (isHighlighted ? 0.9f : 0.5f));
            g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);
            drawPixelText (g, getButtonText(), bounds.reduced (6.0f, 0.0f), 1,
                           selected ? juce::Colour (0xff1a1025) : Palette::ice.withAlpha (isHighlighted ? 1.0f : 0.7f));
            break;
        }

        case Style::menu:
        {
            g.setColour (Palette::statusBar.darker (isHighlighted ? 0.45f : 0.3f));
            g.fillRoundedRectangle (bounds, 3.0f);

            auto textArea = bounds.reduced (6.0f, 0.0f);
            const auto arrowArea = textArea.removeFromRight (10.0f);

            juce::Path arrow;
            arrow.addTriangle (arrowArea.getX(), arrowArea.getCentreY() - 2.0f,
                               arrowArea.getRight(), arrowArea.getCentreY() - 2.0f,
                               arrowArea.getCentreX(), arrowArea.getCentreY() + 3.0f);
            g.setColour (Palette::statusText);
            g.fillPath (arrow);

            drawPixelText (g, getButtonText(), textArea, 2, Palette::statusText);
            break;
        }
    }
}
}
