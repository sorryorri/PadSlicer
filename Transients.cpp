#include "Transients.h"

#include <algorithm>

namespace Transients
{
std::vector<int> findHits (const juce::AudioBuffer<float>& buffer, double sampleRate, float sensitivity, int maxHits)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0 || maxHits <= 0)
        return {};

    sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity);

    // Mono mix, plus a pre-emphasised copy so attacks stand out from sustained bass
    std::vector<float> mono ((size_t) numSamples), emphasised ((size_t) numSamples);
    float previous = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        float sum = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getSample (ch, n);

        mono[(size_t) n] = sum / (float) numChannels;
        emphasised[(size_t) n] = mono[(size_t) n] - 0.95f * previous;
        previous = mono[(size_t) n];
    }

    // Short-time energy in dB, every 5 ms over a 10 ms window
    const int hop = juce::jmax (32, juce::roundToInt (sampleRate * 0.005));
    const int window = hop * 2;
    const int numFrames = numSamples / hop;

    if (numFrames < 3)
        return {};

    std::vector<float> energy ((size_t) numFrames);
    float loudest = -200.0f;

    for (int f = 0; f < numFrames; ++f)
    {
        const int start = f * hop;
        const int end = juce::jmin (numSamples, start + window);
        double sum = 0.0;

        for (int n = start; n < end; ++n)
            sum += (double) emphasised[(size_t) n] * emphasised[(size_t) n];

        energy[(size_t) f] = 10.0f * std::log10 ((float) (sum / (double) juce::jmax (1, end - start)) + 1.0e-12f);
        loudest = juce::jmax (loudest, energy[(size_t) f]);
    }

    const float floorDb = loudest - 60.0f;

    for (auto& e : energy)
        e = juce::jmax (e, floorDb);

    // Onset strength: how much the energy jumped compared with two frames ago
    std::vector<float> rise ((size_t) numFrames, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        const float before = f >= 2 ? energy[(size_t) (f - 2)] : floorDb;
        rise[(size_t) f] = juce::jmax (0.0f, energy[(size_t) f] - before);
    }

    // Sensitivity loosens every threshold
    const float minRiseDb = juce::jmap (sensitivity, 12.0f, 3.0f);
    const float gateDb = loudest - juce::jmap (sensitivity, 24.0f, 48.0f);
    const float adaptiveFactor = juce::jmap (sensitivity, 2.5f, 1.2f);
    const int minGapFrames = juce::jmax (1, juce::roundToInt (juce::jmap (sensitivity, 0.09f, 0.03f) * sampleRate / hop));

    struct Hit { int frame; float strength; };
    std::vector<Hit> hits;

    for (int f = 0; f < numFrames; ++f)
    {
        const float strength = rise[(size_t) f];

        if (strength < minRiseDb)
            continue;

        // Ignore bumps that never get loud
        float loudestAhead = energy[(size_t) f];

        for (int k = f; k < juce::jmin (numFrames, f + 4); ++k)
            loudestAhead = juce::jmax (loudestAhead, energy[(size_t) k]);

        if (loudestAhead < gateDb)
            continue;

        // Must be the local peak of the onset curve
        bool isPeak = true;

        for (int d = -2; d <= 2 && isPeak; ++d)
        {
            const int k = f + d;

            if (d == 0 || k < 0 || k >= numFrames)
                continue;

            isPeak = d < 0 ? rise[(size_t) k] < strength : rise[(size_t) k] <= strength;
        }

        if (! isPeak)
            continue;

        // ...and clearly above the local average, so busy passages don't trigger everywhere
        float sum = 0.0f;
        int count = 0;

        for (int k = juce::jmax (0, f - 15); k <= juce::jmin (numFrames - 1, f + 15); ++k, ++count)
            sum += rise[(size_t) k];

        if (strength < adaptiveFactor * sum / (float) juce::jmax (1, count))
            continue;

        if (! hits.empty() && f - hits.back().frame < minGapFrames)
        {
            if (strength > hits.back().strength)
                hits.back() = { f, strength };

            continue;
        }

        hits.push_back ({ f, strength });
    }

    // Keep the strongest hits if there are too many, back in time order
    if ((int) hits.size() > maxHits)
    {
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.strength > b.strength; });
        hits.resize ((size_t) maxHits);
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b) { return a.frame < b.frame; });
    }

    // Place each hit on the sample where its attack starts
    const int block = juce::jmax (8, juce::roundToInt (sampleRate * 0.0005));
    const int preRoll = juce::roundToInt (sampleRate * 0.001);
    std::vector<int> positions;

    for (const auto& hit : hits)
    {
        const int searchStart = juce::jmax (0, (hit.frame - 2) * hop);
        const int searchEnd = juce::jmin (numSamples, hit.frame * hop + window);

        int peakIndex = searchStart;
        float peak = 0.0f;

        for (int n = searchStart; n < searchEnd; ++n)
        {
            const float level = std::abs (mono[(size_t) n]);

            if (level > peak)
            {
                peak = level;
                peakIndex = n;
            }
        }

        // Walk back from the peak until the signal was still quiet
        int onset = searchStart;

        for (int end = peakIndex; end - block >= searchStart; end -= block)
        {
            float blockPeak = 0.0f;

            for (int n = end - block; n < end; ++n)
                blockPeak = juce::jmax (blockPeak, std::abs (mono[(size_t) n]));

            if (blockPeak < peak * 0.12f)
            {
                onset = end;
                break;
            }
        }

        const int position = juce::jmax (0, onset - preRoll);

        if (positions.empty() || position > positions.back())
            positions.push_back (position);
    }

    return positions;
}
}
