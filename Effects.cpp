#include "Effects.h"

namespace fx
{
namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    float onePoleCoefficient (float cutoffHz, double sampleRate)
    {
        return 1.0f - (float) std::exp (-twoPi * cutoffHz / sampleRate);
    }

    void advance (double& phase, double increment)
    {
        phase += increment;

        if (phase >= 1.0)
            phase -= 1.0;
    }
}

//==============================================================================
void DelayLine::setMaximumDelay (int samples)
{
    const int size = juce::nextPowerOfTwo (juce::jmax (4, samples + 2));
    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;
    writeIndex = 0;
}

void DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
}

//==============================================================================
void Chorus::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    prepareMix (sampleRate);

    for (auto& line : lines)
        line.setMaximumDelay ((int) (sampleRate * 0.03));

    reset();
}

void Chorus::reset()
{
    for (auto& line : lines)
        line.clear();

    warmth = {};
    phase = shimmerPhase = 0.0;
}

void Chorus::setParameters (int newMode, float rateHz, float newDepth)
{
    mode = juce::jlimit (0, 2, newMode);
    rate = rateHz;
    depth = newDepth;
}

void Chorus::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! isActive())
        return;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    // Mode II sweeps wider around a longer delay; I+II adds a fast shallow vibrato on top
    const float baseMs    = mode == 1 ? 7.0f : 5.0f;
    const float sweepMs   = (mode == 1 ? 3.5f : 2.0f) * depth;
    const float shimmerMs = mode == 2 ? 0.35f * depth : 0.0f;
    const auto msToSamples = (float) (sampleRate / 1000.0);
    const float warmthCoefficient = onePoleCoefficient (9000.0f, sampleRate);   // bucket-brigade style top end

    for (int i = 0; i < numSamples; ++i)
    {
        advance (phase, rate / sampleRate);
        advance (shimmerPhase, 9.75 / sampleRate);

        const float triangle = 4.0f * std::abs ((float) phase - 0.5f) - 1.0f;
        const auto shimmer = (float) std::sin (twoPi * shimmerPhase);
        const float amount = mix.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Opposite sweeps on each side give the wide stereo image
            const float sweep = ch == 0 ? triangle : -triangle;
            const float delayMs = baseMs + sweepMs * sweep + shimmerMs * shimmer;

            auto* data = buffer.getWritePointer (ch);
            const float dry = data[i];
            const float wet = lines[(size_t) ch].readFractional (delayMs * msToSamples);
            lines[(size_t) ch].push (dry);

            auto& state = warmth[(size_t) ch];
            state += warmthCoefficient * (wet - state);

            data[i] = (dry + amount * state) / (1.0f + 0.5f * amount);
        }
    }
}

//==============================================================================
void Flanger::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    prepareMix (sampleRate);

    for (auto& line : lines)
        line.setMaximumDelay ((int) (sampleRate * 0.012));

    reset();
}

void Flanger::reset()
{
    for (auto& line : lines)
        line.clear();

    phase = 0.0;
}

void Flanger::setParameters (float rateHz, float newDepth, float newFeedback)
{
    rate = rateHz;
    depth = newDepth;
    feedback = juce::jlimit (-0.95f, 0.95f, newFeedback);
}

void Flanger::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! isActive())
        return;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());
    const auto msToSamples = (float) (sampleRate / 1000.0);
    const float wetGain = 1.0f - 0.6f * std::abs (feedback);   // keeps resonant settings from jumping out

    for (int i = 0; i < numSamples; ++i)
    {
        advance (phase, rate / sampleRate);
        const float amount = mix.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Right side runs a quarter cycle behind for a swirling stereo sweep
            double p = phase + (ch == 1 ? 0.25 : 0.0);

            if (p >= 1.0)
                p -= 1.0;

            const float sweep = 1.0f - std::abs (2.0f * (float) p - 1.0f);
            const float delayMs = 0.25f + depth * 5.0f * sweep;

            auto* data = buffer.getWritePointer (ch);
            const float dry = data[i];
            const float wet = lines[(size_t) ch].readFractional (juce::jmax (1.0f, delayMs * msToSamples));
            lines[(size_t) ch].push (dry + feedback * wet);

            data[i] = dry * (1.0f - amount) + wet * wetGain * amount;
        }
    }
}

//==============================================================================
void TapeEcho::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    prepareMix (sampleRate);

    for (auto& line : lines)
        line.setMaximumDelay ((int) (sampleRate * (maxDelaySeconds + 0.05)));

    delayTime.reset (sampleRate, 0.35);
    delayTime.setCurrentAndTargetValue (0.375f);
    reset();
}

void TapeEcho::reset()
{
    for (auto& line : lines)
        line.clear();

    lowState = {};
    highState = {};
    wowPhase = flutterPhase = 0.0;
    delayTime.setCurrentAndTargetValue (delayTime.getTargetValue());
}

void TapeEcho::setParameters (double delaySeconds, float newFeedback, float newTape)
{
    delayTime.setTargetValue ((float) juce::jlimit (0.01, maxDelaySeconds, delaySeconds));
    feedback = newFeedback;
    tape = newTape;
}

void TapeEcho::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! isActive())
        return;

    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    // More tape = darker repeats, more saturation and more wow and flutter
    const float lowCoefficient = onePoleCoefficient (14000.0f - 10000.0f * tape, sampleRate);
    const float highCoefficient = onePoleCoefficient (70.0f, sampleRate);
    const float drive = 1.0f + 1.5f * tape;
    const auto wowDepth = (float) (tape * 0.0025 * sampleRate);
    const auto flutterDepth = (float) (tape * 0.0003 * sampleRate);
    const auto maxDelay = (float) (maxDelaySeconds * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        advance (wowPhase, 0.55 / sampleRate);
        advance (flutterPhase, 6.3 / sampleRate);

        const float delaySamples = delayTime.getNextValue() * (float) sampleRate;
        const float amount = mix.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float modulation = wowDepth * (float) std::sin (twoPi * wowPhase + ch * 0.9)
                                   + flutterDepth * (float) std::sin (twoPi * flutterPhase + ch * 1.7);

            auto& line = lines[(size_t) ch];
            const float echo = line.readFractional (juce::jlimit (1.0f, maxDelay, delaySamples + modulation));

            auto& low = lowState[(size_t) ch];
            auto& high = highState[(size_t) ch];
            low += lowCoefficient * (echo - low);
            high += highCoefficient * (low - high);
            const float wet = low - high;

            auto* data = buffer.getWritePointer (ch);
            const float dry = data[i];
            line.push (std::tanh (drive * (dry + feedback * wet)) / drive);

            data[i] = dry + amount * wet;
        }
    }
}

//==============================================================================
int PlateReverb::scaled (int samplesAt29761) const noexcept
{
    return juce::jmax (1, (int) std::round (samplesAt29761 * sampleRate / 29761.0));
}

void PlateReverb::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    prepareMix (sampleRate);

    predelay.setMaximumDelay ((int) (sampleRate * 0.25));

    const int diffuserLengths[] = { 142, 107, 379, 277 };

    for (size_t i = 0; i < inputDiffusers.size(); ++i)
    {
        inputDiffusers[i].length = (float) scaled (diffuserLengths[i]);
        inputDiffusers[i].line.setMaximumDelay (scaled (diffuserLengths[i]) + 2);
    }

    const int excursion = scaled (16) + 2;

    auto setupAllpass = [this] (Allpass& allpass, int length, int extra)
    {
        allpass.length = (float) scaled (length);
        allpass.line.setMaximumDelay (scaled (length) + extra + 2);
    };

    setupAllpass (leftModulated, 672, excursion);
    setupAllpass (rightModulated, 908, excursion);
    setupAllpass (leftDecayDiffuser, 1800, 0);
    setupAllpass (rightDecayDiffuser, 2656, 0);

    auto setupDelay = [this] (Tap& tap, int length)
    {
        tap.length = scaled (length);
        tap.line.setMaximumDelay (tap.length + 2);
    };

    setupDelay (leftDelay1, 4453);
    setupDelay (leftDelay2, 3720);
    setupDelay (rightDelay1, 4217);
    setupDelay (rightDelay2, 3163);

    // Output taps from the paper, left then right
    const int tapLengths[] = { 266, 2974, 1913, 1996, 1990, 187, 1066,
                               353, 3627, 1228, 2673, 2111, 335, 121 };

    for (size_t i = 0; i < taps.size(); ++i)
        taps[i] = scaled (tapLengths[i]);

    reset();
}

void PlateReverb::reset()
{
    predelay.clear();

    for (auto& diffuser : inputDiffusers)
        diffuser.line.clear();

    for (auto* allpass : { &leftModulated, &rightModulated, &leftDecayDiffuser, &rightDecayDiffuser })
        allpass->line.clear();

    for (auto* tap : { &leftDelay1, &leftDelay2, &rightDelay1, &rightDelay2 })
        tap->line.clear();

    bandwidthState = leftDamping = rightDamping = leftOut = rightOut = 0.0f;
    lfoPhase = 0.0;
}

void PlateReverb::setParameters (float decaySeconds, float tone, float predelayMs)
{
    // Each half of the tank takes about 0.36 s and applies the decay gain twice,
    // so this gives roughly the requested RT60
    constexpr double halfLoopSeconds = (672.0 + 4453.0 + 1800.0 + 3720.0) / 29761.0;
    decay = (float) juce::jmin (0.97, std::pow (10.0, -1.5 * halfLoopSeconds / juce::jmax (0.1f, decaySeconds)));

    damping = 0.0005f + 0.7f * (1.0f - tone);
    bandwidth = 0.35f + 0.6495f * tone;
    predelaySamples = juce::jlimit (1.0f, (float) (sampleRate * 0.24), predelayMs * 0.001f * (float) sampleRate);
}

void PlateReverb::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! isActive())
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto excursion = (float) scaled (16);

    for (int i = 0; i < numSamples; ++i)
    {
        const float input = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];

        const float delayed = predelay.readFractional (predelaySamples);
        predelay.push (input);

        bandwidthState += bandwidth * (delayed - bandwidthState);

        float x = bandwidthState;
        x = inputDiffusers[0].process (x, inputDiffusers[0].length, 0.75f);
        x = inputDiffusers[1].process (x, inputDiffusers[1].length, 0.75f);
        x = inputDiffusers[2].process (x, inputDiffusers[2].length, 0.625f);
        x = inputDiffusers[3].process (x, inputDiffusers[3].length, 0.625f);

        advance (lfoPhase, 1.0 / sampleRate);
        const float modulationLeft = excursion * (float) std::sin (twoPi * lfoPhase);
        const float modulationRight = excursion * (float) std::cos (twoPi * lfoPhase);

        // Figure-eight tank: each half feeds the other
        float l = leftModulated.process (x + decay * rightOut, leftModulated.length + modulationLeft, -0.7f);
        const float l1 = leftDelay1.line.read (leftDelay1.length);
        leftDelay1.line.push (l);
        leftDamping += (1.0f - damping) * (l1 - leftDamping);
        l = leftDecayDiffuser.process (leftDamping * decay, leftDecayDiffuser.length, 0.5f);
        const float l2 = leftDelay2.line.read (leftDelay2.length);
        leftDelay2.line.push (l);

        float r = rightModulated.process (x + decay * leftOut, rightModulated.length + modulationRight, -0.7f);
        const float r1 = rightDelay1.line.read (rightDelay1.length);
        rightDelay1.line.push (r);
        rightDamping += (1.0f - damping) * (r1 - rightDamping);
        r = rightDecayDiffuser.process (rightDamping * decay, rightDecayDiffuser.length, 0.5f);
        const float r2 = rightDelay2.line.read (rightDelay2.length);
        rightDelay2.line.push (r);

        leftOut = l2;
        rightOut = r2;

        const float wetLeft = 0.6f * (rightDelay1.line.read (taps[0]) + rightDelay1.line.read (taps[1])
                                      - rightDecayDiffuser.line.read (taps[2]) + rightDelay2.line.read (taps[3])
                                      - leftDelay1.line.read (taps[4]) - leftDecayDiffuser.line.read (taps[5])
                                      - leftDelay2.line.read (taps[6]));

        const float wetRight = 0.6f * (leftDelay1.line.read (taps[7]) + leftDelay1.line.read (taps[8])
                                       - leftDecayDiffuser.line.read (taps[9]) + leftDelay2.line.read (taps[10])
                                       - rightDelay1.line.read (taps[11]) - rightDecayDiffuser.line.read (taps[12])
                                       - rightDelay2.line.read (taps[13]));

        const float amount = mix.getNextValue();

        if (right != nullptr)
        {
            left[i] += amount * wetLeft;
            right[i] += amount * wetRight;
        }
        else
        {
            left[i] += amount * 0.5f * (wetLeft + wetRight);
        }
    }
}
}
