#include "../Source/MainComponent.h"

#include <cmath>

int main()
{
    constexpr int numSamples = 512;
    constexpr double sampleRate = 48000.0;

    UpmixEngine engine;
    engine.prepare(sampleRate, numSamples);

    juce::AudioBuffer<float> stereo(2, numSamples);
    juce::AudioBuffer<float> surround(6, numSamples);
    stereo.clear();
    surround.clear();

    // Stereo stress signal.
    for (int i = 0; i < numSamples; ++i)
    {
        const float t = float(i) / float(sampleRate);
        stereo.setSample(0, i, std::sin(juce::MathConstants<float>::twoPi * 440.0f * t) * 0.4f);
        stereo.setSample(1, i, std::sin(juce::MathConstants<float>::twoPi * 660.0f * t) * 0.4f);
    }

    engine.process(stereo, surround, numSamples, true);

    float surroundEnergy = 0.0f;
    for (int ch = 0; ch < 6; ++ch)
    {
        const float* ptr = surround.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            if (!std::isfinite(ptr[i]))
                return 2;
            surroundEnergy += ptr[i] * ptr[i];
        }
    }

    if (surroundEnergy < 1.0e-4f)
        return 3;

    // Stereo mode should keep only FL/FR active.
    surround.clear();
    engine.process(stereo, surround, numSamples, false);

    float nonStereoEnergy = 0.0f;
    for (int ch = 2; ch < 6; ++ch)
    {
        const float* ptr = surround.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            nonStereoEnergy += ptr[i] * ptr[i];
    }

    if (nonStereoEnergy > 1.0e-5f)
        return 4;

    // Mono vocal-like signal should produce a stable center channel (no random spikes).
    stereo.clear();
    surround.clear();
    for (int i = 0; i < numSamples; ++i)
    {
        const float t = float(i) / float(sampleRate);
        const float mono = std::sin(juce::MathConstants<float>::twoPi * 550.0f * t) * 0.25f;
        stereo.setSample(0, i, mono);
        stereo.setSample(1, i, mono);
    }

    engine.process(stereo, surround, numSamples, true);

    const float* fc = surround.getReadPointer(2);
    float fcEnergy = 0.0f;
    float fcMaxAbs = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float s = fc[i];
        if (!std::isfinite(s))
            return 5;
        fcEnergy += s * s;
        fcMaxAbs = juce::jmax(fcMaxAbs, std::abs(s));
    }

    if (fcEnergy < 1.0e-6f)
        return 6;

    // Sanity cap: FC should not blow up relative to the input range.
    if (fcMaxAbs > 1.8f)
        return 7;

    return 0;
}
