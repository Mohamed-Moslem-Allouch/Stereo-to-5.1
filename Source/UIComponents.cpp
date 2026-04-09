#include "UIComponents.h"
#include "ThemeColours.h"

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
    setColour(juce::TextButton::buttonColourId,     COL_CARD);
    setColour(juce::TextButton::textColourOffId,    COL_TXT);
    setColour(juce::ComboBox::backgroundColourId,   COL_BG2);
    setColour(juce::ScrollBar::thumbColourId,       COL_BG3.brighter(0.2f));
    setColour(juce::ScrollBar::trackColourId,       COL_BG2);
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
    const float trackH = 3.5f;
    const float trackY = centreY - trackH * 0.5f;
    auto trackBounds = juce::Rectangle<float>(float(x), trackY, float(w), trackH);

    // Track background
    g.setColour(COL_BG3.withAlpha(0.90f));
    g.fillRoundedRectangle(trackBounds, trackH * 0.5f);

    // Filled region
    float fillW = juce::jlimit(0.0f, trackBounds.getWidth(), sliderPos - float(x));
    if (fillW > 0.0f)
    {
        auto fill = trackBounds.withWidth(fillW);
        // Subtle glow beneath the fill
        g.setColour(accent.withAlpha(0.18f));
        g.fillRoundedRectangle(fill.expanded(0.f, 2.5f), 3.5f);
        // Main gradient fill
        juce::ColourGradient fg(accent.withAlpha(0.55f), fill.getX(), fill.getY(),
                                accent.withAlpha(0.95f), fill.getRight(), fill.getBottom(), false);
        g.setGradientFill(fg);
        g.fillRoundedRectangle(fill, trackH * 0.5f);
    }

    // Thumb: outer glow ring
    const float thumbR = 7.5f;
    g.setColour(accent.withAlpha(0.22f));
    g.fillEllipse(sliderPos - thumbR - 2.0f, centreY - thumbR - 2.0f,
                  (thumbR + 2.0f) * 2.0f, (thumbR + 2.0f) * 2.0f);
    // Thumb body
    g.setColour(accent);
    g.fillEllipse(sliderPos - thumbR, centreY - thumbR, thumbR * 2.0f, thumbR * 2.0f);
    // Thumb inner highlight
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.fillEllipse(sliderPos - 2.5f, centreY - 2.5f, 5.0f, 5.0f);
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
    nameLabel.setColour(juce::Label::textColourId, COL_TXT.withAlpha(0.80f));
    nameLabel.setFont(juce::Font(juce::FontOptions("Consolas", 12.5f, juce::Font::plain)));

    // Value label styled as a colored badge
    valueLabel.setColour(juce::Label::textColourId, accent.brighter(0.25f));
    valueLabel.setColour(juce::Label::backgroundColourId, accent.withAlpha(0.10f));
    valueLabel.setColour(juce::Label::outlineColourId, accent.withAlpha(0.40f));
    valueLabel.setFont(juce::Font(juce::FontOptions("Consolas", 12.5f, juce::Font::bold)));
    valueLabel.setJustificationType(juce::Justification::centred);
    valueLabel.setBorderSize({ 2, 6, 2, 6 });

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
    // Reserve less space for labels so the slider lane has enough height
    // to render the same thumb style as the master level control.
    const int topH = juce::jlimit(16, 18, int(float(getHeight()) * 0.40f));
    auto top = b.removeFromTop(topH);
    // Value badge: fixed width on right, visually balanced
    const int valueW = juce::jlimit(68, 106, b.getWidth() / 4);
    valueLabel.setBounds(top.removeFromRight(valueW).reduced(0, 1));
    top.removeFromRight(4);
    nameLabel.setBounds(top);
    slider.setBounds(b.reduced(0, juce::jmax(1, topH / 7)));
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

    const auto title    = getButtonText().upToFirstOccurrenceOf("\n", false, false);
    const auto subtitle = getButtonText().fromFirstOccurrenceOf("\n", false, false).trim();

    // Card background
    juce::ColourGradient grad(
        active ? colour.withAlpha(0.13f) : COL_BG2.withAlpha(0.80f), b.getX(), b.getY(),
        active ? colour.withAlpha(0.06f) : COL_BG2.withAlpha(0.55f), b.getX(), b.getBottom(), false);
    if (hover && !active)
        grad = juce::ColourGradient(COL_BG2.brighter(0.06f), b.getX(), b.getY(),
                                    COL_BG2.withAlpha(0.65f),  b.getX(), b.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(b, 9.0f);

    // Border: glowing cyan when active
    if (active)
    {
        g.setColour(colour.withAlpha(0.30f));
        g.drawRoundedRectangle(b.expanded(1.0f), 10.0f, 2.0f);
    }
    g.setColour(active ? colour.withAlpha(0.90f) : COL_BRDR.withAlpha(0.70f));
    g.drawRoundedRectangle(b, 9.0f, active ? 1.5f : 1.0f);

    // Inner sheen
    auto sheen = b.reduced(1.0f).withHeight(b.getHeight() * 0.45f);
    g.setColour(juce::Colours::white.withAlpha(active ? 0.055f : 0.035f));
    g.fillRoundedRectangle(sheen, 8.0f);

    auto content = b.reduced(14.f, 8.f);

    // Dot indicator (left side)
    const float dotR = 4.5f;
    const float dotX = content.getX();
    const float dotY = content.getCentreY();
    if (active)
    {
        g.setColour(colour.withAlpha(0.25f));
        g.fillEllipse(dotX - dotR * 1.6f, dotY - dotR * 1.6f, dotR * 3.2f, dotR * 3.2f);
    }
    g.setColour(colour.withAlpha(active ? 1.0f : 0.35f));
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);

    content.removeFromLeft(int(dotR * 2.0f + 8.0f));

    // Badge on the right (2.0 / 5.1)
    const bool isStereoBtn = title.containsIgnoreCase("stereo");
    const juce::String badgeText = isStereoBtn ? "2.0" : "5.1";
    const int badgeW = 38;
    auto badge = content.removeFromRight(badgeW).reduced(0.f, 4.0f);
    g.setColour(COL_BG0.withAlpha(0.70f));
    g.fillRoundedRectangle(badge, 6.0f);
    g.setColour(active ? colour.withAlpha(0.85f) : COL_BRDR.withAlpha(0.65f));
    g.drawRoundedRectangle(badge, 6.0f, 1.0f);
    g.setFont(juce::Font(juce::FontOptions("Consolas", 11.5f, juce::Font::bold)));
    g.setColour(active ? colour.brighter(0.15f) : COL_MUT.brighter(0.25f));
    g.drawFittedText(badgeText, badge.toNearestInt(), juce::Justification::centred, 1);

    content.removeFromRight(6);

    // Title
    auto titleRow = content.removeFromTop(content.getHeight() * 0.56f);
    g.setColour(juce::Colours::white.withAlpha(active ? 1.0f : 0.82f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 13.5f, juce::Font::bold)));
    g.drawFittedText(title, titleRow.toNearestInt(), juce::Justification::centredLeft, 1);

    // Subtitle
    g.setColour(COL_MUT.brighter(active ? 0.30f : 0.10f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 10.0f, juce::Font::plain)));
    g.drawFittedText(subtitle, content.toNearestInt(), juce::Justification::centredLeft, 1);
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
    const int labelH = 16;
    auto barArea = b.reduced(2.f).withTrimmedBottom(float(labelH));

    // Track background
    g.setColour(COL_BG3.withAlpha(0.75f));
    g.fillRoundedRectangle(barArea, 3.f);

    float fillH = juce::jlimit(0.f, 1.f, level * 4.f);
    float fillTop = barArea.getBottom() - fillH * barArea.getHeight();
    auto fillArea = barArea.withTop(fillTop);

    if (fillH > 0.001f)
    {
        // Gradient: cyan at bottom fades to white-cyan at top for high levels
        juce::ColourGradient fillGrad(
            col.withAlpha(0.88f),  fillArea.getX(), fillArea.getBottom(),
            col.brighter(0.35f).withAlpha(0.95f), fillArea.getX(), fillArea.getY(), false);
        if (fillH > 0.75f)
            fillGrad = juce::ColourGradient(
                col.withAlpha(0.88f),                  fillArea.getX(), fillArea.getBottom(),
                juce::Colour(0xffff4444).withAlpha(0.90f), fillArea.getX(), fillArea.getY(), false);
        g.setGradientFill(fillGrad);
        g.fillRoundedRectangle(fillArea, 3.f);
    }

    // Subtle tick marks
    g.setColour(COL_BG0.withAlpha(0.55f));
    const int numTicks = 5;
    for (int i = 1; i < numTicks; ++i)
    {
        float ty = barArea.getY() + barArea.getHeight() * (float(i) / float(numTicks));
        g.drawHorizontalLine(int(ty), barArea.getX(), barArea.getRight());
    }

    // Focus glow
    if (focus > 0.001f)
    {
        g.setColour(col.withAlpha(0.30f * focus));
        g.drawRoundedRectangle(barArea.expanded(2.0f), 5.f, 2.0f);
    }

    // Peak marker
    float pkFrac = juce::jlimit(0.f, 1.f, peak * 4.f);
    if (pkFrac > 0.01f)
    {
        float pkY = barArea.getBottom() - pkFrac * barArea.getHeight();
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.fillRect(barArea.getX() + 1.f, pkY - 0.8f, barArea.getWidth() - 2.f, 1.6f);
    }

    // Label
    g.setColour(col.withAlpha(0.85f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 10.f, juce::Font::bold)));
    g.drawText(lbl, b.withTop(b.getBottom() - float(labelH)).toNearestInt(),
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

    // Background
    juce::ColourGradient bg(COL_BG2.withAlpha(0.95f), b.getX(), b.getY(),
                            COL_BG0.withAlpha(0.98f), b.getX(), b.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRoundedRectangle(b, 7.0f);

    auto plot = b.reduced(8.0f, 7.0f);
    const float plotTop = plot.getY() + 4.0f;
    const float plotBottom = plot.getBottom() - 2.0f;

    // Horizontal dB grid lines
    const char* dbLabels[] = { "0", "-6", "-12", "-18", "-24" };
    for (int i = 0; i <= 4; ++i)
    {
        float y = juce::jmap(float(i), 0.0f, 4.0f, plotTop, plotBottom);
        g.setColour(COL_BG3.withAlpha(i == 0 ? 0.55f : 0.40f));
        g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
        g.setColour(COL_MUT.withAlpha(0.50f));
        g.setFont(juce::Font(juce::FontOptions("Consolas", 8.5f, juce::Font::plain)));
        g.drawText(dbLabels[i],
                   juce::Rectangle<int>(int(plot.getRight()) - 26, int(y) - 6, 24, 12),
                   juce::Justification::centredRight);
    }

    juce::Path line, fill, peakLine;
    const int n = (int) std::size(scopeData);
    const juce::Colour accent = isSurround ? COL_ORG : COL_ACC;

    for (int i = 0; i < n; ++i)
    {
        float x = juce::jmap(float(i), 0.0f, float(n - 1), plot.getX(), plot.getRight());
        float y = juce::jmap(scopeSmooth[i], 0.0f, 1.0f, plotBottom, plotTop);
        float yPeak = juce::jmap(scopePeak[i], 0.0f, 1.0f, plotBottom, plotTop);

        if (i == 0)
        {
            line.startNewSubPath(x, y);
            peakLine.startNewSubPath(x, yPeak);
            fill.startNewSubPath(x, plotBottom);
            fill.lineTo(x, y);
        }
        else
        {
            line.lineTo(x, y);
            peakLine.lineTo(x, yPeak);
            fill.lineTo(x, y);
        }
    }

    fill.lineTo(plot.getRight(), plotBottom);
    fill.closeSubPath();

    // Filled area gradient
    juce::ColourGradient fillGrad(
        accent.withAlpha(0.14f), plot.getX(), plotTop,
        accent.withAlpha(0.03f), plot.getX(), plotBottom, false);
    g.setGradientFill(fillGrad);
    g.fillPath(fill);

    // Glow stroke
    g.setColour(accent.withAlpha(0.14f));
    g.strokePath(line, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Main stroke
    g.setColour(accent.withAlpha(0.92f));
    g.strokePath(line, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Peak trace
    g.setColour(juce::Colours::white.withAlpha(0.34f));
    g.strokePath(peakLine, juce::PathStrokeType(0.9f));

    // Frequency labels at bottom
    const char* freqLabels[] = { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    const float freqPositions[] = { 0.02f, 0.06f, 0.12f, 0.22f, 0.34f, 0.46f, 0.58f, 0.72f, 0.84f, 0.97f };
    g.setColour(COL_MUT.withAlpha(0.45f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 8.0f, juce::Font::plain)));
    for (int i = 0; i < 10; ++i)
    {
        float lx = plot.getX() + freqPositions[i] * plot.getWidth();
        g.drawVerticalLine(int(lx), plotBottom - 3.0f, plotBottom);
        g.drawText(freqLabels[i],
                   juce::Rectangle<int>(int(lx) - 12, int(plotBottom) - 14, 24, 10),
                   juce::Justification::centred);
    }

    // Mode label top-right
    g.setColour(isSurround ? COL_ORG.withAlpha(0.60f) : COL_ACC.withAlpha(0.60f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 9.0f, juce::Font::bold)));
    g.drawText(isSurround ? "5.1" : "2.0",
               juce::Rectangle<int>(int(plot.getRight()) - 26, int(plotTop), 24, 14),
               juce::Justification::centredRight);
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
        juce::Colour(0xff00c8ff), juce::Colour(0xff00c8ff), juce::Colour(0xff3fd8a0),
        juce::Colour(0xffffc840), juce::Colour(0xffff7d30), juce::Colour(0xffff7d30)
    };

    auto b = getLocalBounds().toFloat();

    // Panel background
    juce::ColourGradient panelBg(COL_BG2.withAlpha(0.92f), b.getX(), b.getY(),
                                  COL_BG0.withAlpha(0.96f), b.getX(), b.getBottom(), false);
    g.setGradientFill(panelBg);
    g.fillRoundedRectangle(b, 7.f);
    g.setColour(COL_BRDR.withAlpha(0.55f));
    g.drawRoundedRectangle(b, 7.f, 1.0f);
    auto lanes = b.reduced(5.0f, 3.0f);
    const float laneH = lanes.getHeight() / 6.0f;
    const bool compact = b.getHeight() < 74.0f;

    for (int ch = 0; ch < 6; ++ch)
    {
        auto lane = lanes.removeFromTop(laneH).reduced(0.0f, compact ? 0.35f : 0.8f);
        const auto c = cols[ch];
        const bool active = latest[ch] > 0.02f;

        // Lane background
        g.setColour(COL_BG3.withAlpha(active ? 0.55f : 0.38f));
        g.fillRoundedRectangle(lane, compact ? 2.3f : 3.0f);

        // Compact zones so this component stays useful at smaller heights.
        auto leftLabel = lane.removeFromLeft(compact ? 30.0f : 38.0f);
        auto rightInfo = lane.removeFromRight(compact ? 44.0f : 52.0f);
        auto plot = lane.reduced(compact ? 2.0f : 3.0f, compact ? 1.0f : 1.5f);

        // Waveform path
        juce::Path p;
        juce::Path fillP;
        for (int i = 0; i < historySize; ++i)
        {
            const float x = juce::jmap(float(i), 0.0f, float(historySize - 1), plot.getX(), plot.getRight());
            const float y = juce::jmap(history[ch][i], 0.0f, 1.0f, plot.getBottom(), plot.getY());
            if (i == 0)
            {
                p.startNewSubPath(x, y);
                fillP.startNewSubPath(x, plot.getBottom());
                fillP.lineTo(x, y);
            }
            else
            {
                p.lineTo(x, y);
                fillP.lineTo(x, y);
            }
        }
        fillP.lineTo(plot.getRight(), plot.getBottom());
        fillP.closeSubPath();

        // Fill under waveform
        g.setColour(c.withAlpha(active ? 0.10f : 0.04f));
        g.fillPath(fillP);

        // Waveform stroke
        g.setColour(c.withAlpha(active ? 0.88f : 0.28f));
        g.strokePath(p, juce::PathStrokeType(active ? (compact ? 1.3f : 1.6f) : 1.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

        // Channel label
        g.setColour(c.withAlpha(active ? 0.95f : 0.40f));
        g.setFont(juce::Font(juce::FontOptions("Consolas", compact ? 9.0f : 10.0f, juce::Font::bold)));
        g.drawText(labels[ch], leftLabel.toNearestInt(), juce::Justification::centred);

        // Status badge
        if (active)
        {
            g.setColour(c.withAlpha(0.15f));
            g.fillRoundedRectangle(rightInfo.reduced(compact ? 3.0f : 4.0f, compact ? 1.5f : 2.0f), compact ? 2.0f : 3.0f);
            g.setColour(c.withAlpha(0.80f));
        }
        else
        {
            g.setColour(COL_MUT.withAlpha(0.40f));
        }

        g.setFont(juce::Font(juce::FontOptions("Consolas", compact ? 7.6f : 8.5f, juce::Font::bold)));
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
                             const juce::Rectangle<int>& space,
                             const juce::Rectangle<int>& calib)
{
    frontCard = front;
    lfeCard = lfe;
    surroundCard = surround;
    spaceCard = space;
    calibCard = calib;
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

        // Drop shadow
        g.setColour(juce::Colours::black.withAlpha(0.28f));
        g.fillRoundedRectangle(b.translated(0.0f, 2.5f).expanded(0.5f), 11.0f);

        // Card fill gradient
        juce::ColourGradient bg(
            COL_CARD.withAlpha(0.94f), b.getX(), b.getY(),
            COL_BG0.withAlpha(0.96f),  b.getX(), b.getBottom(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(b, 11.0f);

        // Accent left-edge bar
        auto leftBar = b.withWidth(3.0f).reduced(0.0f, 14.0f);
        g.setColour(accent.withAlpha(0.70f));
        g.fillRoundedRectangle(leftBar, 1.5f);

        // Top sheen
        auto sheen = b.reduced(1.5f).withHeight(b.getHeight() * 0.42f);
        juce::ColourGradient sheenGrad(
            juce::Colours::white.withAlpha(0.048f), sheen.getX(), sheen.getY(),
            juce::Colours::transparentBlack,         sheen.getX(), sheen.getBottom(), false);
        g.setGradientFill(sheenGrad);
        g.fillRoundedRectangle(sheen, 10.0f);

        // Outer border
        g.setColour(COL_BRDR.withAlpha(0.65f));
        g.drawRoundedRectangle(b, 11.0f, 1.0f);
        // Inner accent border (subtle)
        g.setColour(accent.withAlpha(0.16f));
        g.drawRoundedRectangle(b.reduced(1.0f), 10.0f, 0.8f);

        // Title band at top with faint accent tint
        auto titleBand = b.withHeight(24.0f);
        g.setColour(accent.withAlpha(0.12f));
        g.fillRoundedRectangle(titleBand, 10.0f);
        // Separator line under title
        g.setColour(accent.withAlpha(0.28f));
        g.drawLine(titleBand.getX() + 10.0f, titleBand.getBottom(),
                   titleBand.getRight() - 10.0f, titleBand.getBottom(), 0.8f);
    };

    drawCard(frontCard,    COL_ACC);
    drawCard(lfeCard,      COL_YLW);
    drawCard(surroundCard, COL_ORG);
    drawCard(spaceCard,    COL_PRP);
    drawCard(calibCard,    juce::Colour(0xff66d0ff));
}







