#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
// Sir Slicealot, the PadSlicer mascot. His rank, gear and surroundings upgrade with
// his level (see KnightProgress): one new rank per level up to the last one.
namespace Knight
{
    constexpr int width = 24;    // sprite size in cells
    constexpr int height = 18;

    int getNumRanks();
    int getRankIndex (int level);            // 0-based; levels past the last rank keep the final look
    juce::String getRankName (int level);

    void draw (juce::Graphics&, juce::Point<float> topLeft, float pixelSize, int level, bool attacking, double timeMs);

    // Twinkling plus-shaped sparkles scattered over an area
    void drawSparkles (juce::Graphics&, juce::Rectangle<float> area, float pixelSize, int count, double timeMs, juce::int64 seed);

    // The backdrop for a rank. groundY is where the knight's feet go.
    void drawScenery (juce::Graphics&, juce::Rectangle<float> area, float groundY, int level, double timeMs);
}
