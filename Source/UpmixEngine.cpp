#include "UpmixEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

//==============================================================================
//  UPMIX ENGINE
//==============================================================================

// Constructor keeps the default parameter state.
// DSP objects are prepared later when the audio device provides format details.
UpmixEngine::UpmixEngine()
{
    // Will be rebuilt in prepare().
}

// Prepares every DSP block with the active sample rate and block size.
// This must run before the first process() call.
void UpmixEngine::prepare(double sampleRate, int blockSize)
{
    sr = sampleRate;
    maxBlockSize = juce::jmax(1, blockSize);

    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maxBlockSize;
    spec.numChannels = 1;

    // Prepare all mono processors.
    fcHPF.prepare(spec);
    lfeLP1.prepare(spec);
    lfeLP2.prepare(spec);
    lfeShelf.prepare(spec);
    lfeDCBlock.prepare(spec);

    slHPF.prepare(spec);
    srHPF.prepare(spec);
    slAllPass1.prepare(spec);
    slAllPass2.prepare(spec);
    srAllPass1.prepare(spec);
    srAllPass2.prepare(spec);

    // Delay buffers support up to 50 ms for Haas timing.
    slDelay.prepare(spec);
    srDelay.prepare(spec);
    slDelay.setMaximumDelayInSamples(int(sampleRate * 0.05));
    srDelay.setMaximumDelayInSamples(int(sampleRate * 0.05));

    // Convolution engines for surround decorrelation tails.
    slConv.prepare(spec);
    srConv.prepare(spec);
    slConv.reset();
    srConv.reset();

    // Force first-time coefficient/IR rebuild.
    graphPrepared = true;
    lastRoomSize = -1.0f;
    lastDensity = -1.0f;

    // Pre-allocate all temporary processing buffers to keep process() allocation-free.
    scratchMid.setSize(1, maxBlockSize, false, false, true);
    scratchSide.setSize(1, maxBlockSize, false, false, true);
    scratchFC.setSize(1, maxBlockSize, false, false, true);
    scratchLFE.setSize(1, maxBlockSize, false, false, true);
    scratchSurr.setSize(1, maxBlockSize, false, false, true);
    scratchSL.setSize(1, maxBlockSize, false, false, true);
    scratchSR.setSize(1, maxBlockSize, false, false, true);
    scratchSLConv.setSize(1, maxBlockSize, false, false, true);
    scratchSRConv.setSize(1, maxBlockSize, false, false, true);
    scratchCenterMask.setSize(1, maxBlockSize, false, false, true);
    scratchWetMix.setSize(1, maxBlockSize, false, false, true);
    scratchRoomBoost.setSize(1, maxBlockSize, false, false, true);

    // Parameter smoothing to remove zipper noise while dragging controls.
    constexpr double rampSeconds = 0.04;
    frontGainSmoothed.reset(sampleRate, rampSeconds);
    centerGainSmoothed.reset(sampleRate, rampSeconds);
    lfeGainSmoothed.reset(sampleRate, rampSeconds);
    surroundGainSmoothed.reset(sampleRate, rampSeconds);
    sideBlendSmoothed.reset(sampleRate, rampSeconds);
    midBlendSmoothed.reset(sampleRate, rampSeconds);
    reverbWetSmoothed.reset(sampleRate, rampSeconds);
    roomSizeSmoothed.reset(sampleRate, rampSeconds);

    frontGainSmoothed.setCurrentAndTargetValue(params.frontGain);
    centerGainSmoothed.setCurrentAndTargetValue(params.centerGain);
    lfeGainSmoothed.setCurrentAndTargetValue(params.lfeGain);
    surroundGainSmoothed.setCurrentAndTargetValue(params.surroundGain);
    sideBlendSmoothed.setCurrentAndTargetValue(params.sideBlend);
    midBlendSmoothed.setCurrentAndTargetValue(params.midBlend);
    reverbWetSmoothed.setCurrentAndTargetValue(params.reverbWet);
    roomSizeSmoothed.setCurrentAndTargetValue(params.roomSize);

    centerFocusState = 0.70f;

    // Build initial graph from current parameter values.
    rebuildFilters();
    rebuildVelvetIRs();

    // Keep a previous snapshot for change detection in process().
    prevParams = params;
}

// Clears history/state for all DSP blocks.
void UpmixEngine::reset()
{
    fcHPF.reset();
    lfeLP1.reset();
    lfeLP2.reset();
    lfeShelf.reset();
    lfeDCBlock.reset();

    slHPF.reset();
    srHPF.reset();
    slAllPass1.reset();
    slAllPass2.reset();
    srAllPass1.reset();
    srAllPass2.reset();

    slDelay.reset();
    srDelay.reset();
    slConv.reset();
    srConv.reset();

    frontGainSmoothed.setCurrentAndTargetValue(params.frontGain);
    centerGainSmoothed.setCurrentAndTargetValue(params.centerGain);
    lfeGainSmoothed.setCurrentAndTargetValue(params.lfeGain);
    surroundGainSmoothed.setCurrentAndTargetValue(params.surroundGain);
    sideBlendSmoothed.setCurrentAndTargetValue(params.sideBlend);
    midBlendSmoothed.setCurrentAndTargetValue(params.midBlend);
    reverbWetSmoothed.setCurrentAndTargetValue(params.reverbWet);
    roomSizeSmoothed.setCurrentAndTargetValue(params.roomSize);

    centerFocusState = 0.70f;
}

// Rebuilds coefficient-based DSP nodes when relevant controls change.
void UpmixEngine::rebuildFilters()
{
    if (!graphPrepared)
        return;

    // FC high-pass.
    *fcHPF.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, params.centerHPF, 0.707f);

    // LFE Linkwitz-Riley 4th order: two cascaded 2nd-order Butterworth LPFs.
    *lfeLP1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, params.lfeCrossover, 0.5f);
    *lfeLP2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, params.lfeCrossover, 0.5f);

    // LFE tonal shaping and infrasonic cleanup.
    *lfeShelf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sr, 60.0f, 0.707f, juce::Decibels::decibelsToGain(params.lfeShelfGain));
    *lfeDCBlock.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, 18.0f, 0.707f);

    // Surround high-pass.
    *slHPF.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, params.surroundHPF, 0.707f);
    *srHPF.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, params.surroundHPF, 0.707f);

    // Additional all-pass decorrelation. Density shifts phase complexity upward.
    const float densityNorm = juce::jlimit(0.0f, 1.0f, (params.velvetDensity - 500.0f) / 3500.0f);
    const float baseFreq = 650.0f + densityNorm * 1900.0f;
    const float qLow = 0.62f;
    const float qHigh = 0.79f;
    auto clampFreq = [this](float hz)
    {
        return juce::jlimit(120.0f, float(sr * 0.45), hz);
    };

    *slAllPass1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass(sr, clampFreq(baseFreq), qLow);
    *slAllPass2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass(sr, clampFreq(baseFreq * 1.85f), qHigh);
    *srAllPass1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass(sr, clampFreq(baseFreq * 1.12f), qLow);
    *srAllPass2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass(sr, clampFreq(baseFreq * 2.05f), qHigh);

    // Haas delays.
    slDelay.setDelay(float(sr * params.haasDelayMs / 1000.0));
    srDelay.setDelay(float(sr * (params.haasDelayMs + 3.5f) / 1000.0));
}

// Builds a mono velvet-noise impulse response with controlled decay.
juce::AudioBuffer<float> UpmixEngine::makeVelvetIR(int lengthMs, float density,
                                                    float roomSize, unsigned seed)
{
    int len = int(sr * lengthMs / 1000.0);
    len = std::max(len, 64);

    juce::AudioBuffer<float> ir(1, len);
    auto* d = ir.getWritePointer(0);
    ir.clear();

    unsigned s = seed;
    auto rnd = [&]() -> float
    {
        s = s * 1664525u + 1013904223u;
        return float(s) / float(std::numeric_limits<unsigned>::max());
    };

    // Pulse period in samples: higher density => more frequent pulses.
    float period = float(sr) / density;
    period = std::max(period, 1.0f);

    for (float p = 0.0f; p < float(len); p += period)
    {
        const int idx = juce::jlimit(0, len - 1, int(p + rnd() * period));
        const float sign = rnd() > 0.5f ? 1.0f : -1.0f;
        const float decay = std::exp(-5.5f * (1.0f - roomSize) * float(idx) / float(len));
        d[idx] = sign * decay;
    }

    // Normalize energy so room settings do not cause loudness jumps.
    float energy = 0.0f;
    for (int i = 0; i < len; ++i)
        energy += d[i] * d[i];

    if (energy > 1.0e-9f)
    {
        const float scale = 1.0f / std::sqrt(energy);
        for (int i = 0; i < len; ++i)
            d[i] *= scale;
    }

    return ir;
}

// Rebuilds left/right IRs when room controls change.
// Slight asymmetry between channels increases perceived spaciousness.
void UpmixEngine::rebuildVelvetIRs()
{
    if (!graphPrepared)
        return;

    const int irLengthMs = juce::jlimit(40, 220, int(35.0f + params.roomSize * 185.0f));
    const float slDensity = params.velvetDensity;
    const float srDensity = params.velvetDensity * 1.09f;

    auto slIR = makeVelvetIR(irLengthMs, slDensity, juce::jlimit(0.2f, 0.98f, params.roomSize), 1337u);
    auto srIR = makeVelvetIR(irLengthMs + 11, srDensity, juce::jlimit(0.2f, 0.98f, params.roomSize * 0.965f), 4242u);

    slConv.loadImpulseResponse(std::move(slIR), sr,
        juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::no);

    srConv.loadImpulseResponse(std::move(srIR), sr,
        juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::no);

    lastRoomSize = params.roomSize;
    lastDensity = params.velvetDensity;
}

// Simple soft saturation for LFE harmonic enhancement.
float UpmixEngine::excite(float x, float drive) const noexcept
{
    const float k = drive * 80.0f;
    return (juce::MathConstants<float>::pi + k) * x
        / (juce::MathConstants<float>::pi + k * std::abs(x));
}

// Main upmix graph.
// Input: 2-channel stereo, Output: 6 channels in FL/FR/FC/LFE/SL/SR order.
void UpmixEngine::process(const juce::AudioBuffer<float>& stereoIn,
                          juce::AudioBuffer<float>& sixChOut,
                          int numSamples,
                          bool surround51Active)
{
    jassert(stereoIn.getNumChannels() >= 2);
    jassert(sixChOut.getNumChannels() >= 6);
    jassert(sixChOut.getNumSamples() >= numSamples);

    if (numSamples <= 0)
        return;

    if (numSamples > maxBlockSize)
    {
        jassertfalse;
        sixChOut.clear();
        return;
    }

    sixChOut.clear();

    auto hasMeaningfulChange = [](float a, float b)
    {
        return std::abs(a - b) > 1.0e-6f;
    };

    const bool needsFilterRebuild =
        hasMeaningfulChange(params.centerHPF, prevParams.centerHPF)
        || hasMeaningfulChange(params.lfeCrossover, prevParams.lfeCrossover)
        || hasMeaningfulChange(params.lfeShelfGain, prevParams.lfeShelfGain)
        || hasMeaningfulChange(params.surroundHPF, prevParams.surroundHPF)
        || hasMeaningfulChange(params.haasDelayMs, prevParams.haasDelayMs)
        || hasMeaningfulChange(params.velvetDensity, prevParams.velvetDensity);

    if (needsFilterRebuild)
        rebuildFilters();

    if (hasMeaningfulChange(params.roomSize, lastRoomSize)
        || hasMeaningfulChange(params.velvetDensity, lastDensity))
    {
        rebuildVelvetIRs();
    }

    prevParams = params;

    frontGainSmoothed.setTargetValue(params.frontGain);
    centerGainSmoothed.setTargetValue(params.centerGain);
    lfeGainSmoothed.setTargetValue(params.lfeGain);
    surroundGainSmoothed.setTargetValue(params.surroundGain);
    sideBlendSmoothed.setTargetValue(params.sideBlend);
    midBlendSmoothed.setTargetValue(params.midBlend);
    reverbWetSmoothed.setTargetValue(params.reverbWet);
    roomSizeSmoothed.setTargetValue(params.roomSize);

    const float* inL = stereoIn.getReadPointer(0);
    const float* inR = stereoIn.getReadPointer(1);

    float* outFL = sixChOut.getWritePointer(0);
    float* outFR = sixChOut.getWritePointer(1);
    float* outFC = sixChOut.getWritePointer(2);
    float* outLFE = sixChOut.getWritePointer(3);
    float* outSL = sixChOut.getWritePointer(4);
    float* outSR = sixChOut.getWritePointer(5);

    if (!surround51Active)
    {
        // Stereo mode: direct FL/FR passthrough only.
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = frontGainSmoothed.getNextValue();
            outFL[i] = inL[i] * g;
            outFR[i] = inR[i] * g;
        }

        return;
    }

    auto processMonoBlock = [numSamples](auto& processor, juce::AudioBuffer<float>& monoBuf)
    {
        juce::dsp::AudioBlock<float> block(monoBuf);
        auto subBlock = block.getSubBlock(0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context(subBlock);
        processor.process(context);
    };

    float* mid = scratchMid.getWritePointer(0);
    float* side = scratchSide.getWritePointer(0);
    float* centerMask = scratchCenterMask.getWritePointer(0);

    const float densityNorm = juce::jlimit(0.0f, 1.0f, (params.velvetDensity - 500.0f) / 3500.0f);
    const float decorCross = 0.04f + densityNorm * 0.34f;

    // Mid/Side extraction + block coherence analysis for stable center confidence.
    double sumLR = 0.0;
    double sumL2 = 1.0e-12;
    double sumR2 = 1.0e-12;
    double sumMid2 = 1.0e-12;
    double sumSide2 = 1.0e-12;

    for (int i = 0; i < numSamples; ++i)
    {
        const float left = inL[i];
        const float right = inR[i];

        const float frontG = frontGainSmoothed.getNextValue();
        outFL[i] = left * frontG;
        outFR[i] = right * frontG;

        const float midSample = (left + right) * 0.5f;
        const float sideSample = (left - right) * 0.5f;
        mid[i] = midSample;
        side[i] = sideSample;

        sumLR += double(left) * double(right);
        sumL2 += double(left) * double(left);
        sumR2 += double(right) * double(right);
        sumMid2 += double(midSample) * double(midSample);
        sumSide2 += double(sideSample) * double(sideSample);
    }

    const float coherence = juce::jlimit(-1.0f, 1.0f, float(sumLR / std::sqrt(sumL2 * sumR2)));
    const float corrWeight = juce::jlimit(0.0f, 1.0f, (coherence + 0.25f) * 0.8f);
    const float widthRatio = float(std::sqrt(sumSide2 / sumMid2));
    const float widthWeight = juce::jlimit(0.18f, 1.0f, 1.05f - 0.62f * widthRatio);
    const float centerTarget = juce::jlimit(0.18f, 1.0f, corrWeight * widthWeight);

    const float previousCenterFocus = centerFocusState;
    centerFocusState += (centerTarget - centerFocusState) * 0.18f;
    if (!std::isfinite(centerFocusState))
        centerFocusState = 0.65f;

    // Smooth center confidence across the block to avoid zipper/pumping noise in FC.
    if (numSamples == 1)
    {
        centerMask[0] = juce::jlimit(0.12f, 1.0f, centerFocusState);
    }
    else
    {
        const float denom = 1.0f / float(numSamples - 1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = float(i) * denom;
            const float w = previousCenterFocus + (centerFocusState - previousCenterFocus) * t;
            centerMask[i] = juce::jlimit(0.12f, 1.0f, w);
        }
    }

    // FC path: Mid -> high-pass -> stable center confidence -> gain.
    scratchFC.copyFrom(0, 0, scratchMid, 0, 0, numSamples);
    processMonoBlock(fcHPF, scratchFC);

    {
        const float* fcIn = scratchFC.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            const float centerWeight = 0.15f + 0.85f * centerMask[i];
            outFC[i] = fcIn[i] * centerGainSmoothed.getNextValue() * centerWeight;
        }
    }

    // LFE path: Mid -> LR4 crossover -> shelf -> DC block -> level-aware exciter -> gain.
    scratchLFE.copyFrom(0, 0, scratchMid, 0, 0, numSamples);
    processMonoBlock(lfeLP1, scratchLFE);
    processMonoBlock(lfeLP2, scratchLFE);
    processMonoBlock(lfeShelf, scratchLFE);
    processMonoBlock(lfeDCBlock, scratchLFE);

    {
        float* lfePtr = scratchLFE.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            const float input = lfePtr[i];
            const float levelNorm = juce::jlimit(0.0f, 1.0f, std::abs(input) * 4.0f);
            const float drive = juce::jlimit(0.0f, 1.0f, params.exciterDrive * (0.55f + 0.45f * levelNorm));
            const float enhanced = input * 0.80f + excite(input, drive) * 0.20f;
            outLFE[i] = std::tanh(enhanced) * lfeGainSmoothed.getNextValue();
        }
    }

    // Surround source: side + controlled mid bleed (center-aware, but noise-safe).
    {
        float* surr = scratchSurr.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            const float centerReject = 1.0f - centerMask[i];
            const float midContribution = mid[i] * midBlendSmoothed.getNextValue()
                                        * (0.25f + 0.75f * centerReject);

            surr[i] = side[i] * sideBlendSmoothed.getNextValue() + midContribution;
        }
    }

    // Precompute shared wet/room curves once so SL and SR stay matched.
    {
        float* wetMix = scratchWetMix.getWritePointer(0);
        float* roomBoost = scratchRoomBoost.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
        {
            float wet = std::pow(juce::jlimit(0.0f, 1.0f, reverbWetSmoothed.getNextValue()), 0.65f);
            wet *= 0.84f + 0.16f * (1.0f - centerMask[i]);
            wetMix[i] = juce::jlimit(0.0f, 0.97f, wet);
            roomBoost[i] = 0.64f + roomSizeSmoothed.getNextValue() * 0.54f;
        }
    }

    // SL path: surround source -> HPF -> all-pass decorrelation -> Haas delay -> velvet convolution.
    scratchSL.copyFrom(0, 0, scratchSurr, 0, 0, numSamples);
    processMonoBlock(slHPF, scratchSL);
    processMonoBlock(slAllPass1, scratchSL);
    processMonoBlock(slAllPass2, scratchSL);

    {
        float* slPtr = scratchSL.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            slDelay.pushSample(0, slPtr[i]);
            slPtr[i] = slDelay.popSample(0);
        }
    }

    scratchSLConv.copyFrom(0, 0, scratchSL, 0, 0, numSamples);
    processMonoBlock(slConv, scratchSLConv);

    {
        const float* slDry = scratchSL.getReadPointer(0);
        const float* slWet = scratchSLConv.getReadPointer(0);
        const float* wetMix = scratchWetMix.getReadPointer(0);
        const float* roomBoost = scratchRoomBoost.getReadPointer(0);

        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = wetMix[i];
            const float dry = 1.0f - wet;
            outSL[i] = slDry[i] * dry + (slWet[i] * roomBoost[i]) * wet;
        }
    }

    // SR path: equivalent to SL with independent all-pass/delay/IR for stronger decorrelation.
    scratchSR.copyFrom(0, 0, scratchSurr, 0, 0, numSamples);
    processMonoBlock(srHPF, scratchSR);
    processMonoBlock(srAllPass1, scratchSR);
    processMonoBlock(srAllPass2, scratchSR);

    {
        float* srPtr = scratchSR.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            srDelay.pushSample(0, srPtr[i]);
            srPtr[i] = srDelay.popSample(0);
        }
    }

    scratchSRConv.copyFrom(0, 0, scratchSR, 0, 0, numSamples);
    processMonoBlock(srConv, scratchSRConv);

    {
        const float* srDry = scratchSR.getReadPointer(0);
        const float* srWet = scratchSRConv.getReadPointer(0);
        const float* wetMix = scratchWetMix.getReadPointer(0);
        const float* roomBoost = scratchRoomBoost.getReadPointer(0);

        for (int i = 0; i < numSamples; ++i)
        {
            const float wet = wetMix[i];
            const float dry = 1.0f - wet;
            outSR[i] = srDry[i] * dry + (srWet[i] * roomBoost[i]) * wet;
        }
    }

    // Stereo surround decorrelation matrix:
    // stronger decorrelation when center confidence is lower (wider content).
    for (int i = 0; i < numSamples; ++i)
    {
        const float sl = outSL[i];
        const float srChan = outSR[i];
        const float centerReject = 1.0f - centerMask[i];
        const float adaptiveCross = decorCross * (0.60f + 0.40f * centerReject);
        const float g = surroundGainSmoothed.getNextValue();

        outSL[i] = (sl - srChan * adaptiveCross) * g;
        outSR[i] = (srChan - sl * adaptiveCross) * g;
    }
}
