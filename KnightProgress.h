#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

//==============================================================================
// Hit counter for the knight mascot. Every plugin instance has its own knight, starting
// at level 1; nothing is saved, so he starts over whenever the plugin is loaded.
class KnightProgress
{
public:
    // XP needed to go from one level to the next: 50, 75, 100, 125...
    static constexpr int firstLevelHits = 50;
    static constexpr int extraHitsPerLevel = 25;

    static int hitsToAdvance (int level) { return firstLevelHits + extraHitsPerLevel * (level - 1); }

    static int levelForHits (juce::int64 hits)
    {
        int level = 1;

        while (hits >= hitsToAdvance (level))
            hits -= hitsToAdvance (level++);

        return level;
    }

    // Hits earned so far towards the next level
    static int hitsIntoLevel (juce::int64 hits)
    {
        for (int level = 1; hits >= hitsToAdvance (level); ++level)
            hits -= hitsToAdvance (level);

        return (int) hits;
    }

    // Safe to call from the audio thread
    void addHit() noexcept                      { hits.fetch_add (1, std::memory_order_relaxed); }
    juce::int64 getTotalHits() const noexcept   { return hits.load (std::memory_order_relaxed); }

private:
    std::atomic<juce::int64> hits { 0 };
};
