#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

//==============================================================================
// Finds the hits (transients) in a loop, for slicing by hits instead of an even grid.
namespace Transients
{
    // Returns the start sample of each detected hit, in time order, at most maxHits of them
    // (the strongest ones if there are more). sensitivity goes from 0 (only big hits) to 1 (everything).
    std::vector<int> findHits (const juce::AudioBuffer<float>& buffer, double sampleRate, float sensitivity, int maxHits);
}
