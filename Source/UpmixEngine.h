#pragma once
#include <JuceHeader.h>

//==============================================================================
//  Psychoacoustic 5.1 upmix engine.
//  Outputs true 6 discrete channels via JUCE AudioDeviceManager.
//
//  5.1 Channel order (Windows / WASAPI / ASIO standard):
//    0 = Front Left    (FL)
//    1 = Front Right   (FR)
//    2 = Front Center  (FC)
//    3 = LFE           (Subwoofer)
//    4 = Surround Left (SL)
//    5 = Surround Right(SR)
//==============================================================================

struct UpmixParams
{
    // Front stage controls (FL/FR/FC)
    float frontGain     = 1.00f;   // FL/FR level
    float centerGain    = 1.05f;   // FC level
    float centerHPF     = 130.0f;  // FC highpass Hz

    // Sub/LFE controls
    float lfeGain       = 1.20f;   // Sub level
    float lfeCrossover  = 90.0f;   // LR4 crossover Hz
    float lfeShelfGain  = 3.5f;    // Low-shelf boost dB at 60Hz
    float exciterDrive  = 0.45f;   // Harmonic exciter amount

    // Surround controls (SL/SR)
    float surroundGain  = 0.95f;   // SL/SR level
    float haasDelayMs   = 16.0f;   // Haas pre-delay ms (5-28)
    float surroundHPF   = 140.0f;  // SL/SR highpass Hz
    float sideBlend     = 0.85f;   // How much L-R side signal goes to SL/SR
    float midBlend      = 0.12f;   // How much mid bleeds into SL/SR

    // Room/decorrelation controls
    float reverbWet     = 0.62f;   // Velvet reverb wet mix
    float roomSize      = 0.74f;   // Decay of velvet IR (0-1)
    float velvetDensity = 2200.0f; // Pulses/sec in velvet noise IR
};

//==============================================================================
class UpmixEngine
{
public:
    UpmixEngine();

    // Initializes DSP objects and internal state.
    void prepare(double sampleRate, int blockSize);
    // Resets all DSP states (filters, delays, convolution tails).
    void reset();

    // Processes stereoIn (2 channels) and writes sixChOut (6 channels).
    // sixChOut must be allocated as 6 channels x numSamples.
    void process(const juce::AudioBuffer<float>& stereoIn,
                 juce::AudioBuffer<float>& sixChOut,
                 int numSamples,
                 bool surround51Active);

    UpmixParams params;

private:
    double sr = 44100.0;
    int maxBlockSize = 0;

    // Center channel filters
    juce::dsp::IIR::Filter<float> fcHPF;

    // LFE crossover: Linkwitz-Riley 4th order via cascaded Butterworth LPFs
    juce::dsp::IIR::Filter<float> lfeLP1, lfeLP2;
    juce::dsp::IIR::Filter<float> lfeShelf;  // low-shelf boost
    juce::dsp::IIR::Filter<float> lfeDCBlock;

    // Surround high-pass + all-pass decorrelation filters
    juce::dsp::IIR::Filter<float> slHPF, srHPF;
    juce::dsp::IIR::Filter<float> slAllPass1, slAllPass2;
    juce::dsp::IIR::Filter<float> srAllPass1, srAllPass2;

    // Haas delay lines (max 50 ms)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> slDelay { 8192 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> srDelay { 8192 };

    // Velvet-noise convolution for surround decorrelation
    juce::dsp::Convolution slConv, srConv;

    // Parameter-tracking for lazy IR rebuild
    float lastRoomSize = -1.0f;
    float lastDensity = -1.0f;

    bool graphPrepared = false;

    void rebuildFilters();
    void rebuildVelvetIRs();

    // Builds a mono velvet-noise impulse response buffer.
    juce::AudioBuffer<float> makeVelvetIR(int lengthMs, float density,
                                          float roomSize, unsigned seed);

    // Harmonic exciter transfer function.
    float excite(float x, float drive) const noexcept;

    // Previous param snapshot for dirty checking
    UpmixParams prevParams;

    juce::dsp::ProcessSpec spec {};

    // Pre-allocated scratch buffers used in process() to avoid audio-thread allocations.
    juce::AudioBuffer<float> scratchMid;
    juce::AudioBuffer<float> scratchSide;
    juce::AudioBuffer<float> scratchFC;
    juce::AudioBuffer<float> scratchLFE;
    juce::AudioBuffer<float> scratchSurr;
    juce::AudioBuffer<float> scratchSL;
    juce::AudioBuffer<float> scratchSR;
    juce::AudioBuffer<float> scratchSLConv;
    juce::AudioBuffer<float> scratchSRConv;
    juce::AudioBuffer<float> scratchCenterMask;
    juce::AudioBuffer<float> scratchWetMix;
    juce::AudioBuffer<float> scratchRoomBoost;

    // Smoothed parameters to reduce zipper noise while dragging controls.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> frontGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> centerGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lfeGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> surroundGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sideBlendSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> midBlendSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reverbWetSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> roomSizeSmoothed;

    // Stable block-level center confidence (used to avoid FC noise/pumping).
    float centerFocusState = 0.70f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpmixEngine)
};
