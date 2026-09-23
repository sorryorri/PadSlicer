#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Look and drawing helpers for PadSlicer's 8-bit handheld console UI.
namespace Retro
{
    namespace Palette
    {
        // Night-sky navy shell
        inline const juce::Colour shellLight  { 0xff1c2b5a };
        inline const juce::Colour shellDark   { 0xff0b1330 };
        inline const juce::Colour shellRim    { 0xff3e5a9e };
        inline const juce::Colour shellShadow { 0xff050a1c };
        inline const juce::Colour panel       { 0xff101b3d };
        inline const juce::Colour panelHeader { 0xff0a1128 };
        inline const juce::Colour ice         { 0xffdce8ff };
        inline const juce::Colour bezel       { 0xff070b18 };
        inline const juce::Colour stone       { 0xff2a3a66 };

        // Amber LCD, darkest to brightest
        inline const juce::Colour lcdBackground { 0xff1a1105 };
        inline const juce::Colour lcdDim        { 0xff4a3010 };
        inline const juce::Colour lcdMid        { 0xffb3741c };
        inline const juce::Colour lcdBright     { 0xffffc85a };

        inline const juce::Colour statusBar     { 0xff2ec4b6 };
        inline const juce::Colour statusText    { 0xff04201d };

        inline const juce::Colour gold   { 0xffffc145 };
        inline const juce::Colour mint   { 0xff5ce1a6 };
        inline const juce::Colour coral  { 0xffff6b5b };
        inline const juce::Colour sky    { 0xff5bb8ff };
        inline const juce::Colour silver { 0xffc9d3e6 };
        inline const juce::Colour violet { 0xffb18cff };
        inline const juce::Colour red    { 0xffe8384f };
        inline const juce::Colour blue   { 0xff3f6fe0 };
    }

    //==============================================================================
    // 5x7 bitmap font. Text is upper-cased and accented letters are folded to ASCII.
    constexpr int glyphHeight = 7;

    int pixelTextWidth (const juce::String& text, int pixelSize);

    // Draws crisp pixel text into area. Shrinks the pixel size, then truncates, if it doesn't fit.
    void drawPixelText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area,
                        int pixelSize, juce::Colour colour,
                        juce::Justification justification = juce::Justification::centred,
                        juce::Colour shadow = juce::Colours::transparentBlack);

    //==============================================================================
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawLabel (juce::Graphics&, juce::Label&) override;

        // Slider text boxes inherit the slider's "pixelSize" property
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };

    //==============================================================================
    // Chunky square console button with a pixel caption underneath.
    class ArcadeButton final : public juce::Button
    {
    public:
        ArcadeButton (const juce::String& caption, juce::Colour colour);

        void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

    private:
        juce::Colour colour;
    };

    //==============================================================================
    // Folder-style tab that sits on top of the screen bezel. Toggle state = selected.
    class TabButton final : public juce::Button
    {
    public:
        explicit TabButton (const juce::String& name);

        void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
    };

    //==============================================================================
    // Small ON/OFF switch drawn in the LCD's amber.
    class LedButton final : public juce::Button
    {
    public:
        LedButton();

        void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
    };

    //==============================================================================
    // Small labelled chip. Toggle state = selected.
    class ChipButton final : public juce::Button
    {
    public:
        enum class Style
        {
            lcd,      // amber, drawn on the screen
            panel,    // gold on navy, for the knob panel
            menu      // status-bar dropdown with a little arrow
        };

        ChipButton (const juce::String& text, Style);

        void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

    private:
        Style style;
    };
}
