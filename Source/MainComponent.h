#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include "UpmixEngine.h"
#include "UIComponents.h"

//==============================================================================
//  Main component: audio engine + transport + UI.
//==============================================================================
class MainComponent : public juce::AudioAppComponent,
                      public juce::Button::Listener,
                      public juce::ChangeListener,
                      public juce::FileDragAndDropTarget,
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
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void timerCallback() override;

private:
    struct SnapshotState
    {
        UpmixParams params;
        float masterGain = 1.0f;
        bool surroundMode = false;
        bool valid = false;
    };

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
    juce::TextButton batchExportBtn { "Batch Export Folder" };
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
    juce::TextButton presetSave   { "Save Preset" };
    juce::TextButton presetLoad   { "Load Preset" };
    int              activePreset = -1; // 0=cinema, 1=music, 2=vocal

    // A/B snapshot compare
    juce::TextButton snapshotStoreA  { "Store A" };
    juce::TextButton snapshotStoreB  { "Store B" };
    juce::TextButton snapshotRecallA { "Recall A" };
    juce::TextButton snapshotRecallB { "Recall B" };
    juce::Label      snapshotLabel;
    SnapshotState    snapshotA;
    SnapshotState    snapshotB;

    // Volume
    juce::Slider     masterVol;
    juce::Label      masterVolLabel;
    juce::Label      limiterLabel;
    juce::Label      meterStatsLabel;
    juce::TextButton resetMeterStatsBtn { "Reset Stats" };

    // Per-channel control row
    juce::Label channelControlLabel;
    std::array<juce::TextButton, 6> soloBtns {{
        juce::TextButton("FL"), juce::TextButton("FR"), juce::TextButton("FC"),
        juce::TextButton("LFE"), juce::TextButton("SL"), juce::TextButton("SR")
    }};

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
    juce::Label lblFront, lblLFE, lblSurround, lblSpace, lblCalib;
    std::array<std::unique_ptr<ParamSlider>, 6> calibTrim;
    std::array<std::unique_ptr<ParamSlider>, 6> calibDelay;
    std::array<std::unique_ptr<juce::ToggleButton>, 6> calibPolarity;

    // Helper methods
    // Shared loader used by file chooser and drag-and-drop.
    bool loadAudioFileForPlayback(const juce::File& fileToLoad, bool shouldAutoPlay);
    // Validates dropped files against supported audio formats.
    bool isSupportedAudioFile(const juce::File& file) const;
    // Opens the file chooser and loads a track.
    void openFile();
    // Exports loaded audio to a 6-channel WAV using current processing settings.
    void exportCurrentTrackTo51Wav();
    // Batch export multiple files using current settings.
    void exportBatchTo51Wav();
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
    // Captures current slider state into a parameter struct.
    UpmixParams captureParamsFromSliders() const;
    // Publishes slider params to the lock-free audio thread slot.
    void publishParamsToAudioThread();

    // Preset/session persistence helpers.
    juce::File getStateDirectory() const;
    juce::File getSessionFile() const;
    juce::File getDefaultPresetFile() const;
    bool savePresetToFile(const juce::File& file);
    bool loadPresetFromFile(const juce::File& file);
    void saveSessionState();
    void restoreSessionState();
    void saveDefaultPreset();
    void loadDefaultPreset();
    // Shared render function for single and batch export.
    bool renderFileTo51Wav(const juce::File& source,
                           const juce::File& target,
                           const UpmixParams& paramsSnapshot,
                           float masterGainSnapshot,
                           float& maxLimiterReductionDb,
                           juce::String& errorText);

    // Snapshot and routing helpers.
    void storeSnapshot(bool toA);
    void recallSnapshot(bool fromA);
    void refreshSnapshotLabel();
    // Channel control mode: 0=normal, 1=mute, 2=solo.
    int getChannelControlMode(int ch) const;
    void setChannelControlMode(int ch, int mode, bool shouldSave);
    void refreshChannelControlButton(int ch);
    void resetProMeters();
    void applyChannelMasking(juce::AudioBuffer<float>& buffer, int numSamples);
    void applySpeakerCalibration(juce::AudioBuffer<float>& buffer, int numSamples);
    void updateMeterStats(const juce::AudioBuffer<float>& buffer, int numSamples);

    bool fileLoaded = false;
    bool suppressSliderCallbacks = false;
    bool suppressSessionPersistence = false;
    bool timelineBeingDragged = false;
    bool dragDropHighlightActive = false;
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
    std::unique_ptr<juce::FileChooser> presetChooser;
    std::unique_ptr<juce::FileChooser> batchSourceChooser;
    std::unique_ptr<juce::FileChooser> batchTargetChooser;
    juce::Array<juce::File> batchPendingFiles;

    // Re-used real-time buffers (avoid heap allocations in audio callback).
    juce::AudioBuffer<float> stereoInBuf;

    // Lock-free parameter handoff: UI thread writes inactive slot, audio thread reads active slot.
    std::array<UpmixParams, 2> paramSlots { UpmixParams{}, UpmixParams{} };
    std::atomic<int> activeParamSlot { 0 };
    std::atomic<float> masterGainAtomic { 1.0f };

    // Output safety limiter + clip indicator state.
    float limiterGainState = 1.0f;
    float limiterReleaseCoeff = 0.0f;
    std::atomic<float> limiterReductionDbAtomic { 0.0f };
    std::atomic<int> limiterHoldBlocksAtomic { 0 };
    std::atomic<float> peakDbAtomic { -120.0f };
    std::atomic<float> lufsApproxAtomic { -70.0f };
    std::atomic<int> clipEventsAtomic { 0 };
    float lufsIntegrator = 0.0f;

    // Per-channel mute/solo state (audio-thread safe atomics).
    std::array<std::atomic<int>, 6> soloAtomic {};
    std::array<std::atomic<int>, 6> muteAtomic {};

    // Per-channel speaker calibration state.
    std::array<std::atomic<float>, 6> calibTrimGainAtomic {};
    std::array<std::atomic<float>, 6> calibDelayMsAtomic {};
    std::array<std::atomic<int>, 6> calibPolarityAtomic {};
    juce::AudioBuffer<float> speakerDelayBuffer;
    int speakerDelayWritePos = 0;
    int speakerDelayCapacity = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};


