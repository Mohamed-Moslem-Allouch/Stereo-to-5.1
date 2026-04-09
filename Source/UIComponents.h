#pragma once
#include <JuceHeader.h>

//==============================================================================
//  Custom look and feel for the app UI.
//==============================================================================
class StudioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    StudioLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    juce::Label* createSliderTextBox(juce::Slider&) override;
};

//==============================================================================
//  LABELLED SLIDER COMPONENT
//==============================================================================
class ParamSlider : public juce::Component
{
public:
    ParamSlider(const juce::String& label, float minVal, float maxVal,
                float defaultVal, const juce::String& unit,
                juce::Colour accent = juce::Colour(0xff00cfff));

    void resized() override;

    juce::Slider slider;
    float* target = nullptr;  // kept for compatibility; unused

    std::function<void(float)> onChange;

    juce::String unitStr;
    juce::Colour accentCol;

private:
    juce::Label nameLabel;
    juce::Label valueLabel;
    float minV, maxV;
    juce::String labelStr;

    void sliderChanged();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamSlider)
};

//==============================================================================
//  MODE TOGGLE BUTTON
//==============================================================================
class ModeButton : public juce::Button
{
public:
    ModeButton(const juce::String& label, juce::Colour col);
    void paintButton(juce::Graphics&, bool, bool) override;

    juce::Colour colour;
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModeButton)
};

//==============================================================================
//  CHANNEL METER
//==============================================================================
class ChannelMeter : public juce::Component, private juce::Timer
{
public:
    ChannelMeter(const juce::String& label, juce::Colour col);
    void push(float rms);
    void setEffectFocus(float amount);
    void paint(juce::Graphics&) override;
    void timerCallback() override;
    void resized() override;

private:
    juce::String lbl;
    juce::Colour col;
    float level = 0.f;
    float peak  = 0.f;
    float focus = 0.f;
    int   peakHoldMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelMeter)
};

//==============================================================================
//  SPECTRUM DISPLAY
//==============================================================================
class SpectrumDisplay : public juce::Component, private juce::Timer
{
public:
    SpectrumDisplay();
    void pushBuffer(const float* data, int n);
    void paint(juce::Graphics&) override;
    void timerCallback() override;
    void setSurroundMode(bool s) { isSurround = s; }

private:
    static constexpr int FFT_ORDER = 10;
    static constexpr int FFT_SIZE  = 1 << FFT_ORDER;

    juce::dsp::FFT fft { FFT_ORDER };
    juce::dsp::WindowingFunction<float> window {
        (size_t)FFT_SIZE,
        juce::dsp::WindowingFunction<float>::hann
    };

    float fifo[FFT_SIZE]     = {};
    float fftData[FFT_SIZE*2]= {};
    float scopeData[300]     = {};
    float scopeSmooth[300]   = {};
    float scopePeak[300]     = {};
    int   fifoIdx = 0;
    bool  nextFFT = false;
    bool  isSurround = false;

    void drawNextFrameOfSpectrum();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumDisplay)
};

//==============================================================================
//  PER-CHANNEL FLOW DISPLAY
//==============================================================================
class ChannelScopeDisplay : public juce::Component
{
public:
    ChannelScopeDisplay();
    void pushLevels(const float levels[6]);
    void paint(juce::Graphics&) override;

private:
    static constexpr int historySize = 220;
    float history[6][historySize] = {};
    float latest[6] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelScopeDisplay)
};

//==============================================================================
//  PARAMETER CARDS BACKGROUND
//==============================================================================
class ParamsContent : public juce::Component
{
public:
    void setCards(const juce::Rectangle<int>& front,
                  const juce::Rectangle<int>& lfe,
                  const juce::Rectangle<int>& surround,
                  const juce::Rectangle<int>& space,
                  const juce::Rectangle<int>& calib);
    void paint(juce::Graphics&) override;

private:
    juce::Rectangle<int> frontCard;
    juce::Rectangle<int> lfeCard;
    juce::Rectangle<int> surroundCard;
    juce::Rectangle<int> spaceCard;
    juce::Rectangle<int> calibCard;
};

