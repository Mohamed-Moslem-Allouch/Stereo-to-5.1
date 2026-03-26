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
                 juce::AudioBuffer<float>&       sixChOut,
                 int numSamples,
                 bool surround51Active);

    UpmixParams params;

private:
    double sr = 44100.0;

    // Center channel filters
    juce::dsp::IIR::Filter<float> fcHPF;

    // LFE crossover: Linkwitz-Riley 4th order via cascaded Butterworth LPFs
    juce::dsp::IIR::Filter<float> lfeLP1, lfeLP2;
    juce::dsp::IIR::Filter<float> lfeShelf;  // low-shelf boost

    // Surround high-pass filters
    juce::dsp::IIR::Filter<float> slHPF, srHPF;

    // Haas delay lines (max 50 ms)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> slDelay { 8192 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> srDelay { 8192 };

    // Velvet-noise convolution for surround decorrelation
    juce::dsp::Convolution slConv, srConv;

    // Parameter-tracking for lazy IR rebuild
    float lastRoomSize     = -1.f;
    float lastDensity      = -1.f;
    float lastReverbWet    = -1.f;

    bool  graphPrepared    = false;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpmixEngine)
};

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
                  const juce::Rectangle<int>& space);
    void paint(juce::Graphics&) override;

private:
    juce::Rectangle<int> frontCard;
    juce::Rectangle<int> lfeCard;
    juce::Rectangle<int> surroundCard;
    juce::Rectangle<int> spaceCard;
};

//==============================================================================
//  Main component: audio engine + transport + UI.
//==============================================================================
class MainComponent : public juce::AudioAppComponent,
                      public juce::Button::Listener,
                      public juce::ChangeListener,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // AudioAppComponent
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo&) override;

    // GUI
    void paint(juce::Graphics&) override;
    void resized() override;
    void buttonClicked(juce::Button*) override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

private:
    // Audio objects
    juce::AudioFormatManager       formatManager;
    juce::AudioTransportSource     transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    UpmixEngine  engine;
    bool         surround51Active = false;
    double       currentSampleRate = 44100.0;

    // 6-channel working buffer (re-used every block)
    juce::AudioBuffer<float> sixChBuf;

    // Meter accumulation (RMS per block, per channel)
    float meterRms[6] = {};

    // UI objects
    StudioLookAndFeel laf;

    // Mode buttons
    ModeButton btnStereo  { "STEREO\nOriginal",    juce::Colour(0xff00cfff) };
    ModeButton btn51      { "5.1 SURROUND\nUpmix", juce::Colour(0xffff5e00) };

    // File
    juce::TextButton fileBtn  { "Open Audio File" };
    juce::TextButton exportBtn { "Export 5.1 WAV" };
    juce::TextButton settingsBtn { "Audio Settings" };
    juce::Label      fileLabel;
    juce::Label      statusLabel;
    juce::Label      channelImpactLabel;

    // Transport
    juce::TextButton playBtn  { "Play" };
    juce::TextButton stopBtn  { "Stop" };
    juce::Slider     timelineSlider;
    juce::Label      timelineLabel;

    // Presets
    juce::Label      presetLabel;
    juce::TextButton presetCinema { "Cinema Impact" };
    juce::TextButton presetMusic  { "Music Wide" };
    juce::TextButton presetVocal  { "Vocal Focus" };
    juce::TextButton presetReset  { "Reset" };

    // Volume
    juce::Slider     masterVol;
    juce::Label      masterVolLabel;

    // Meters
    ChannelMeter mFL  { "FL",  juce::Colour(0xff00cfff) };
    ChannelMeter mFR  { "FR",  juce::Colour(0xff00cfff) };
    ChannelMeter mFC  { "FC",  juce::Colour(0xff00e896) };
    ChannelMeter mLFE { "LFE", juce::Colour(0xffffd700) };
    ChannelMeter mSL  { "SL",  juce::Colour(0xffff5e00) };
    ChannelMeter mSR  { "SR",  juce::Colour(0xffff5e00) };

    // Spectrum
    SpectrumDisplay spectrum;
    ChannelScopeDisplay channelScope;

    // Scrollable params container
    juce::Viewport paramsViewport;
    ParamsContent paramsContent;

    // Parameter sliders (mapped to UpmixParams)
    // Front
    ParamSlider sFrontGain   { "Front Level",       0.3f,  1.5f,  1.00f, "x",   juce::Colour(0xff00cfff) };
    ParamSlider sCenterGain  { "Center (Vocals)",   0.0f,  1.8f,  1.05f, "x",   juce::Colour(0xff00e896) };
    ParamSlider sCenterHPF   { "Center HPF",        60.f,  400.f, 130.f, "Hz",  juce::Colour(0xff00e896) };

    // LFE
    ParamSlider sLFEGain     { "LFE Gain",          0.0f,  3.0f,  1.20f, "x",   juce::Colour(0xffffd700) };
    ParamSlider sLFEHz       { "LFE Crossover",     40.f,  160.f, 90.f,  "Hz",  juce::Colour(0xffffd700) };
    ParamSlider sLFEShelf    { "Sub Shelf Boost",   0.0f,  12.f,  3.5f,  "dB",  juce::Colour(0xffffd700) };
    ParamSlider sExciter     { "Bass Exciter",      0.0f,  1.0f,  0.45f, "",    juce::Colour(0xffffd700) };

    // Surround
    ParamSlider sSurrGain    { "Surround Level",    0.0f,  1.8f,  0.95f, "x",   juce::Colour(0xffff5e00) };
    ParamSlider sHaasMs      { "Haas Delay",        5.0f,  28.f,  16.f,  "ms",  juce::Colour(0xffff5e00) };
    ParamSlider sSurrHPF     { "Surround HPF",      60.f,  500.f, 140.f, "Hz",  juce::Colour(0xffff5e00) };
    ParamSlider sSideBlend   { "Side Blend",        0.0f,  1.5f,  0.85f, "",    juce::Colour(0xffff5e00) };
    // Center Spill to Surround:
    // Adds some center (mid) information into SL/SR so the rear field can feel fuller.
    // Lower values keep vocals mostly in FC; higher values spread vocals more into surrounds.
    ParamSlider sMidBlend    { "Center Spill to Surround", 0.0f,  0.6f,  0.12f, "", juce::Colour(0xffff5e00) };

    // Space
    ParamSlider sReverbWet   { "Reverb Wet",        0.0f,  1.0f,  0.62f, "",    juce::Colour(0xffaa88ff) };
    ParamSlider sRoomSize    { "Room Size",          0.2f,  0.98f, 0.74f, "",    juce::Colour(0xffaa88ff) };
    ParamSlider sVelvetDens  { "Decorrelation",     500.f, 4000.f,2200.f,"Hz",  juce::Colour(0xffaa88ff) };

    // Section labels
    juce::Label lblFront, lblLFE, lblSurround, lblSpace;

    // Helper methods
    // Opens the file chooser and loads a track.
    void openFile();
    // Exports loaded audio to a 6-channel WAV using current processing settings.
    void exportCurrentTrackTo51Wav();
    // Opens JUCE audio device settings dialog.
    void openAudioSettings();
    // Updates play button behavior and label.
    void setPlayState(bool playing);
    // Applies a full preset to all parameter sliders.
    void applyPreset(const UpmixParams& preset);
    // Built-in preset entry points.
    void applyPresetCinema();
    void applyPresetMusicWide();
    void applyPresetVocalFocus();
    void resetToDefaultPreset();
    // Updates top status text (mode/device/channel count).
    void refreshStatusLabel();
    // Highlights affected output channels in the UI.
    void flashChannelFocus(std::initializer_list<int> channels, const juce::String& sourceName);
    // Routes slider edits to channel-impact visualization.
    void onSliderEdited(ParamSlider* source);
    // Syncs seek bar + time text to current transport state.
    void updateTimelineFromTransport();
    // Reconfigures active output channels based on current output device.
    void configureOutputForCurrentDevice();
    // Follows OS default output device changes in real time.
    void followSystemOutputDevice();
    // Returns current OS default output device name.
    juce::String queryDefaultOutputDeviceName();
    // Utility formatter for timeline labels.
    static juce::String formatTime(double seconds);
    // Copies all slider values into engine parameters.
    void syncParamsFromSliders();

    bool fileLoaded = false;
    bool suppressSliderCallbacks = false;
    bool timelineBeingDragged = false;
    bool deviceReconfigInProgress = false;
    int statusRefreshTick = 0;
    int outputPollTick = 0;
    float channelFocus[6] = {};
    double trackLengthSeconds = 0.0;
    juce::File loadedFile;
    juce::String lastKnownOutputDeviceName;
    juce::String lastKnownDefaultOutputName;
    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::FileChooser> exportChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

