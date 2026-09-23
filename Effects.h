#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

//==============================================================================
// PadSlicer's FX chain: chorus -> flanger -> tape echo -> plate reverb.
// Each effect works on mono or stereo buffers in place, and costs nothing while its mix is at zero.
namespace fx
{
    //==============================================================================
    // Circular buffer. read (d) returns the sample pushed d samples ago (d >= 1).
    class DelayLine
    {
    public:
        void setMaximumDelay (int samples);
        void clear();

        void push (float sample) noexcept
        {
            buffer[(size_t) writeIndex] = sample;
            writeIndex = (writeIndex + 1) & mask;
        }

        float read (int delay) const noexcept
        {
            return buffer[(size_t) ((writeIndex - delay) & mask)];
        }

        float readFractional (float delay) const noexcept
        {
            const int whole = (int) delay;
            const float fraction = delay - (float) whole;
            const float a = read (whole), b = read (whole + 1);
            return a + fraction * (b - a);
        }

    private:
        std::vector<float> buffer;
        int mask = 0, writeIndex = 0;
    };

    //==============================================================================
    // Shared wet/dry handling: the mix fades in and out smoothly, and the effect
    // clears its state and stops processing once it has faded out.
    class Effect
    {
    public:
        virtual ~Effect() = default;

        void setMix (float target)
        {
            if (! isActive() && target > 0.0f)
                reset();

            mix.setTargetValue (target);
        }

        bool isActive() const noexcept   { return mix.getCurrentValue() > 0.0f || mix.getTargetValue() > 0.0f; }

        virtual void prepare (double sampleRate) = 0;
        virtual void reset() = 0;

    protected:
        void prepareMix (double sampleRate)   { mix.reset (sampleRate, 0.03); mix.setCurrentAndTargetValue (0.0f); }

        juce::SmoothedValue<float> mix;
    };

    //==============================================================================
    // Juno-style stereo chorus. Mode I is gentle, II is deeper, I+II adds a fast shimmer.
    class Chorus final : public Effect
    {
    public:
        void prepare (double sampleRate) override;
        void reset() override;
        void setParameters (int mode, float rateHz, float depth);
        void process (juce::AudioBuffer<float>&, int numSamples);

    private:
        double sampleRate = 44100.0;
        std::array<DelayLine, 2> lines;
        std::array<float, 2> warmth {};
        double phase = 0.0, shimmerPhase = 0.0;
        int mode = 0;
        float rate = 0.5f, depth = 0.6f;
    };

    //==============================================================================
    // Analog-style flanger with a triangle sweep and bipolar feedback.
    class Flanger final : public Effect
    {
    public:
        void prepare (double sampleRate) override;
        void reset() override;
        void setParameters (float rateHz, float depth, float feedback);
        void process (juce::AudioBuffer<float>&, int numSamples);

    private:
        double sampleRate = 44100.0;
        std::array<DelayLine, 2> lines;
        double phase = 0.0;
        float rate = 0.2f, depth = 0.7f, feedback = 0.5f;
    };

    //==============================================================================
    // Tape echo: saturated, darkening repeats with wow and flutter. "tape" goes from clean to worn.
    class TapeEcho final : public Effect
    {
    public:
        static constexpr double maxDelaySeconds = 5.0;

        void prepare (double sampleRate) override;
        void reset() override;
        void setParameters (double delaySeconds, float feedback, float tape);
        void process (juce::AudioBuffer<float>&, int numSamples);

    private:
        double sampleRate = 44100.0;
        std::array<DelayLine, 2> lines;
        std::array<float, 2> lowState {}, highState {};
        juce::SmoothedValue<float> delayTime;   // glides like a tape machine when changed
        double wowPhase = 0.0, flutterPhase = 0.0;
        float feedback = 0.4f, tape = 0.4f;
    };

    //==============================================================================
    // Plate reverb after Jon Dattorro's "Effect Design Part 1" (1997).
    class PlateReverb final : public Effect
    {
    public:
        void prepare (double sampleRate) override;
        void reset() override;
        void setParameters (float decaySeconds, float tone, float predelayMs);
        void process (juce::AudioBuffer<float>&, int numSamples);

    private:
        struct Allpass
        {
            DelayLine line;
            float length = 0.0f;

            float process (float input, float delay, float gain) noexcept
            {
                const float delayed = line.readFractional (delay);
                const float v = input - gain * delayed;
                line.push (v);
                return delayed + gain * v;
            }
        };

        struct Tap
        {
            DelayLine line;
            int length = 0;
        };

        int scaled (int samplesAt29761) const noexcept;

        double sampleRate = 44100.0;
        DelayLine predelay;
        std::array<Allpass, 4> inputDiffusers;
        Allpass leftModulated, rightModulated, leftDecayDiffuser, rightDecayDiffuser;
        Tap leftDelay1, leftDelay2, rightDelay1, rightDelay2;
        float bandwidthState = 0.0f, leftDamping = 0.0f, rightDamping = 0.0f;
        float leftOut = 0.0f, rightOut = 0.0f;
        double lfoPhase = 0.0;

        float decay = 0.5f, damping = 0.3f, bandwidth = 0.9f, predelaySamples = 1.0f;
        std::array<int, 14> taps {};
    };
}
