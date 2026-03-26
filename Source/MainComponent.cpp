#include "MainComponent.h"

//==============================================================================
//  COLOURS & CONSTANTS
//==============================================================================
static const juce::Colour COL_BG0   (0xff070b12);
static const juce::Colour COL_BG1   (0xff0e1623);
static const juce::Colour COL_BG2   (0xff152132);
static const juce::Colour COL_BG3   (0xff24344b);
static const juce::Colour COL_PANEL (0xff0d1521);
static const juce::Colour COL_BRDR  (0xff3f5471);
static const juce::Colour COL_ACC   (0xff59d5ff);
static const juce::Colour COL_ORG   (0xffffb45b);
static const juce::Colour COL_GRN   (0xff79e4b8);
static const juce::Colour COL_YLW   (0xffffd978);
static const juce::Colour COL_PRP   (0xff9fb1ff);
static const juce::Colour COL_TXT   (0xfff1f6ff);
static const juce::Colour COL_MUT   (0xffa4b4ca);
// This palette is intentionally centralized so visual tuning can happen in one place.
// If you want to re-theme the app, start by editing these constants.

// Increase this to make all UI text larger globally.
static constexpr float UI_FONT_SCALE = 1.18f;
static float fs(float px) noexcept { return px * UI_FONT_SCALE; }

//==============================================================================
//  UPMIX ENGINE
//==============================================================================

// Constructor keeps the default parameter state.
// DSP objects are prepared later when the audio device provides format details.
UpmixEngine::UpmixEngine()
{
    // will be rebuilt in prepare()
}

// Prepares every DSP block with the active sample rate and block size.
// This must run before the first process() call.
void UpmixEngine::prepare(double sampleRate, int blockSize)
{
    // Store runtime format used by filter/delay calculations.
    sr = sampleRate;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32)blockSize;
    spec.numChannels      = 1;

    // Prepare all mono-path filters.
    fcHPF .prepare(spec);
    lfeLP1.prepare(spec);
    lfeLP2.prepare(spec);
    lfeShelf.prepare(spec);
    slHPF .prepare(spec);
    srHPF .prepare(spec);

    // Delay buffers support up to 50 ms for Haas timing.
    slDelay.prepare(spec);
    srDelay.prepare(spec);
    slDelay.setMaximumDelayInSamples(int(sampleRate * 0.05));
    srDelay.setMaximumDelayInSamples(int(sampleRate * 0.05));

    // Convolution engines are used for surround decorrelation tails.
    slConv.prepare(spec);
    srConv.prepare(spec);
    slConv.reset();
    srConv.reset();

    // Force first-time coefficient/IR rebuild.
    graphPrepared = true;
    lastRoomSize = lastDensity = lastReverbWet = -1.f;

    // Build initial graph from current parameter values.
    rebuildFilters();
    rebuildVelvetIRs();

    // Keep a previous snapshot for change-detection in process().
    prevParams = params;
}

// Clears history/state for all DSP blocks.
void UpmixEngine::reset()
{
    fcHPF .reset();
    lfeLP1.reset();
    lfeLP2.reset();
    lfeShelf.reset();
    slHPF .reset();
    srHPF .reset();
    slDelay.reset();
    srDelay.reset();
    slConv.reset();
    srConv.reset();
}

// Rebuilds coefficient-based DSP nodes when relevant controls change.
void UpmixEngine::rebuildFilters()
{
    if (!graphPrepared) return;

    // FC highpass
    *fcHPF.coefficients  = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, params.centerHPF, 0.707f);

    // LFE Linkwitz-Riley 4th order: two cascaded 2nd-order Butterworth LPFs
    *lfeLP1.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, params.lfeCrossover, 0.5f);
    *lfeLP2.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, params.lfeCrossover, 0.5f);

    // LFE low-shelf boost
    *lfeShelf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sr, 60.f, 0.707f, juce::Decibels::decibelsToGain(params.lfeShelfGain));

    // Surround highpass
    *slHPF.coefficients  = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, params.surroundHPF, 0.707f);
    *srHPF.coefficients  = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, params.surroundHPF, 0.707f);

    // Haas delays
    slDelay.setDelay(float(sr * params.haasDelayMs / 1000.0));
    srDelay.setDelay(float(sr * (params.haasDelayMs + 3.5f) / 1000.0));
}

// Builds a mono velvet-noise impulse response with controlled decay.
// This is used by convolution to create spacious but lightweight surround tails.
juce::AudioBuffer<float> UpmixEngine::makeVelvetIR(int lengthMs, float density,
                                                     float roomSize, unsigned seed)
{
    int len = int(sr * lengthMs / 1000.0);
    len = std::max(len, 64);

    juce::AudioBuffer<float> ir(1, len);
    auto* d = ir.getWritePointer(0);
    ir.clear();

    unsigned s = seed;
    // Deterministic pseudo-random generator so IRs are repeatable for same seed.
    auto rnd = [&]() -> float {
        s = s * 1664525u + 1013904223u;
        return float(s) / float(std::numeric_limits<unsigned>::max());
    };

    // Pulse period in samples: higher density => more frequent pulses.
    float period = float(sr) / density;
    period = std::max(period, 1.f);

    for (float p = 0.f; p < float(len); p += period)
    {
        int idx = juce::jlimit(0, len-1, int(p + rnd() * period));
        float sign = rnd() > 0.5f ? 1.f : -1.f;
        // Exponential decay envelope: models room acoustic decay
        float decay = std::exp(-5.5f * (1.f - roomSize) * float(idx) / float(len));
        d[idx] = sign * decay;
    }

    // Normalize energy so different room settings do not cause loudness jumps.
    float energy = 0.f;
    for (int i = 0; i < len; i++) energy += d[i]*d[i];
    if (energy > 1e-9f)
    {
        float sc = 1.f / std::sqrt(energy);
        for (int i = 0; i < len; i++) d[i] *= sc;
    }
    return ir;
}

// Rebuilds left/right IRs when room controls change.
// Slight asymmetry between channels increases perceived spaciousness.
void UpmixEngine::rebuildVelvetIRs()
{
    if (!graphPrepared) return;

    const int   irLengthMs = juce::jlimit(40, 220, int(35.0f + params.roomSize * 185.0f));
    const float slDensity  = params.velvetDensity;
    const float srDensity  = params.velvetDensity * 1.09f;

    auto slIR = makeVelvetIR(irLengthMs,      slDensity, juce::jlimit(0.2f, 0.98f, params.roomSize),          1337u);
    auto srIR = makeVelvetIR(irLengthMs + 11, srDensity, juce::jlimit(0.2f, 0.98f, params.roomSize * 0.965f), 4242u);

    slConv.loadImpulseResponse(std::move(slIR), sr,
        juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::no);

    srConv.loadImpulseResponse(std::move(srIR), sr,
        juce::dsp::Convolution::Stereo::no,
        juce::dsp::Convolution::Trim::no,
        juce::dsp::Convolution::Normalise::no);

    lastRoomSize  = params.roomSize;
    lastDensity   = params.velvetDensity;
    lastReverbWet = params.reverbWet;
}

// Simple soft saturation for LFE harmonic enhancement.
float UpmixEngine::excite(float x, float drive) const noexcept
{
    // Soft saturation that generates 2nd + 3rd harmonic distortion.
    // drive = 0 -> linear, drive = 1 -> heavy saturation
    float k = drive * 80.f;
    return (juce::MathConstants<float>::pi + k) * x
           / (juce::MathConstants<float>::pi + k * std::abs(x));
}

// Main upmix graph.
// Input: 2-channel stereo, Output: 6 channels in FL/FR/FC/LFE/SL/SR order.
void UpmixEngine::process(const juce::AudioBuffer<float>& stereoIn,
                           juce::AudioBuffer<float>&       sixChOut,
                           int numSamples,
                           bool surround51Active)
{
    jassert(stereoIn.getNumChannels() >= 2);
    jassert(sixChOut.getNumChannels() >= 6);
    jassert(sixChOut.getNumSamples()  >= numSamples);

    sixChOut.clear();

    // Rebuild DSP blocks when parameter groups change.
    if (params.centerHPF    != prevParams.centerHPF    ||
        params.lfeCrossover != prevParams.lfeCrossover  ||
        params.lfeShelfGain != prevParams.lfeShelfGain  ||
        params.surroundHPF  != prevParams.surroundHPF   ||
        params.haasDelayMs  != prevParams.haasDelayMs)
    {
        rebuildFilters();
    }
    if (params.roomSize      != lastRoomSize  ||
        params.velvetDensity != lastDensity)
    {
        rebuildVelvetIRs();
    }
    prevParams = params;

    const float* inL = stereoIn.getReadPointer(0);
    const float* inR = stereoIn.getReadPointer(1);

    float* outFL  = sixChOut.getWritePointer(0);
    float* outFR  = sixChOut.getWritePointer(1);
    float* outFC  = sixChOut.getWritePointer(2);
    float* outLFE = sixChOut.getWritePointer(3);
    float* outSL  = sixChOut.getWritePointer(4);
    float* outSR  = sixChOut.getWritePointer(5);

    if (!surround51Active)
    {
        // Stereo mode: direct FL/FR passthrough only.
        juce::FloatVectorOperations::copy(outFL, inL, numSamples);
        juce::FloatVectorOperations::copy(outFR, inR, numSamples);
        // Remaining channels stay silent.
        return;
    }

    // 5.1 upmix mode signal flow.

    // Working mono buffers for each branch in the processing graph.
    juce::AudioBuffer<float> midBuf(1, numSamples);
    juce::AudioBuffer<float> sideBuf(1, numSamples);
    juce::AudioBuffer<float> fcBuf(1, numSamples);
    juce::AudioBuffer<float> lfeBuf(1, numSamples);
    juce::AudioBuffer<float> surrBuf(1, numSamples);
    juce::AudioBuffer<float> slBuf(1, numSamples);
    juce::AudioBuffer<float> srBuf(1, numSamples);
    juce::AudioBuffer<float> slConvOut(1, numSamples);
    juce::AudioBuffer<float> srConvOut(1, numSamples);

    float* mid  = midBuf.getWritePointer(0);
    float* side = sideBuf.getWritePointer(0);

    // Mid/Side extraction from stereo input.
    for (int i = 0; i < numSamples; i++)
    {
        mid[i]  = (inL[i] + inR[i]) * 0.5f;
        side[i] = (inL[i] - inR[i]) * 0.5f;
    }

    // FL/FR keep full original stereo.
    juce::FloatVectorOperations::copyWithMultiply(outFL, inL, params.frontGain, numSamples);
    juce::FloatVectorOperations::copyWithMultiply(outFR, inR, params.frontGain, numSamples);

    // FC path: Mid -> high-pass -> gain -> center.
    {
        fcBuf.copyFrom(0, 0, midBuf, 0, 0, numSamples);
        juce::dsp::AudioBlock<float> blk(fcBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        fcHPF.process(ctx);
        juce::FloatVectorOperations::copyWithMultiply(outFC,
            fcBuf.getReadPointer(0), params.centerGain, numSamples);
    }

    // LFE path: Mid -> LR4 crossover -> shelf -> exciter -> gain.
    {
        lfeBuf.copyFrom(0, 0, midBuf, 0, 0, numSamples);

        juce::dsp::AudioBlock<float> blk(lfeBuf);
        juce::dsp::ProcessContextReplacing<float> ctx(blk);
        lfeLP1.process(ctx);    // 1st 2nd-order Butterworth LPF
        lfeLP2.process(ctx);    // 2nd 2nd-order stage -> LR4 total
        lfeShelf.process(ctx);  // Low shelf boost

        float* lfePtr = lfeBuf.getWritePointer(0);
        float  drive  = params.exciterDrive;
        for (int i = 0; i < numSamples; i++)
            lfePtr[i] = std::tanh(excite(lfePtr[i], drive));

        juce::FloatVectorOperations::copyWithMultiply(outLFE,
            lfeBuf.getReadPointer(0), params.lfeGain, numSamples);
    }

    // Surround source: side * blend + mid * midBlend.
    {
        float* surr = surrBuf.getWritePointer(0);
        for (int i = 0; i < numSamples; i++)
            surr[i] = side[i] * params.sideBlend
                    + mid[i]  * params.midBlend;
    }

    const float wet = std::pow(juce::jlimit(0.f, 1.f, params.reverbWet), 0.65f);
    const float dry = 1.f - wet;
    const float roomBoost = 0.65f + params.roomSize * 0.55f;
    const float densityNorm = juce::jlimit(0.f, 1.f, (params.velvetDensity - 500.0f) / 3500.0f);
    const float decorCross = 0.05f + densityNorm * 0.32f;

    // SL path: surround source -> HPF -> Haas delay -> velvet convolution.
    {
        slBuf.copyFrom(0, 0, surrBuf, 0, 0, numSamples);

        {
            juce::dsp::AudioBlock<float> blk(slBuf);
            juce::dsp::ProcessContextReplacing<float> ctx(blk);
            slHPF.process(ctx);
        }

        // Haas delay
        float* slPtr = slBuf.getWritePointer(0);
        for (int i = 0; i < numSamples; i++)
        {
            slDelay.pushSample(0, slPtr[i]);
            slPtr[i] = slDelay.popSample(0);
        }

        // Velvet noise convolution (decorrelation)
        slConvOut.copyFrom(0, 0, slBuf, 0, 0, numSamples);
        {
            juce::dsp::AudioBlock<float> blk(slConvOut);
            juce::dsp::ProcessContextReplacing<float> ctx(blk);
            slConv.process(ctx);
        }

        // Wet/dry blend
        const float* slDry = slBuf.getReadPointer(0);
        const float* slWet = slConvOut.getReadPointer(0);
        for (int i = 0; i < numSamples; i++)
            outSL[i] = (slDry[i] * dry + (slWet[i] * roomBoost) * wet);
    }

    // SR path: same as SL with separate IR and extra delay.
    {
        srBuf.copyFrom(0, 0, surrBuf, 0, 0, numSamples);

        {
            juce::dsp::AudioBlock<float> blk(srBuf);
            juce::dsp::ProcessContextReplacing<float> ctx(blk);
            srHPF.process(ctx);
        }

        float* srPtr = srBuf.getWritePointer(0);
        for (int i = 0; i < numSamples; i++)
        {
            srDelay.pushSample(0, srPtr[i]);
            srPtr[i] = srDelay.popSample(0);
        }

        srConvOut.copyFrom(0, 0, srBuf, 0, 0, numSamples);
        {
            juce::dsp::AudioBlock<float> blk(srConvOut);
            juce::dsp::ProcessContextReplacing<float> ctx(blk);
            srConv.process(ctx);
        }

        const float* srDry = srBuf.getReadPointer(0);
        const float* srWet = srConvOut.getReadPointer(0);
        for (int i = 0; i < numSamples; i++)
            outSR[i] = (srDry[i] * dry + (srWet[i] * roomBoost) * wet);
    }

    // Stereo surround decorrelation matrix:
    // higher density creates stronger left/right independence.
    for (int i = 0; i < numSamples; i++)
    {
        const float sl = outSL[i];
        const float srChan = outSR[i];
        outSL[i] = (sl - srChan * decorCross) * params.surroundGain;
        outSR[i] = (srChan - sl * decorCross) * params.surroundGain;
    }
}

//==============================================================================
//  LOOK AND FEEL
//==============================================================================
// Central look-and-feel object used by custom sliders/buttons in this app.
StudioLookAndFeel::StudioLookAndFeel()
{
    setColour(juce::Slider::thumbColourId,          COL_ACC);
    setColour(juce::Slider::trackColourId,          COL_BG3);
    setColour(juce::Slider::backgroundColourId,     COL_BG2);
    setColour(juce::Label::textColourId,            COL_TXT);
    setColour(juce::TextButton::buttonColourId,     COL_PANEL);
    setColour(juce::TextButton::textColourOffId,    COL_TXT);
    setColour(juce::ComboBox::backgroundColourId,   COL_BG2);
}

// Draws horizontal sliders with a glowing fill + round thumb.
void StudioLookAndFeel::drawLinearSlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float minPos, float maxPos,
    juce::Slider::SliderStyle style, juce::Slider& slider)
{
    juce::ignoreUnused(minPos, maxPos, style, slider);

    juce::Colour accent = COL_ACC;
    if (auto v = slider.getProperties().getVarPointer("accent"); v != nullptr)
        accent = juce::Colour::fromString(v->toString());

    const float centreY = float(y) + float(h) * 0.5f;
    const float trackH = juce::jlimit(4.0f, 6.0f, float(h) * 0.32f);
    auto trackBounds = juce::Rectangle<float>(float(x), centreY - trackH * 0.5f, float(w), trackH);
    g.setColour(COL_BG3.withAlpha(0.82f));
    g.fillRoundedRectangle(trackBounds, trackH * 0.5f);

    auto fill = trackBounds.withWidth(juce::jlimit(0.0f, trackBounds.getWidth(), sliderPos - float(x)));
    juce::ColourGradient fg(accent.withAlpha(0.96f), fill.getX(), fill.getY(),
                            accent.withAlpha(0.72f), fill.getRight(), fill.getBottom(), false);
    g.setGradientFill(fg);
    g.fillRoundedRectangle(fill, trackH * 0.5f);

    g.setColour(accent.withAlpha(0.24f));
    g.fillEllipse(sliderPos - 7.0f, centreY - 7.0f, 14.f, 14.f);
    g.setColour(accent);
    g.fillEllipse(sliderPos - 4.6f, centreY - 4.6f, 9.2f, 9.2f);
    g.setColour(juce::Colours::white.withAlpha(0.80f));
    g.fillEllipse(sliderPos - 1.6f, centreY - 1.6f, 3.2f, 3.2f);
}

// Draws rotary sliders (used if a control switches to rotary style in future).
void StudioLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int w, int h,
    float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    juce::ignoreUnused(slider);
    auto bounds = juce::Rectangle<int>(x,y,w,h).toFloat().reduced(4);
    auto cx = bounds.getCentreX(), cy = bounds.getCentreY();
    auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.4f;

    g.setColour(COL_BG3);
    g.drawEllipse(cx-radius, cy-radius, radius*2, radius*2, 2.f);

    auto angle = startAngle + sliderPos * (endAngle - startAngle);
    juce::Path p;
    p.addLineSegment({cx, cy, cx + radius * std::sin(angle), cy - radius * std::cos(angle)}, 3.f);
    g.setColour(COL_ACC);
    g.strokePath(p, juce::PathStrokeType(2.5f));
}

// Creates a transparent text box style for slider labels when needed.
juce::Label* StudioLookAndFeel::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setColour(juce::Label::textColourId, COL_TXT);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

//==============================================================================
//  PARAM SLIDER
//==============================================================================
// Reusable UI widget: parameter name + value readout + custom slider.
ParamSlider::ParamSlider(const juce::String& label, float minVal, float maxVal,
                         float defaultVal, const juce::String& unit,
                         juce::Colour accent)
    : unitStr(unit), accentCol(accent),
      minV(minVal), maxV(maxVal), labelStr(label)
{
    slider.setRange(double(minVal), double(maxVal), 0.0);
    slider.setValue(double(defaultVal), juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.getProperties().set("accent", accent.toString());

    nameLabel.setText(label, juce::dontSendNotification);
    nameLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.92f));
    nameLabel.setFont(juce::Font(juce::FontOptions("Segoe UI", fs(13.f), juce::Font::plain)));

    valueLabel.setColour(juce::Label::textColourId, accent.brighter(0.2f));
    valueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    valueLabel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    valueLabel.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(14.f), juce::Font::bold)));
    valueLabel.setJustificationType(juce::Justification::centredRight);
    valueLabel.setBorderSize({ 0, 0, 0, 0 });

    slider.onValueChange = [this] { sliderChanged(); };
    sliderChanged();  // set initial label

    addAndMakeVisible(slider);
    addAndMakeVisible(nameLabel);
    addAndMakeVisible(valueLabel);
}

// Layouts caption/value row above the slider track.
void ParamSlider::resized()
{
    auto b = getLocalBounds();
    const int topH = juce::jlimit(16, 24, getHeight() / 2);
    auto top = b.removeFromTop(topH);
    auto valueW = juce::jlimit(74, 120, b.getWidth() / 4);
    nameLabel.setBounds(top.removeFromLeft(top.getWidth() - valueW));
    valueLabel.setBounds(top.reduced(0, 1));
    slider.setBounds(b.reduced(0, juce::jmax(1, topH / 6)));
}

// Formats and displays current value text, then notifies caller.
void ParamSlider::sliderChanged()
{
    float v = float(slider.getValue());
    juce::String txt;
    if (unitStr == "Hz")
        txt = juce::String(juce::roundToInt(v)) + " Hz";
    else if (unitStr == "ms")
        txt = juce::String(juce::roundToInt(v)) + " ms";
    else if (unitStr == "dB")
        txt = "+" + juce::String(v, 1) + " dB";
    else
        txt = juce::String(v, 2) + (unitStr.isEmpty() ? "" : " " + unitStr);

    valueLabel.setText(txt, juce::dontSendNotification);

    if (onChange) onChange(v);
}

//==============================================================================
//  MODE BUTTON
//==============================================================================
// Toggle button with custom rendering for Stereo vs 5.1 mode selection.
ModeButton::ModeButton(const juce::String& label, juce::Colour col)
    : juce::Button(label), colour(col)
{
    setClickingTogglesState(true);
}

// Paints the split title/subtitle style used by mode buttons.
void ModeButton::paintButton(juce::Graphics& g, bool hover, bool)
{
    auto b = getLocalBounds().toFloat().reduced(1.0f);
    const bool active = getToggleState();

    const auto title = getButtonText().upToFirstOccurrenceOf("\n", false, false);
    const auto subtitle = getButtonText().fromFirstOccurrenceOf("\n", false, false).trim();

    auto base = active ? colour.withAlpha(0.14f) : COL_BG2.withAlpha(0.90f);
    if (hover && !active)
        base = base.brighter(0.05f);

    g.setColour(base);
    g.fillRoundedRectangle(b, 8.0f);

    g.setColour(active ? colour.withAlpha(0.70f) : COL_BRDR.withAlpha(0.65f));
    g.drawRoundedRectangle(b, 8.0f, active ? 1.1f : 0.9f);

    auto content = b.toNearestInt().reduced(12, 7);
    auto badge = content.removeFromRight(44).reduced(0, 4);

    auto titleRow = content.removeFromTop(content.getHeight() / 2 + 1);
    g.setColour(juce::Colours::white.withAlpha(active ? 1.0f : 0.90f));
    g.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(15.0f * 0.80f), juce::Font::bold)));
    g.drawFittedText(title, titleRow, juce::Justification::centredLeft, 1);

    g.setColour(COL_MUT.brighter(0.15f));
    g.setFont(juce::Font(juce::FontOptions("Segoe UI", fs(10.0f), juce::Font::plain)));
    g.drawFittedText(subtitle, content, juce::Justification::centredLeft, 1);

    g.setColour(COL_BG0.withAlpha(0.55f));
    g.fillRoundedRectangle(badge.toFloat(), 6.0f);
    g.setColour(active ? colour.withAlpha(0.58f) : COL_BRDR.withAlpha(0.58f));
    g.drawRoundedRectangle(badge.toFloat(), 6.0f, 0.9f);
    g.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(10.5f), juce::Font::bold)));
    g.setColour(juce::Colours::white.withAlpha(active ? 0.95f : 0.80f));
    g.drawFittedText(title.containsIgnoreCase("stereo") ? "2.0" : "5.1", badge, juce::Justification::centred, 1);
}

//==============================================================================
//  CHANNEL METER
//==============================================================================
// Vertical per-channel level meter with peak hold and focus highlight.
ChannelMeter::ChannelMeter(const juce::String& label, juce::Colour col)
    : lbl(label), col(col)
{
    startTimerHz(30);
}

// Pushes one RMS value from the audio thread; applies simple smoothing.
void ChannelMeter::push(float rms)
{
    level = std::max(level * 0.85f, rms);
    if (rms > peak) { peak = rms; peakHoldMs = 1200; }
}

// Sets temporary highlight strength to show which channel a control affects.
void ChannelMeter::setEffectFocus(float amount)
{
    focus = juce::jlimit(0.f, 1.f, amount);
}

// Decays held peak marker and repaints at UI rate.
void ChannelMeter::timerCallback()
{
    peak = peakHoldMs > 0 ? peak : peak * 0.95f;
    peakHoldMs = std::max(0, peakHoldMs - 33);
    repaint();
}

// Draws meter fill, held peak line, and channel label.
void ChannelMeter::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    auto barArea = b.reduced(2.f).withTrimmedBottom(16.f);

    g.setColour(COL_BG3);
    g.fillRoundedRectangle(barArea, 3.f);

    float fillH = juce::jlimit(0.f, 1.f, level * 4.f);
    auto fill = barArea.withTop(barArea.getBottom() - fillH * barArea.getHeight());
    g.setColour(col.withAlpha(0.85f));
    g.fillRoundedRectangle(fill, 3.f);

    if (focus > 0.001f)
    {
        auto glow = barArea.expanded(2.f);
        g.setColour(col.withAlpha(0.25f * focus));
        g.drawRoundedRectangle(glow, 4.f, 2.4f);
        g.setColour(juce::Colours::white.withAlpha(0.18f * focus));
        g.drawRoundedRectangle(glow.reduced(1.f), 4.f, 1.0f);
    }

    // Peak marker
    float pkY = barArea.getBottom() - juce::jlimit(0.f,1.f,peak*4.f) * barArea.getHeight();
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.fillRect(barArea.getX(), pkY, barArea.getWidth(), 1.5f);

    // Label
    g.setColour(COL_MUT);
    g.setFont(juce::Font(juce::FontOptions(fs(10.f))));
    g.drawText(lbl, b.withTop(b.getBottom()-16.f).toNearestInt(),
               juce::Justification::centred);
}

// No child controls to lay out currently.
void ChannelMeter::resized() {}

//==============================================================================
//  SPECTRUM DISPLAY
//==============================================================================
// Lightweight FFT display fed by FL output for a live spectral view.
SpectrumDisplay::SpectrumDisplay()
{
    startTimerHz(30);
}

// Feeds incoming samples into the FFT fifo.
void SpectrumDisplay::pushBuffer(const float* data, int n)
{
    for (int i = 0; i < n; i++)
    {
        fifo[fifoIdx++] = data[i];
        if (fifoIdx == FFT_SIZE) { nextFFT = true; fifoIdx = 0; }
    }
}

// Redraw only when a new FFT frame is ready.
void SpectrumDisplay::timerCallback()
{
    if (nextFFT) { drawNextFrameOfSpectrum(); nextFFT = false; repaint(); }
}

// Runs FFT and maps bins into the compact UI scope arrays.
void SpectrumDisplay::drawNextFrameOfSpectrum()
{
    std::fill(fftData, fftData + FFT_SIZE*2, 0.f);
    std::copy(fifo, fifo + FFT_SIZE, fftData);
    window.multiplyWithWindowingTable(fftData, FFT_SIZE);
    fft.performFrequencyOnlyForwardTransform(fftData);

    int nBins = (int)std::size(scopeData);
    for (int i = 0; i < nBins; i++)
    {
        float skew = std::pow(float(i)/float(nBins-1), 0.3f);
        int   bin  = juce::jlimit(1, FFT_SIZE/2-1, int(skew * (FFT_SIZE/2)));
        float dB   = juce::Decibels::gainToDecibels(fftData[bin]) + 60.f;
        float norm = juce::jlimit(0.f, 1.f, dB / 60.f);
        scopeData[i] = norm;
        scopeSmooth[i] = scopeSmooth[i] * 0.80f + norm * 0.20f;
        scopePeak[i] = std::max(scopePeak[i] * 0.96f, scopeSmooth[i]);
    }
}

// Draws smoothed spectrum curve, peak trace, and filled area.
void SpectrumDisplay::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient bg(COL_BG1.brighter(0.08f), b.getX(), b.getY(),
                            COL_BG2, b.getX(), b.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(b, 6.f);

    auto plot = b.reduced(6.f, 5.f);

    g.setColour(COL_BG3.withAlpha(0.9f));
    for (int i = 1; i <= 4; ++i)
    {
        float y = juce::jmap(float(i), 0.0f, 4.0f, plot.getBottom(), plot.getY());
        g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
    }

    juce::Path line, fill, peakLine;
    const int n = (int)std::size(scopeData);
    const juce::Colour accent = isSurround ? COL_ORG : COL_ACC;

    for (int i = 0; i < n; ++i)
    {
        float x = juce::jmap(float(i), 0.0f, float(n - 1), plot.getX(), plot.getRight());
        float y = juce::jmap(scopeSmooth[i], 0.0f, 1.0f, plot.getBottom(), plot.getY());
        float yPeak = juce::jmap(scopePeak[i], 0.0f, 1.0f, plot.getBottom(), plot.getY());

        if (i == 0)
        {
            line.startNewSubPath(x, y);
            peakLine.startNewSubPath(x, yPeak);
            fill.startNewSubPath(x, plot.getBottom());
            fill.lineTo(x, y);
        }
        else
        {
            line.lineTo(x, y);
            peakLine.lineTo(x, yPeak);
            fill.lineTo(x, y);
        }
    }

    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.closeSubPath();

    g.setColour(accent.withAlpha(0.16f));
    g.fillPath(fill);
    g.setColour(accent.withAlpha(0.24f));
    g.strokePath(line, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent.withAlpha(0.95f));
    g.strokePath(line, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colours::white.withAlpha(0.45f));
    g.strokePath(peakLine, juce::PathStrokeType(1.0f));
}

//==============================================================================
//  CHANNEL SCOPE DISPLAY
//==============================================================================
// Constructor has no dynamic state beyond default arrays.
ChannelScopeDisplay::ChannelScopeDisplay() = default;

// Pushes one frame of 6-channel levels into scrolling history buffers.
void ChannelScopeDisplay::pushLevels(const float levels[6])
{
    for (int ch = 0; ch < 6; ++ch)
    {
        for (int i = 0; i < historySize - 1; ++i)
            history[ch][i] = history[ch][i + 1];

        latest[ch] = juce::jlimit(0.0f, 1.0f, levels[ch] * 3.0f);
        history[ch][historySize - 1] = latest[ch];
    }

    repaint();
}

// Draws six horizontal mini-scopes (one lane per channel).
void ChannelScopeDisplay::paint(juce::Graphics& g)
{
    static const char* labels[6] = { "FL", "FR", "FC", "LFE", "SL", "SR" };
    static const juce::Colour cols[6] =
    {
        juce::Colour(0xff00cfff), juce::Colour(0xff00cfff), juce::Colour(0xff00e896),
        juce::Colour(0xffffd700), juce::Colour(0xffff5e00), juce::Colour(0xffff5e00)
    };

    auto b = getLocalBounds().toFloat();
    g.setColour(COL_BG1.withAlpha(0.95f));
    g.fillRoundedRectangle(b, 6.f);

    auto lanes = b.reduced(4.f);
    const float laneH = lanes.getHeight() / 6.0f;

    for (int ch = 0; ch < 6; ++ch)
    {
        auto lane = lanes.removeFromTop(laneH).reduced(0.f, 1.f);
        g.setColour(COL_BG3.withAlpha(0.68f));
        g.fillRoundedRectangle(lane, 3.f);

        auto leftLabel = lane.removeFromLeft(38.f);
        auto rightInfo = lane.removeFromRight(56.f);
        auto plot = lane.reduced(3.f, 2.f);

        juce::Path p;
        for (int i = 0; i < historySize; ++i)
        {
            float x = juce::jmap(float(i), 0.0f, float(historySize - 1), plot.getX(), plot.getRight());
            float y = juce::jmap(history[ch][i], 0.0f, 1.0f, plot.getBottom(), plot.getY());
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }

        const bool active = latest[ch] > 0.02f;
        const auto c = cols[ch];
        g.setColour(c.withAlpha(active ? 0.85f : 0.35f));
        g.strokePath(p, juce::PathStrokeType(active ? 1.8f : 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(c.withAlpha(active ? 0.95f : 0.45f));
        g.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(10.f), juce::Font::bold)));
        g.drawText(labels[ch], leftLabel.toNearestInt(), juce::Justification::centred);

        g.setColour(active ? juce::Colours::white.withAlpha(0.85f) : COL_MUT.brighter(0.2f));
        g.setFont(juce::Font(juce::FontOptions("Segoe UI", fs(9.f), juce::Font::plain)));
        g.drawText(active ? "ACTIVE" : "IDLE", rightInfo.toNearestInt(), juce::Justification::centred);
    }
}

//==============================================================================
//  PARAMS CONTENT
//==============================================================================
// Stores card bounds computed in MainComponent::resized().
void ParamsContent::setCards(const juce::Rectangle<int>& front,
                             const juce::Rectangle<int>& lfe,
                             const juce::Rectangle<int>& surround,
                             const juce::Rectangle<int>& space)
{
    frontCard = front;
    lfeCard = lfe;
    surroundCard = surround;
    spaceCard = space;
    repaint();
}

// Paints stylized backgrounds for the four parameter sections.
void ParamsContent::paint(juce::Graphics& g)
{
    auto drawCard = [&](juce::Rectangle<int> card, juce::Colour accent)
    {
        if (card.isEmpty())
            return;

        auto b = card.toFloat();
        g.setColour(COL_BG1.withAlpha(0.90f));
        g.fillRoundedRectangle(b, 10.0f);

        g.setColour(COL_BRDR.withAlpha(0.62f));
        g.drawRoundedRectangle(b, 10.0f, 1.0f);

        auto titleBand = b.removeFromTop(22.0f);
        g.setColour(accent.withAlpha(0.07f));
        g.fillRoundedRectangle(titleBand, 9.0f);
        g.setColour(accent.withAlpha(0.26f));
        g.drawLine(titleBand.getX() + 12.0f, titleBand.getBottom() - 0.5f,
                   titleBand.getRight() - 12.0f, titleBand.getBottom() - 0.5f, 1.0f);
    };

    drawCard(frontCard,    COL_ACC);
    drawCard(lfeCard,      COL_YLW);
    drawCard(surroundCard, COL_ORG);
    drawCard(spaceCard,    COL_PRP);
}

//==============================================================================
//  MAIN COMPONENT
//==============================================================================
// Builds the full app: UI controls, audio routing, default state, and timers.
MainComponent::MainComponent()
{
    setLookAndFeel(&laf);

    formatManager.registerBasicFormats();

    auto styleButton = [](juce::TextButton& b, juce::Colour base, bool emphasize = false)
    {
        b.setColour(juce::TextButton::buttonColourId, base.withAlpha(emphasize ? 0.40f : 0.28f));
        b.setColour(juce::TextButton::buttonOnColourId, base.withAlpha(0.52f));
        b.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    };

    fileBtn.setButtonText("Open Audio File");
    exportBtn.setButtonText("Export 5.1 WAV");
    settingsBtn.setButtonText("Audio Settings");

    fileLabel.setColour(juce::Label::textColourId, COL_TXT);
    fileLabel.setColour(juce::Label::backgroundColourId, COL_BG0.withAlpha(0.72f));
    fileLabel.setColour(juce::Label::outlineColourId, COL_BRDR.withAlpha(0.82f));
    fileLabel.setText("No file loaded", juce::dontSendNotification);
    fileLabel.setBorderSize({ 3, 10, 3, 10 });
    fileLabel.setJustificationType(juce::Justification::centredLeft);
    fileLabel.setFont(juce::Font(juce::FontOptions("Segoe UI", fs(12.f), juce::Font::plain)));

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.94f));
    statusLabel.setColour(juce::Label::backgroundColourId, COL_BG0.withAlpha(0.65f));
    statusLabel.setColour(juce::Label::outlineColourId, COL_BRDR.withAlpha(0.72f));
    statusLabel.setBorderSize({ 2, 10, 2, 10 });
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(11.f), juce::Font::bold)));

    channelImpactLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.88f));
    channelImpactLabel.setColour(juce::Label::backgroundColourId, COL_BG0.withAlpha(0.58f));
    channelImpactLabel.setColour(juce::Label::outlineColourId, COL_BRDR.withAlpha(0.6f));
    channelImpactLabel.setBorderSize({ 2, 10, 2, 10 });
    channelImpactLabel.setJustificationType(juce::Justification::centredLeft);
    channelImpactLabel.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(10.f), juce::Font::bold)));
    channelImpactLabel.setText("Effect chain: Master > Front L/R > Front C > LFE > Surround L/R", juce::dontSendNotification);

    timelineSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timelineSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    timelineSlider.setRange(0.0, 1.0, 0.001);
    timelineSlider.setColour(juce::Slider::trackColourId, COL_ACC.withAlpha(0.75f));
    timelineSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white.withAlpha(0.95f));
    timelineSlider.getProperties().set("accent", COL_ACC.toString());
    timelineSlider.onDragStart = [this] { timelineBeingDragged = true; };
    timelineSlider.onDragEnd = [this]
    {
        timelineBeingDragged = false;
        if (fileLoaded)
            transport.setPosition(timelineSlider.getValue());
        updateTimelineFromTransport();
    };
    timelineSlider.onValueChange = [this]
    {
        if (timelineBeingDragged && fileLoaded)
            transport.setPosition(timelineSlider.getValue());
    };

    timelineLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.9f));
    timelineLabel.setJustificationType(juce::Justification::centredRight);
    timelineLabel.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(11.f), juce::Font::bold)));
    timelineLabel.setText("00:00 / 00:00", juce::dontSendNotification);

    presetLabel.setText("Scene Presets", juce::dontSendNotification);
    presetLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.96f));
    presetLabel.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(12.f), juce::Font::bold)));

    styleButton(fileBtn, COL_ACC);
    styleButton(exportBtn, COL_YLW);
    styleButton(settingsBtn, COL_GRN);
    styleButton(playBtn, COL_GRN, true);
    styleButton(stopBtn, COL_ORG);
    styleButton(presetCinema, juce::Colour(0xfff0a000));
    styleButton(presetMusic, juce::Colour(0xff3da7ff));
    styleButton(presetVocal, juce::Colour(0xff55d19a));
    styleButton(presetReset, COL_MUT);

    addAndMakeVisible(fileBtn);
    addAndMakeVisible(exportBtn);
    addAndMakeVisible(settingsBtn);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(channelImpactLabel);
    addAndMakeVisible(timelineSlider);
    addAndMakeVisible(timelineLabel);
    addAndMakeVisible(playBtn);
    addAndMakeVisible(stopBtn);
    addAndMakeVisible(presetLabel);
    addAndMakeVisible(presetCinema);
    addAndMakeVisible(presetMusic);
    addAndMakeVisible(presetVocal);
    addAndMakeVisible(presetReset);
    addAndMakeVisible(spectrum);
    addAndMakeVisible(channelScope);
    addAndMakeVisible(paramsViewport);

    paramsViewport.setViewedComponent(&paramsContent, false);
    paramsViewport.setScrollBarsShown(true, false, true, false);
    paramsViewport.setSingleStepSizes(28, 28);

    fileBtn.onClick      = [this] { openFile(); };
    exportBtn.onClick    = [this] { exportCurrentTrackTo51Wav(); };
    settingsBtn.onClick  = [this] { openAudioSettings(); };
    playBtn.onClick      = [this] {
        if (!fileLoaded)
        {
            fileLabel.setText("Load a file first", juce::dontSendNotification);
            return;
        }
        transport.start();
        setPlayState(true);
    };
    stopBtn.onClick      = [this] { transport.stop(); transport.setPosition(0.0); setPlayState(false); };
    presetCinema.onClick = [this] { applyPresetCinema(); };
    presetMusic.onClick  = [this] { applyPresetMusicWide(); };
    presetVocal.onClick  = [this] { applyPresetVocalFocus(); };
    presetReset.onClick  = [this] { resetToDefaultPreset(); };

    btnStereo.setRadioGroupId(1);
    btn51.setRadioGroupId(1);
    btnStereo.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(btnStereo);
    addAndMakeVisible(btn51);
    btnStereo.addListener(this);
    btn51.addListener(this);

    masterVol.setRange(0.0, 1.5, 0.01);
    masterVol.setValue(1.0);
    masterVol.setSliderStyle(juce::Slider::LinearHorizontal);
    masterVol.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    masterVol.getProperties().set("accent", COL_ACC.toString());
    masterVolLabel.setText("Master Level", juce::dontSendNotification);
    masterVolLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.65f));
    masterVolLabel.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(11.f), juce::Font::bold)));
    masterVol.onValueChange = [this] {
        flashChannelFocus({0, 1, 2, 3, 4, 5}, "Master Level");
    };
    addAndMakeVisible(masterVol);
    addAndMakeVisible(masterVolLabel);

    addAndMakeVisible(mFL); addAndMakeVisible(mFR);
    addAndMakeVisible(mFC); addAndMakeVisible(mLFE);
    addAndMakeVisible(mSL); addAndMakeVisible(mSR);

    for (auto* l : {&lblFront, &lblLFE, &lblSurround, &lblSpace})
    {
        l->setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(13.5f), juce::Font::bold)));
        l->setMinimumHorizontalScale(0.80f);
        l->setColour(juce::Label::textColourId, COL_MUT.brighter(0.55f));
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setBorderSize({ 0, 0, 9, 0 });
        l->setJustificationType(juce::Justification::centredLeft);
        paramsContent.addAndMakeVisible(l);
    }
    lblFront.setText   ("FRONT STAGE - FL / FR / FC" , juce::dontSendNotification);
    lblLFE.setText     ("SUB BASS - LFE", juce::dontSendNotification);
    lblSurround.setText("SURROUND FIELD - SL / SR", juce::dontSendNotification);
    lblSpace.setText   ("SPACE / DECORRELATION", juce::dontSendNotification);
    lblFront.setColour   (juce::Label::textColourId, COL_ACC);
    lblLFE.setColour     (juce::Label::textColourId, COL_YLW);
    lblSurround.setColour(juce::Label::textColourId, COL_ORG);
    lblSpace.setColour   (juce::Label::textColourId, COL_PRP);

    auto wire = [this](ParamSlider& ps) {
        ps.onChange = [this, &ps](float) {
            if (suppressSliderCallbacks)
                return;
            syncParamsFromSliders();
            onSliderEdited(&ps);
        };
        paramsContent.addAndMakeVisible(ps);
    };
    for (auto* ps : {&sFrontGain,&sCenterGain,&sCenterHPF,
                     &sLFEGain,&sLFEHz,&sLFEShelf,&sExciter,
                     &sSurrGain,&sHaasMs,&sSurrHPF,&sSideBlend,&sMidBlend,
                     &sReverbWet,&sRoomSize,&sVelvetDens})
        wire(*ps);

    // Audio device setup
    setAudioChannels(0, 2);
    configureOutputForCurrentDevice();
    deviceManager.addChangeListener(this);

    refreshStatusLabel();
    updateTimelineFromTransport();

    setSize(1120, 860);
    startTimerHz(30);
}

// Cleans up listeners and shuts down audio safely.
MainComponent::~MainComponent()
{
    deviceManager.removeChangeListener(this);
    shutdownAudio();
    setLookAndFeel(nullptr);
}

// Called by JUCE when the audio device starts or changes sample rate/buffer size.
void MainComponent::prepareToPlay(int blockSize, double sampleRate)
{
    currentSampleRate = sampleRate;
    transport.prepareToPlay(blockSize, sampleRate);
    engine.prepare(sampleRate, blockSize);
    sixChBuf.setSize(6, blockSize, false, true);
    syncParamsFromSliders();
}

// Called by JUCE when playback is stopping or audio device is being released.
void MainComponent::releaseResources()
{
    transport.releaseResources();
    engine.reset();
}

// Real-time audio callback: reads stereo file data, upmixes to 5.1, and writes to hardware output.
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    if (!fileLoaded || !transport.isPlaying())
    {
        std::fill(std::begin(meterRms), std::end(meterRms), 0.0f);
        return;
    }

    // Get stereo from transport into a temp buffer
    int numSamples = info.numSamples;
    juce::AudioBuffer<float> stereoTmp(2, numSamples);
    stereoTmp.clear();
    juce::AudioSourceChannelInfo stereoInfo(&stereoTmp, 0, numSamples);
    transport.getNextAudioBlock(stereoInfo);

    // Process into 6-channel output
    sixChBuf.setSize(6, numSamples, false, false, true);
    engine.process(stereoTmp, sixChBuf, numSamples, surround51Active);

    // Apply master volume and write to hardware output buffer
    float vol = float(masterVol.getValue());
    for (int ch = 0; ch < std::min(6, info.buffer->getNumChannels()); ch++)
    {
        float* dest = info.buffer->getWritePointer(ch, info.startSample);
        const float* src = sixChBuf.getReadPointer(ch);
        juce::FloatVectorOperations::copyWithMultiply(dest, src, vol, numSamples);
    }

    // Feed meters & spectrum (from the output buffer)
    for (int ch = 0; ch < 6; ch++)
    {
        const float* data = sixChBuf.getReadPointer(ch);
        float rms = 0.f;
        for (int i = 0; i < numSamples; i++) rms += data[i]*data[i];
        meterRms[ch] = std::sqrt(rms / numSamples);
    }
    // Spectrum from FL
    spectrum.pushBuffer(sixChBuf.getReadPointer(0), numSamples);
}

// UI timer: refreshes meters, timeline, and device-follow checks on the message thread.
void MainComponent::timerCallback()
{
    // Push meter values (called from message thread)
    mFL .push(meterRms[0]);
    mFR .push(meterRms[1]);
    mFC .push(meterRms[2]);
    mLFE.push(meterRms[3]);
    mSL .push(meterRms[4]);
    mSR .push(meterRms[5]);
    channelScope.pushLevels(meterRms);
    spectrum.setSurroundMode(surround51Active);
    updateTimelineFromTransport();

    for (int ch = 0; ch < 6; ++ch)
        channelFocus[ch] = std::max(0.0f, channelFocus[ch] - 0.05f);

    mFL .setEffectFocus(channelFocus[0]);
    mFR .setEffectFocus(channelFocus[1]);
    mFC .setEffectFocus(channelFocus[2]);
    mLFE.setEffectFocus(channelFocus[3]);
    mSL .setEffectFocus(channelFocus[4]);
    mSR .setEffectFocus(channelFocus[5]);

    if (++statusRefreshTick >= 20)
    {
        statusRefreshTick = 0;
        refreshStatusLabel();
    }

    if (++outputPollTick >= 15)
    {
        outputPollTick = 0;
        followSystemOutputDevice();
    }
}

// Handles Stereo/5.1 mode toggle buttons.
void MainComponent::buttonClicked(juce::Button* b)
{
    if (b == &btnStereo) { surround51Active = false; engine.reset(); flashChannelFocus({0, 1}, "Stereo Mode"); }
    if (b == &btn51)     { surround51Active = true;  engine.reset(); flashChannelFocus({0, 1, 2, 3, 4, 5}, "5.1 Mode"); }
    refreshStatusLabel();
}

// Reacts to device-manager changes (driver switch, channel count changes, etc.).
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &deviceManager && !deviceReconfigInProgress)
    {
        configureOutputForCurrentDevice();
        updateTimelineFromTransport();
    }
}

// Pulls all UI slider values into the DSP parameter model.
void MainComponent::syncParamsFromSliders()
{
    engine.params.frontGain     = float(sFrontGain .slider.getValue());
    engine.params.centerGain    = float(sCenterGain.slider.getValue());
    engine.params.centerHPF     = float(sCenterHPF .slider.getValue());
    engine.params.lfeGain       = float(sLFEGain   .slider.getValue());
    engine.params.lfeCrossover  = float(sLFEHz     .slider.getValue());
    engine.params.lfeShelfGain  = float(sLFEShelf  .slider.getValue());
    engine.params.exciterDrive  = float(sExciter   .slider.getValue());
    engine.params.surroundGain  = float(sSurrGain  .slider.getValue());
    engine.params.haasDelayMs   = float(sHaasMs    .slider.getValue());
    engine.params.surroundHPF   = float(sSurrHPF   .slider.getValue());
    engine.params.sideBlend     = float(sSideBlend .slider.getValue());
    engine.params.midBlend      = float(sMidBlend  .slider.getValue());
    engine.params.reverbWet     = float(sReverbWet .slider.getValue());
    engine.params.roomSize      = float(sRoomSize  .slider.getValue());
    engine.params.velvetDensity = float(sVelvetDens.slider.getValue());
}

// Briefly highlights channels affected by a control change.
void MainComponent::flashChannelFocus(std::initializer_list<int> channels, const juce::String& sourceName)
{
    static const char* names[6] = { "FL", "FR", "FC", "LFE", "SL", "SR" };

    juce::String targets;
    bool first = true;

    for (int ch : channels)
    {
        if (!juce::isPositiveAndBelow(ch, 6))
            continue;

        channelFocus[ch] = 1.0f;
        if (!first)
            targets << " ";
        targets << names[ch];
        first = false;
    }

    if (targets.isNotEmpty())
        channelImpactLabel.setText("Effect: " + sourceName + " -> " + targets, juce::dontSendNotification);
}

// Maps each slider to its impacted channels for immediate visual feedback.
void MainComponent::onSliderEdited(ParamSlider* source)
{
    if (source == &sFrontGain)     { flashChannelFocus({0, 1}, "Front Level"); return; }
    if (source == &sCenterGain)    { flashChannelFocus({2}, "Center Vocals"); return; }
    if (source == &sCenterHPF)     { flashChannelFocus({2}, "Center HPF"); return; }

    if (source == &sLFEGain)       { flashChannelFocus({3}, "LFE Gain"); return; }
    if (source == &sLFEHz)         { flashChannelFocus({3}, "LFE Crossover"); return; }
    if (source == &sLFEShelf)      { flashChannelFocus({3}, "Sub Shelf"); return; }
    if (source == &sExciter)       { flashChannelFocus({3}, "Bass Exciter"); return; }

    if (source == &sSurrGain)      { flashChannelFocus({4, 5}, "Surround Level"); return; }
    if (source == &sHaasMs)        { flashChannelFocus({4, 5}, "Haas Delay"); return; }
    if (source == &sSurrHPF)       { flashChannelFocus({4, 5}, "Surround HPF"); return; }
    if (source == &sSideBlend)     { flashChannelFocus({4, 5}, "Side Blend"); return; }
    if (source == &sMidBlend)      { flashChannelFocus({4, 5}, "Center Spill to Surround"); return; }

    if (source == &sReverbWet)     { flashChannelFocus({4, 5}, "Reverb Wet"); return; }
    if (source == &sRoomSize)      { flashChannelFocus({4, 5}, "Room Size"); return; }
    if (source == &sVelvetDens)    { flashChannelFocus({4, 5}, "Decorrelation"); return; }
}

// Applies a full parameter set to UI controls and DSP state.
void MainComponent::applyPreset(const UpmixParams& preset)
{
    suppressSliderCallbacks = true;

    auto set = [](juce::Slider& slider, float value)
    {
        slider.setValue(double(value), juce::sendNotificationSync);
    };

    set(sFrontGain.slider,  preset.frontGain);
    set(sCenterGain.slider, preset.centerGain);
    set(sCenterHPF.slider,  preset.centerHPF);

    set(sLFEGain.slider,  preset.lfeGain);
    set(sLFEHz.slider,    preset.lfeCrossover);
    set(sLFEShelf.slider, preset.lfeShelfGain);
    set(sExciter.slider,  preset.exciterDrive);

    set(sSurrGain.slider,  preset.surroundGain);
    set(sHaasMs.slider,    preset.haasDelayMs);
    set(sSurrHPF.slider,   preset.surroundHPF);
    set(sSideBlend.slider, preset.sideBlend);
    set(sMidBlend.slider,  preset.midBlend);

    set(sReverbWet.slider,  preset.reverbWet);
    set(sRoomSize.slider,   preset.roomSize);
    set(sVelvetDens.slider, preset.velvetDensity);

    suppressSliderCallbacks = false;
    syncParamsFromSliders();
    engine.reset();
    flashChannelFocus({0, 1, 2, 3, 4, 5}, "Preset Apply");
}

// Preset tuned for stronger movie/cinema impact.
void MainComponent::applyPresetCinema()
{
    UpmixParams p;
    p.centerGain    = 1.15f;
    p.centerHPF     = 145.0f;
    p.lfeGain       = 1.60f;
    p.lfeCrossover  = 85.0f;
    p.lfeShelfGain  = 6.0f;
    p.exciterDrive  = 0.72f;
    p.surroundGain  = 0.92f;
    p.haasDelayMs   = 22.0f;
    p.surroundHPF   = 125.0f;
    p.sideBlend     = 0.75f;
    p.midBlend      = 0.24f;
    p.reverbWet     = 0.78f;
    p.roomSize      = 0.86f;
    p.velvetDensity = 1700.0f;
    applyPreset(p);
}

// Preset tuned for wider music imaging with moderate center.
void MainComponent::applyPresetMusicWide()
{
    UpmixParams p;
    p.centerGain    = 0.88f;
    p.centerHPF     = 120.0f;
    p.lfeGain       = 1.15f;
    p.lfeCrossover  = 78.0f;
    p.lfeShelfGain  = 3.0f;
    p.exciterDrive  = 0.38f;
    p.surroundGain  = 1.15f;
    p.haasDelayMs   = 15.0f;
    p.surroundHPF   = 135.0f;
    p.sideBlend     = 1.05f;
    p.midBlend      = 0.10f;
    p.reverbWet     = 0.56f;
    p.roomSize      = 0.72f;
    p.velvetDensity = 2650.0f;
    applyPreset(p);
}

// Preset tuned for vocal clarity and center focus.
void MainComponent::applyPresetVocalFocus()
{
    UpmixParams p;
    p.centerGain    = 1.35f;
    p.centerHPF     = 210.0f;
    p.lfeGain       = 0.90f;
    p.lfeCrossover  = 72.0f;
    p.lfeShelfGain  = 2.0f;
    p.exciterDrive  = 0.24f;
    p.surroundGain  = 0.58f;
    p.haasDelayMs   = 12.0f;
    p.surroundHPF   = 190.0f;
    p.sideBlend     = 0.62f;
    p.midBlend      = 0.30f;
    p.reverbWet     = 0.36f;
    p.roomSize      = 0.56f;
    p.velvetDensity = 1250.0f;
    applyPreset(p);
}

// Restores all parameters to constructor defaults.
void MainComponent::resetToDefaultPreset()
{
    applyPreset(UpmixParams{});
}

// Shows mode + active output channel count + current audio device.
void MainComponent::refreshStatusLabel()
{
    juce::String mode = surround51Active ? "Mode: 5.1 Upmix" : "Mode: Stereo";

    juce::String deviceName = "No Device";
    int activeOutputs = 0;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        deviceName = dev->getName();
        activeOutputs = dev->getActiveOutputChannels().countNumberOfSetBits();
    }

    juce::String speakerInfo = "Speakers: Unknown";
    if (activeOutputs >= 6)      speakerInfo = "Speakers: 3 (L,C,R) + Sub + 2 Surround";
    else if (activeOutputs == 2) speakerInfo = "Speakers: Stereo / Headphones";
    else if (activeOutputs == 1) speakerInfo = "Speakers: Mono";
    else if (activeOutputs > 2)  speakerInfo = "Speakers: Multi-channel (" + juce::String(activeOutputs) + ")";

    statusLabel.setText(mode
                        + "  |  Outputs: " + juce::String(activeOutputs)
                        + "  |  " + speakerInfo
                        + "  |  " + deviceName,
                        juce::dontSendNotification);
}

// Shared timeline formatter (mm:ss or h:mm:ss).
juce::String MainComponent::formatTime(double seconds)
{
    const int total = juce::jmax(0, int(std::floor(seconds + 0.5)));
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;

    if (h > 0)
        return juce::String::formatted("%d:%02d:%02d", h, m, s);

    return juce::String::formatted("%02d:%02d", m, s);
}

// Keeps seek bar and time label synced with transport playback state.
void MainComponent::updateTimelineFromTransport()
{
    if (!fileLoaded)
    {
        timelineSlider.setRange(0.0, 1.0, 0.001);
        timelineSlider.setValue(0.0, juce::dontSendNotification);
        timelineLabel.setText("00:00 / 00:00", juce::dontSendNotification);
        return;
    }

    const double length = juce::jmax(1.0, juce::jmax(trackLengthSeconds, transport.getLengthInSeconds()));
    const double pos = juce::jlimit(0.0, length, transport.getCurrentPosition());

    timelineSlider.setRange(0.0, length, 0.001);
    if (!timelineBeingDragged)
        timelineSlider.setValue(pos, juce::dontSendNotification);

    const double displayPos = timelineBeingDragged ? timelineSlider.getValue() : pos;
    timelineLabel.setText(formatTime(displayPos) + " / " + formatTime(length), juce::dontSendNotification);
}

// Reads the current OS default output name from active JUCE device type.
juce::String MainComponent::queryDefaultOutputDeviceName()
{
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        auto outputs = type->getDeviceNames(false);
        const int defaultOutputIdx = type->getDefaultDeviceIndex(false);
        if (juce::isPositiveAndBelow(defaultOutputIdx, outputs.size()))
            return outputs[defaultOutputIdx];
    }
    return {};
}

// Rebuilds active output-channel mask to match the selected audio hardware.
void MainComponent::configureOutputForCurrentDevice()
{
    juce::ScopedValueSetter<bool> guard(deviceReconfigInProgress, true);

    if (auto* currentDevice = deviceManager.getCurrentAudioDevice())
    {
        lastKnownOutputDeviceName = currentDevice->getName();
        lastKnownDefaultOutputName = queryDefaultOutputDeviceName();

        const int availableOutputs = currentDevice->getOutputChannelNames().size();
        const int requestedOutputs = juce::jlimit(1, 6, availableOutputs);

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange(0, requestedOutputs, true);

        auto err = deviceManager.setAudioDeviceSetup(setup, true);
        if (err.isNotEmpty())
        {
            setup.useDefaultOutputChannels = true;
            setup.outputChannels.clear();
            juce::ignoreUnused(deviceManager.setAudioDeviceSetup(setup, true));
        }
    }

    refreshStatusLabel();
}

// Polls OS default output and switches the app device live when it changes.
void MainComponent::followSystemOutputDevice()
{
    auto defaultOutput = queryDefaultOutputDeviceName();
    if (defaultOutput.isEmpty())
        return;

    auto* currentDevice = deviceManager.getCurrentAudioDevice();
    const auto currentName = currentDevice != nullptr ? currentDevice->getName() : juce::String{};

    // If OS default output changed (e.g. speakers -> headset), switch live without restart.
    if (defaultOutput != currentName && defaultOutput != lastKnownOutputDeviceName)
    {
        juce::ScopedValueSetter<bool> guard(deviceReconfigInProgress, true);

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = defaultOutput;
        setup.useDefaultOutputChannels = true;
        setup.outputChannels.clear();

        auto err = deviceManager.setAudioDeviceSetup(setup, true);
        if (err.isNotEmpty())
        {
            setup.outputDeviceName.clear();
            juce::ignoreUnused(deviceManager.setAudioDeviceSetup(setup, true));
        }

        configureOutputForCurrentDevice();
        channelImpactLabel.setText("Audio device switched -> " + defaultOutput, juce::dontSendNotification);
    }

    lastKnownDefaultOutputName = defaultOutput;
}

// Opens JUCE's built-in audio-device selector dialog.
void MainComponent::openAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        0, 2,   // min/max inputs
        0, 8,   // min/max outputs
        true,
        true,
        true,
        false);

    selector->setSize(640, 460);

    juce::DialogWindow::LaunchOptions opts;
    opts.dialogTitle = "Audio Settings";
    opts.dialogBackgroundColour = COL_BG1;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.componentToCentreAround = this;
    opts.content.setOwned(selector);
    opts.launchAsync();

    refreshStatusLabel();
}

// Opens source file chooser and attaches selected file to transport playback.
void MainComponent::openFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Open audio file", juce::File{},
        "*.wav;*.mp3;*.flac;*.aiff;*.ogg;*.aac");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto results = fc.getResults();
            if (results.isEmpty()) return;

            auto f = results[0];
            auto* reader = formatManager.createReaderFor(f);
            if (reader)
            {
                trackLengthSeconds = reader->sampleRate > 0.0
                    ? (double(reader->lengthInSamples) / reader->sampleRate)
                    : 0.0;

                auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
                transport.stop();
                transport.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
                readerSource = std::move(newSource);
                loadedFile = f;
                fileLabel.setText(f.getFileName(), juce::dontSendNotification);
                fileLoaded = true;
                transport.start();
                setPlayState(true);
                refreshStatusLabel();
                updateTimelineFromTransport();
            }
            else
            {
                fileLabel.setText("Could not open: " + f.getFileName(), juce::dontSendNotification);
            }
        });
}

// Offline export path:
// reads the currently loaded source file and writes a 6-channel WAV using current slider settings.
void MainComponent::exportCurrentTrackTo51Wav()
{
    if (!fileLoaded || !loadedFile.existsAsFile())
    {
        channelImpactLabel.setText("Export failed: load a source file first.", juce::dontSendNotification);
        return;
    }

    // Snapshot the latest UI values now, so export matches what the user sees.
    syncParamsFromSliders();
    const UpmixParams paramsSnapshot = engine.params;
    const float masterGainSnapshot = float(masterVol.getValue());

    auto suggested = loadedFile.getSiblingFile(loadedFile.getFileNameWithoutExtension() + "_upmix_5_1.wav");

    exportChooser = std::make_unique<juce::FileChooser>(
        "Export current settings to 5.1 WAV",
        suggested,
        "*.wav");

    channelImpactLabel.setText("Choose destination for 5.1 export...", juce::dontSendNotification);

    exportChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, paramsSnapshot, masterGainSnapshot](const juce::FileChooser& chooser)
        {
            auto target = chooser.getResult();
            if (target == juce::File{})
                return;

            channelImpactLabel.setText("Exporting 5.1 WAV... please wait.", juce::dontSendNotification);

            if (!target.hasFileExtension("wav"))
                target = target.withFileExtension(".wav");

            auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(loadedFile));
            if (!reader)
            {
                channelImpactLabel.setText("Export failed: cannot read source file.", juce::dontSendNotification);
                return;
            }

            if (target.existsAsFile() && !target.deleteFile())
            {
                channelImpactLabel.setText("Export failed: cannot overwrite target file.", juce::dontSendNotification);
                return;
            }

            auto fileOut = target.createOutputStream();
            if (!fileOut || !fileOut->openedOk())
            {
                channelImpactLabel.setText("Export failed: cannot create output file.", juce::dontSendNotification);
                return;
            }
            std::unique_ptr<juce::OutputStream> outStream(std::move(fileOut));

            juce::WavAudioFormat wav;
            const auto writeOptions = juce::AudioFormatWriterOptions{}
                .withSampleRate(reader->sampleRate)
                .withChannelLayout(juce::AudioChannelSet::create5point1())
                .withBitsPerSample(24);

            auto writer = wav.createWriterFor(outStream, writeOptions);
            if (!writer)
            {
                channelImpactLabel.setText("Export failed: WAV writer init error.", juce::dontSendNotification);
                return;
            }

            constexpr int exportBlockSize = 2048;
            const int64 totalSamples = reader->lengthInSamples;

            UpmixEngine exportEngine;
            exportEngine.params = paramsSnapshot;
            exportEngine.prepare(reader->sampleRate, exportBlockSize);

            juce::AudioBuffer<float> stereoBlock(2, exportBlockSize);
            juce::AudioBuffer<float> surroundBlock(6, exportBlockSize);

            bool failed = false;
            for (int64 readPos = 0; readPos < totalSamples; readPos += exportBlockSize)
            {
                const int numThisBlock = int(juce::jmin<int64>(exportBlockSize, totalSamples - readPos));

                stereoBlock.clear();
                surroundBlock.clear();

                // Read as stereo (left/right); mono files will still map correctly.
                if (!reader->read(&stereoBlock, 0, numThisBlock, readPos, true, true))
                {
                    failed = true;
                    break;
                }

                // Always export as 5.1 upmix, using the parameter snapshot taken at export start.
                exportEngine.process(stereoBlock, surroundBlock, numThisBlock, true);

                if (masterGainSnapshot != 1.0f)
                    surroundBlock.applyGain(0, numThisBlock, masterGainSnapshot);

                if (!writer->writeFromAudioSampleBuffer(surroundBlock, 0, numThisBlock))
                {
                    failed = true;
                    break;
                }
            }

            writer.reset();

            if (failed)
            {
                target.deleteFile();
                channelImpactLabel.setText("Export failed during render.", juce::dontSendNotification);
                return;
            }

            channelImpactLabel.setText("Export complete: " + target.getFileName(), juce::dontSendNotification);
        });
}

// Updates play button text + callback so one button handles Play/Pause.
void MainComponent::setPlayState(bool playing)
{
    playBtn.setButtonText(playing ? "Pause" : "Play");
    playBtn.onClick = [this, playing] {
        if (playing) { transport.stop(); setPlayState(false); }
        else if (fileLoaded)
        {
            transport.start();
            setPlayState(true);
        }
    };
}

// Draws the main panel, background gradients, and top banner text.
void MainComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Clean dark background.
    juce::ColourGradient baseBg(COL_BG0, bounds.getX(), bounds.getY(),
                                COL_BG1, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(baseBg);
    g.fillRect(bounds);

    auto shell = bounds.reduced(8.0f);
    g.setColour(COL_PANEL.withAlpha(0.95f));
    g.fillRoundedRectangle(shell, 10.0f);
    g.setColour(COL_BRDR.withAlpha(0.62f));
    g.drawRoundedRectangle(shell, 10.0f, 1.0f);

    auto headerBand = juce::Rectangle<float>(shell.getX() + 8.0f, shell.getY() + 6.0f,
                                             shell.getWidth() - 16.0f, 44.0f);
    g.setColour(COL_BG2.withAlpha(0.82f));
    g.fillRoundedRectangle(headerBand, 7.0f);
    g.setColour(COL_BRDR.withAlpha(0.48f));
    g.drawRoundedRectangle(headerBand, 7.0f, 0.9f);

    auto titleArea = juce::Rectangle<int>(int(headerBand.getX() + 12.0f), int(headerBand.getY() + 2.0f),
                                          int(headerBand.getWidth() * 0.65f), 24);
    g.setColour(juce::Colours::white.withAlpha(0.96f));
    g.setFont(juce::Font(juce::FontOptions("Segoe UI Semibold", fs(20.0f), juce::Font::bold)));
    g.drawText("Surround 5.1 Upmixer", titleArea, juce::Justification::centredLeft);

    auto subtitleArea = juce::Rectangle<int>(titleArea.getX(), titleArea.getBottom() - 2,
                                             int(headerBand.getWidth() * 0.70f), 14);
    g.setColour(COL_MUT.withAlpha(0.92f));
    g.setFont(juce::Font(juce::FontOptions("Segoe UI", fs(10.0f), juce::Font::plain)));
    g.drawText("Discrete hardware output   |   Psychoacoustic upmix   |   Live tuning",
               subtitleArea, juce::Justification::centredLeft);

    auto signatureArea = juce::Rectangle<int>(int(headerBand.getRight()) - 290, int(headerBand.getY() + 6), 278, 22);
    g.setColour(COL_MUT.withAlpha(0.88f));
    g.setFont(juce::Font(juce::FontOptions("Segoe UI", fs(10.0f), juce::Font::plain)));
    g.drawText("by Mohamed Moslem Allouch", signatureArea, juce::Justification::centredRight);

    auto drawSection = [&](juce::Rectangle<int> r)
    {
        if (r.isEmpty())
            return;

        auto b = r.expanded(3, 3).toFloat();
        g.setColour(COL_BG1.withAlpha(0.78f));
        g.fillRoundedRectangle(b, 8.5f);
        g.setColour(COL_BRDR.withAlpha(0.56f));
        g.drawRoundedRectangle(b, 8.5f, 0.9f);
    };

    drawSection(fileBtn.getBounds().getUnion(exportBtn.getBounds())
                .getUnion(settingsBtn.getBounds())
                .getUnion(fileLabel.getBounds())
                .getUnion(playBtn.getBounds())
                .getUnion(stopBtn.getBounds()));
    drawSection(btnStereo.getBounds().getUnion(btn51.getBounds()));
    drawSection(statusLabel.getBounds());
    drawSection(presetLabel.getBounds().getUnion(presetCinema.getBounds())
                .getUnion(presetMusic.getBounds())
                .getUnion(presetVocal.getBounds())
                .getUnion(presetReset.getBounds()));
    drawSection(channelImpactLabel.getBounds());
    drawSection(timelineSlider.getBounds().getUnion(timelineLabel.getBounds()));
    drawSection(spectrum.getBounds());
    drawSection(masterVolLabel.getBounds().getUnion(masterVol.getBounds()));

    auto meterBounds = mFL.getBounds().getUnion(mFR.getBounds())
        .getUnion(mFC.getBounds()).getUnion(mLFE.getBounds())
        .getUnion(mSL.getBounds()).getUnion(mSR.getBounds());
    drawSection(meterBounds);

    drawSection(paramsViewport.getBounds());
    drawSection(channelScope.getBounds());
}

// Lays out all controls, meters, visualizers, and scrollable parameter panels.
void MainComponent::resized()
{
    constexpr int gap = 8;
    auto root = getLocalBounds().reduced(12);

    // Reserved for title/subtitle text drawn in paint().
    root.removeFromTop(62);
    root.removeFromTop(gap);

    const int scopeH = juce::jlimit(62, 92, getHeight() / 9);
    auto scopeArea = root.removeFromBottom(scopeH);
    channelScope.setBounds(scopeArea);
    root.removeFromBottom(gap);

    const int minTop = 250;
    const int minParams = 220;
    const int maxTop = juce::jmax(minTop, root.getHeight() - minParams - gap);
    const int topH = juce::jlimit(minTop, maxTop, int(float(root.getHeight()) * 0.52f));
    auto top = root.removeFromTop(topH);
    root.removeFromTop(gap);

    auto fileRow = top.removeFromTop(34);
    fileBtn.setBounds(fileRow.removeFromLeft(126));
    fileRow.removeFromLeft(gap);
    exportBtn.setBounds(fileRow.removeFromLeft(148));
    fileRow.removeFromLeft(gap);
    settingsBtn.setBounds(fileRow.removeFromLeft(136));
    fileRow.removeFromLeft(gap);
    playBtn.setBounds(fileRow.removeFromRight(86));
    fileRow.removeFromRight(gap);
    stopBtn.setBounds(fileRow.removeFromRight(78));
    fileRow.removeFromRight(gap);
    fileLabel.setBounds(fileRow);

    top.removeFromTop(gap);
    auto modeRow = top.removeFromTop(54);
    auto leftMode = modeRow.removeFromLeft((modeRow.getWidth() - gap) / 2);
    modeRow.removeFromLeft(gap);
    btnStereo.setBounds(leftMode);
    btn51.setBounds(modeRow);

    top.removeFromTop(gap);
    statusLabel.setBounds(top.removeFromTop(24));

    top.removeFromTop(gap);
    presetLabel.setBounds(top.removeFromTop(18));
    top.removeFromTop(4);
    auto presetRow = top.removeFromTop(30);
    const int presetW = (presetRow.getWidth() - 3 * gap) / 4;
    presetCinema.setBounds(presetRow.removeFromLeft(presetW));
    presetRow.removeFromLeft(gap);
    presetMusic.setBounds(presetRow.removeFromLeft(presetW));
    presetRow.removeFromLeft(gap);
    presetVocal.setBounds(presetRow.removeFromLeft(presetW));
    presetRow.removeFromLeft(gap);
    presetReset.setBounds(presetRow);

    top.removeFromTop(gap);
    channelImpactLabel.setBounds(top.removeFromTop(22));

    top.removeFromTop(gap);
    auto timelineRow = top.removeFromTop(22);
    timelineLabel.setBounds(timelineRow.removeFromRight(120));
    timelineRow.removeFromRight(gap);
    timelineSlider.setBounds(timelineRow);

    top.removeFromTop(gap);
    auto meterRow = top.removeFromBottom(juce::jlimit(52, 78, top.getHeight() / 3));
    top.removeFromBottom(gap);
    auto volRow = top.removeFromBottom(22);
    top.removeFromBottom(gap);
    spectrum.setBounds(top);

    masterVolLabel.setBounds(volRow.removeFromLeft(110));
    volRow.removeFromLeft(gap);
    masterVol.setBounds(volRow);

    const int meterGap = 6;
    const int meterW = (meterRow.getWidth() - meterGap * 5) / 6;
    mFL.setBounds(meterRow.removeFromLeft(meterW));
    meterRow.removeFromLeft(meterGap);
    mFR.setBounds(meterRow.removeFromLeft(meterW));
    meterRow.removeFromLeft(meterGap);
    mFC.setBounds(meterRow.removeFromLeft(meterW));
    meterRow.removeFromLeft(meterGap);
    mLFE.setBounds(meterRow.removeFromLeft(meterW));
    meterRow.removeFromLeft(meterGap);
    mSL.setBounds(meterRow.removeFromLeft(meterW));
    meterRow.removeFromLeft(meterGap);
    mSR.setBounds(meterRow);

    paramsViewport.setBounds(root);

    const int scrollThickness = paramsViewport.getScrollBarThickness();
    const int contentWidth = juce::jmax(520, paramsViewport.getWidth() - scrollThickness - 6);
    const int xPad = 8;
    const int colGap = 12;
    const int sectionGap = 10;
    const int sectionPad = 10;
    const int titleH = 18;
    const int sliderH = 31;
    const int sliderGap = 6;
    const bool oneColumn = contentWidth < 760;
    const int usableWidth = contentWidth - xPad * 2;
    const int colWidth = oneColumn ? usableWidth : (usableWidth - colGap) / 2;
    int y = xPad;

    auto placeSection = [&](int x, int yPos, juce::Label& title, std::initializer_list<ParamSlider*> sliders)
    {
        int yy = yPos + sectionPad - 2;
        title.setBounds(x + sectionPad, yy, colWidth - sectionPad * 2, titleH);
        yy += titleH + 7;

        for (auto* slider : sliders)
        {
            slider->setBounds(x + sectionPad, yy, colWidth - sectionPad * 2, sliderH);
            yy += sliderH + sliderGap;
        }
        yy += sectionPad - sliderGap;
        return juce::Rectangle<int>(x, yPos, colWidth, yy - yPos);
    };

    juce::Rectangle<int> frontCard;
    juce::Rectangle<int> lfeCard;
    juce::Rectangle<int> surroundCard;
    juce::Rectangle<int> spaceCard;

    if (oneColumn)
    {
        frontCard = placeSection(xPad, y, lblFront, { &sFrontGain, &sCenterGain, &sCenterHPF });
        y = frontCard.getBottom() + sectionGap;
        lfeCard = placeSection(xPad, y, lblLFE, { &sLFEGain, &sLFEHz, &sLFEShelf, &sExciter });
        y = lfeCard.getBottom() + sectionGap;
        surroundCard = placeSection(xPad, y, lblSurround, { &sSurrGain, &sHaasMs, &sSurrHPF, &sSideBlend, &sMidBlend });
        y = surroundCard.getBottom() + sectionGap;
        spaceCard = placeSection(xPad, y, lblSpace, { &sReverbWet, &sRoomSize, &sVelvetDens });
        y = spaceCard.getBottom() + xPad;
    }
    else
    {
        const int rightX = xPad + colWidth + colGap;
        frontCard = placeSection(xPad, y, lblFront, { &sFrontGain, &sCenterGain, &sCenterHPF });
        lfeCard = placeSection(rightX, y, lblLFE, { &sLFEGain, &sLFEHz, &sLFEShelf, &sExciter });
        y = juce::jmax(frontCard.getBottom(), lfeCard.getBottom()) + sectionGap;

        surroundCard = placeSection(xPad, y, lblSurround, { &sSurrGain, &sHaasMs, &sSurrHPF, &sSideBlend, &sMidBlend });
        spaceCard = placeSection(rightX, y, lblSpace, { &sReverbWet, &sRoomSize, &sVelvetDens });
        y = juce::jmax(surroundCard.getBottom(), spaceCard.getBottom()) + xPad;
    }

    paramsContent.setCards(frontCard, lfeCard, surroundCard, spaceCard);
    paramsContent.setSize(contentWidth, y);
}
