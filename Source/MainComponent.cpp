#include "MainComponent.h"
#include "ThemeColours.h"

namespace
{
constexpr float limiterThreshold = 0.965f;
constexpr float clipDetectThreshold = 0.999f;
constexpr std::array<const char*, 6> channelShortNames { "FL", "FR", "FC", "LFE", "SL", "SR" };

juce::Colour getChannelUiColour(int ch)
{
    static const std::array<juce::Colour, 6> colours
    {
        juce::Colour(0xff00cfff), // FL
        juce::Colour(0xff00cfff), // FR
        juce::Colour(0xff00e896), // FC
        juce::Colour(0xffffd700), // LFE
        juce::Colour(0xffff5e00), // SL
        juce::Colour(0xffff5e00)  // SR
    };

    if (juce::isPositiveAndBelow(ch, (int)colours.size()))
        return colours[(size_t)ch];
    return COL_ACC;
}
float readFloatProperty(const juce::var& objectVar, const juce::Identifier& property, float fallback)
{
    if (const auto* obj = objectVar.getDynamicObject())
    {
        const auto value = obj->getProperty(property);
        if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
            return static_cast<float>(double(value));
    }
    return fallback;
}

int readIntProperty(const juce::var& objectVar, const juce::Identifier& property, int fallback)
{
    if (const auto* obj = objectVar.getDynamicObject())
    {
        const auto value = obj->getProperty(property);
        if (value.isInt() || value.isInt64() || value.isDouble() || value.isBool())
            return int(value);
    }
    return fallback;
}

bool readBoolProperty(const juce::var& objectVar, const juce::Identifier& property, bool fallback)
{
    if (const auto* obj = objectVar.getDynamicObject())
    {
        const auto value = obj->getProperty(property);
        if (value.isBool())
            return bool(value);
        if (value.isInt() || value.isInt64() || value.isDouble())
            return int(value) != 0;
    }
    return fallback;
}

juce::String readStringProperty(const juce::var& objectVar, const juce::Identifier& property,
                                const juce::String& fallback = {})
{
    if (const auto* obj = objectVar.getDynamicObject())
    {
        const auto value = obj->getProperty(property);
        if (value.isString())
            return value.toString();
    }
    return fallback;
}

juce::var upmixParamsToVar(const UpmixParams& p)
{
    auto* paramsObj = new juce::DynamicObject();
    paramsObj->setProperty("frontGain", p.frontGain);
    paramsObj->setProperty("centerGain", p.centerGain);
    paramsObj->setProperty("centerHPF", p.centerHPF);
    paramsObj->setProperty("lfeGain", p.lfeGain);
    paramsObj->setProperty("lfeCrossover", p.lfeCrossover);
    paramsObj->setProperty("lfeShelfGain", p.lfeShelfGain);
    paramsObj->setProperty("exciterDrive", p.exciterDrive);
    paramsObj->setProperty("surroundGain", p.surroundGain);
    paramsObj->setProperty("haasDelayMs", p.haasDelayMs);
    paramsObj->setProperty("surroundHPF", p.surroundHPF);
    paramsObj->setProperty("sideBlend", p.sideBlend);
    paramsObj->setProperty("midBlend", p.midBlend);
    paramsObj->setProperty("reverbWet", p.reverbWet);
    paramsObj->setProperty("roomSize", p.roomSize);
    paramsObj->setProperty("velvetDensity", p.velvetDensity);
    return juce::var(paramsObj);
}

UpmixParams upmixParamsFromVar(const juce::var& paramsVar, const UpmixParams& fallback)
{
    UpmixParams p = fallback;
    p.frontGain = readFloatProperty(paramsVar, "frontGain", p.frontGain);
    p.centerGain = readFloatProperty(paramsVar, "centerGain", p.centerGain);
    p.centerHPF = readFloatProperty(paramsVar, "centerHPF", p.centerHPF);
    p.lfeGain = readFloatProperty(paramsVar, "lfeGain", p.lfeGain);
    p.lfeCrossover = readFloatProperty(paramsVar, "lfeCrossover", p.lfeCrossover);
    p.lfeShelfGain = readFloatProperty(paramsVar, "lfeShelfGain", p.lfeShelfGain);
    p.exciterDrive = readFloatProperty(paramsVar, "exciterDrive", p.exciterDrive);
    p.surroundGain = readFloatProperty(paramsVar, "surroundGain", p.surroundGain);
    p.haasDelayMs = readFloatProperty(paramsVar, "haasDelayMs", p.haasDelayMs);
    p.surroundHPF = readFloatProperty(paramsVar, "surroundHPF", p.surroundHPF);
    p.sideBlend = readFloatProperty(paramsVar, "sideBlend", p.sideBlend);
    p.midBlend = readFloatProperty(paramsVar, "midBlend", p.midBlend);
    p.reverbWet = readFloatProperty(paramsVar, "reverbWet", p.reverbWet);
    p.roomSize = readFloatProperty(paramsVar, "roomSize", p.roomSize);
    p.velvetDensity = readFloatProperty(paramsVar, "velvetDensity", p.velvetDensity);
    return p;
}

bool writeJsonFile(const juce::File& file, const juce::var& rootVar)
{
    auto parent = file.getParentDirectory();
    if (!parent.exists() && !parent.createDirectory())
        return false;

    return file.replaceWithText(juce::JSON::toString(rootVar, true));
}

bool readJsonFile(const juce::File& file, juce::var& outVar)
{
    if (!file.existsAsFile())
        return false;

    auto parsed = juce::JSON::parse(file);
    if (parsed.isVoid())
        return false;

    outVar = parsed;
    return true;
}

void applySafetyLimiter(juce::AudioBuffer<float>& buffer,
                        int numChannels,
                        int numSamples,
                        float threshold,
                        float releaseCoeff,
                        float& gainState,
                        float& reductionDbOut,
                        int& clipHoldBlocks)
{
    if (numChannels <= 0 || numSamples <= 0)
    {
        reductionDbOut = 0.0f;
        clipHoldBlocks = juce::jmax(0, clipHoldBlocks - 1);
        return;
    }

    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));
    }

    const float targetGain = peak > threshold ? (threshold / peak) : 1.0f;
    if (targetGain < gainState)
    {
        gainState = targetGain; // fast attack to prevent overs.
    }
    else
    {
        const float clampedRelease = juce::jlimit(0.0f, 0.999999f, releaseCoeff);
        const float releasePow = std::pow(clampedRelease, float(numSamples));
        gainState = targetGain + (gainState - targetGain) * releasePow;
    }

    gainState = juce::jlimit(0.02f, 1.0f, gainState);
    if (gainState < 0.9999f)
        buffer.applyGain(0, numSamples, gainState);

    const float gainDb = juce::Decibels::gainToDecibels(gainState, -120.0f);
    reductionDbOut = juce::jmax(0.0f, -gainDb);

    if (peak >= clipDetectThreshold)
        clipHoldBlocks = juce::jmax(clipHoldBlocks, 15);
    else
        clipHoldBlocks = juce::jmax(0, clipHoldBlocks - 1);
}
} // namespace


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
        // Dark, cohesive button palette with subtle accent tint.
        b.setColour(juce::TextButton::buttonColourId,   base.withAlpha(emphasize ? 0.82f : 0.69f));
        b.setColour(juce::TextButton::buttonOnColourId, base.brighter(0.12f).withAlpha(0.86f));
        b.setColour(juce::TextButton::textColourOffId,  COL_TXT.withAlpha(0.95f));
        b.setColour(juce::TextButton::textColourOnId,   COL_TXT);
    };

    auto themedButton = [](juce::Colour accent, float accentAmount = 0.22f)
    {
        const auto base = COL_CARD.interpolatedWith(COL_BG2, 0.48f);
        return base.interpolatedWith(accent, juce::jlimit(0.0f, 0.42f, accentAmount));
    };

    // Custom LookAndFeel override for text buttons to get border/rounded style
    struct ButtonLAF : public juce::LookAndFeel_V4
    {
        // Thin-bordered, lightly glossy buttons.
        void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                  bool hover, bool down) override
        {
            auto bounds = b.getLocalBounds().toFloat().reduced(0.5f);
            auto baseCol = bg;
            if (hover) baseCol = baseCol.brighter(0.08f);
            if (down)  baseCol = baseCol.brighter(0.16f);

            juce::ColourGradient grad(baseCol.brighter(0.06f), bounds.getX(), bounds.getY(),
                                      baseCol.darker(0.05f).withAlpha(baseCol.getAlpha() * 0.82f), bounds.getX(), bounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bounds, 6.0f);

            // Thin border
            g.setColour(baseCol.brighter(0.26f).withAlpha(0.48f));
            g.drawRoundedRectangle(bounds, 6.0f, 0.9f);
        }
    };

    fileBtn.setButtonText("  Open File");
    exportBtn.setButtonText("  Export 5.1 WAV");
    batchExportBtn.setButtonText("  Batch Export");
    settingsBtn.setButtonText("  Audio Settings");

    fileLabel.setColour(juce::Label::textColourId, COL_TXT.withAlpha(0.72f));
    fileLabel.setColour(juce::Label::backgroundColourId, COL_BG2.withAlpha(0.70f));
    fileLabel.setColour(juce::Label::outlineColourId, COL_BRDR.withAlpha(0.65f));
    fileLabel.setText("  No file loaded", juce::dontSendNotification);
    fileLabel.setBorderSize({ 3, 8, 3, 8 });
    fileLabel.setJustificationType(juce::Justification::centredLeft);
    fileLabel.setFont(juce::Font(juce::FontOptions("Consolas", 11.5f, juce::Font::plain)));

    statusLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.45f));
    statusLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    statusLabel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    statusLabel.setBorderSize({ 2, 10, 2, 10 });
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setFont(juce::Font(juce::FontOptions("Consolas", 11.f, juce::Font::plain)));

    channelImpactLabel.setColour(juce::Label::textColourId, COL_ACC.withAlpha(0.80f));
    channelImpactLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    channelImpactLabel.setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    channelImpactLabel.setBorderSize({ 2, 4, 2, 4 });
    channelImpactLabel.setJustificationType(juce::Justification::centredLeft);
    channelImpactLabel.setFont(juce::Font(juce::FontOptions("Consolas", 10.5f, juce::Font::plain)));
    channelImpactLabel.setText("Effect Chain:  Master > Front L/R > Front C > LFE > Surround L/R", juce::dontSendNotification);

    timelineSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timelineSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    timelineSlider.setRange(0.0, 1.0, 0.001);
    timelineSlider.setColour(juce::Slider::trackColourId, COL_ACC.withAlpha(0.80f));
    timelineSlider.setColour(juce::Slider::thumbColourId, juce::Colours::white.withAlpha(0.95f));
    timelineSlider.getProperties().set("accent", COL_ACC.toString());
    timelineSlider.onDragStart = [this] { timelineBeingDragged = true; };
    timelineSlider.onDragEnd = [this]
    {
        timelineBeingDragged = false;
        if (fileLoaded)
            transport.setPosition(timelineSlider.getValue());
        updateTimelineFromTransport();
        saveSessionState();
    };
    timelineSlider.onValueChange = [this]
    {
        if (timelineBeingDragged && fileLoaded)
            transport.setPosition(timelineSlider.getValue());
    };

    timelineLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    timelineLabel.setJustificationType(juce::Justification::centredRight);
    timelineLabel.setFont(juce::Font(juce::FontOptions("Consolas", 12.f, juce::Font::bold)));
    timelineLabel.setText("00:00 / 00:00", juce::dontSendNotification);

    presetLabel.setText("Scene Presets", juce::dontSendNotification);
    presetLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.55f));
    presetLabel.setFont(juce::Font(juce::FontOptions("Consolas", 11.f, juce::Font::bold)));

    snapshotLabel.setText("A/B Snapshots: empty", juce::dontSendNotification);
    snapshotLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.42f));
    snapshotLabel.setFont(juce::Font(juce::FontOptions("Consolas", 10.5f, juce::Font::plain)));

    meterStatsLabel.setText("Peak: -120.0 dBFS  |  LUFS approx: -70.0  |  Clips: 0", juce::dontSendNotification);
    meterStatsLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.42f));
    meterStatsLabel.setFont(juce::Font(juce::FontOptions("Consolas", 10.5f, juce::Font::plain)));
    meterStatsLabel.setJustificationType(juce::Justification::centredLeft);

    channelControlLabel.setText("Channel Controls (click: Normal -> Mute -> Solo):", juce::dontSendNotification);
    channelControlLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.42f));
    channelControlLabel.setFont(juce::Font(juce::FontOptions("Consolas", 10.5f, juce::Font::bold)));

    styleButton(fileBtn,      themedButton(COL_ACC, 0.20f));
    styleButton(exportBtn,    themedButton(COL_YLW, 0.24f));
    styleButton(batchExportBtn, themedButton(COL_PRP, 0.22f));
    styleButton(settingsBtn,  themedButton(COL_GRN, 0.22f));
    styleButton(playBtn,      themedButton(COL_GRN, 0.30f), true);
    styleButton(stopBtn,      themedButton(COL_ORG, 0.26f));
    styleButton(presetCinema, themedButton(COL_ORG, 0.26f));
    styleButton(presetMusic,  themedButton(COL_ACC, 0.25f));
    styleButton(presetVocal,  themedButton(COL_GRN, 0.25f));
    styleButton(presetReset,  themedButton(COL_PRP, 0.18f));
    styleButton(presetSave,   themedButton(COL_PRP, 0.24f));
    styleButton(presetLoad,   themedButton(COL_ACC, 0.24f));
    styleButton(snapshotStoreA, themedButton(COL_YLW, 0.23f));
    styleButton(snapshotStoreB, themedButton(COL_ORG, 0.23f));
    styleButton(snapshotRecallA, themedButton(COL_ACC, 0.23f));
    styleButton(snapshotRecallB, themedButton(COL_PRP, 0.23f));
    styleButton(resetMeterStatsBtn, themedButton(COL_PRP, 0.17f));

    for (int ch = 0; ch < 6; ++ch)
    {
        soloAtomic[(size_t)ch].store(0, std::memory_order_relaxed);
        muteAtomic[(size_t)ch].store(0, std::memory_order_relaxed);
        styleButton(soloBtns[(size_t)ch], themedButton(getChannelUiColour(ch), 0.22f));
    }

    addAndMakeVisible(fileBtn);
    addAndMakeVisible(exportBtn);
    addAndMakeVisible(batchExportBtn);
    addAndMakeVisible(settingsBtn);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(statusLabel);
    addAndMakeVisible(channelImpactLabel);
    addAndMakeVisible(snapshotLabel);
    addAndMakeVisible(channelControlLabel);
    addAndMakeVisible(timelineSlider);
    addAndMakeVisible(timelineLabel);
    addAndMakeVisible(playBtn);
    addAndMakeVisible(stopBtn);
    addAndMakeVisible(presetLabel);
    addAndMakeVisible(presetCinema);
    addAndMakeVisible(presetMusic);
    addAndMakeVisible(presetVocal);
    addAndMakeVisible(presetReset);
    addAndMakeVisible(presetSave);
    addAndMakeVisible(presetLoad);
    addAndMakeVisible(snapshotStoreA);
    addAndMakeVisible(snapshotStoreB);
    addAndMakeVisible(snapshotRecallA);
    addAndMakeVisible(snapshotRecallB);
    addAndMakeVisible(meterStatsLabel);
    addAndMakeVisible(resetMeterStatsBtn);

    for (int ch = 0; ch < 6; ++ch)
        addAndMakeVisible(soloBtns[(size_t)ch]);
    addAndMakeVisible(spectrum);
    addAndMakeVisible(channelScope);
    addAndMakeVisible(paramsViewport);

    paramsViewport.setViewedComponent(&paramsContent, false);
    paramsViewport.setScrollBarsShown(true, false, true, false);
    paramsViewport.setSingleStepSizes(28, 28);

    fileBtn.onClick      = [this] { openFile(); };
    exportBtn.onClick    = [this] { exportCurrentTrackTo51Wav(); };
    batchExportBtn.onClick = [this] { exportBatchTo51Wav(); };
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
    stopBtn.onClick      = [this] { transport.stop(); transport.setPosition(0.0); setPlayState(false); saveSessionState(); };
    presetCinema.onClick = [this] { applyPresetCinema(); };
    presetMusic.onClick  = [this] { applyPresetMusicWide(); };
    presetVocal.onClick  = [this] { applyPresetVocalFocus(); };
    presetReset.onClick  = [this] { resetToDefaultPreset(); };
    snapshotStoreA.onClick = [this] { storeSnapshot(true); };
    snapshotStoreB.onClick = [this] { storeSnapshot(false); };
    snapshotRecallA.onClick = [this] { recallSnapshot(true); };
    snapshotRecallB.onClick = [this] { recallSnapshot(false); };
    resetMeterStatsBtn.onClick = [this] { resetProMeters(); };
    presetSave.onClick   = [this]
    {
        presetChooser = std::make_unique<juce::FileChooser>(
            "Save current upmix preset",
            getDefaultPresetFile(),
            "*.json");

        presetChooser->launchAsync(juce::FileBrowserComponent::saveMode
                                    | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& chooser)
            {
                auto target = chooser.getResult();
                if (target == juce::File{})
                    return;

                if (!target.hasFileExtension("json"))
                    target = target.withFileExtension(".json");

                if (savePresetToFile(target))
                    channelImpactLabel.setText("Preset saved: " + target.getFileName(), juce::dontSendNotification);
                else
                    channelImpactLabel.setText("Preset save failed.", juce::dontSendNotification);
            });
    };
    presetLoad.onClick   = [this]
    {
        presetChooser = std::make_unique<juce::FileChooser>(
            "Load upmix preset",
            getStateDirectory(),
            "*.json");

        presetChooser->launchAsync(juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser)
            {
                auto target = chooser.getResult();
                if (target == juce::File{})
                    return;

                if (loadPresetFromFile(target))
                    channelImpactLabel.setText("Preset loaded: " + target.getFileName(), juce::dontSendNotification);
                else
                    channelImpactLabel.setText("Preset load failed.", juce::dontSendNotification);
            });
    };

    for (int ch = 0; ch < 6; ++ch)
    {
        auto& soloBtn = soloBtns[(size_t)ch];
        soloBtn.setClickingTogglesState(false);
        soloBtn.onClick = [this, ch]
        {
            int mode = getChannelControlMode(ch);
            mode = (mode + 1) % 3;
            setChannelControlMode(ch, mode, true);
            flashChannelFocus({ ch }, juce::String(channelShortNames[(size_t)ch]) + " " + (mode == 2 ? "Solo" : (mode == 1 ? "Mute" : "Normal")));
        };
        setChannelControlMode(ch, 0, false);
    }

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
    masterVolLabel.setColour(juce::Label::textColourId, COL_MUT.brighter(0.45f));
    masterVolLabel.setFont(juce::Font(juce::FontOptions("Consolas", 11.f, juce::Font::plain)));
    limiterLabel.setText("Limiter: 0.0 dB", juce::dontSendNotification);
    limiterLabel.setColour(juce::Label::textColourId, COL_GRN.withAlpha(0.90f));
    limiterLabel.setJustificationType(juce::Justification::centredRight);
    limiterLabel.setFont(juce::Font(juce::FontOptions("Consolas", 11.f, juce::Font::bold)));
    masterVol.onValueChange = [this] {
        masterGainAtomic.store(float(masterVol.getValue()), std::memory_order_relaxed);
        flashChannelFocus({0, 1, 2, 3, 4, 5}, "Master Level");
    };
    masterVol.onDragEnd = [this] { saveSessionState(); };
    addAndMakeVisible(masterVol);
    addAndMakeVisible(masterVolLabel);
    addAndMakeVisible(limiterLabel);

    addAndMakeVisible(mFL); addAndMakeVisible(mFR);
    addAndMakeVisible(mFC); addAndMakeVisible(mLFE);
    addAndMakeVisible(mSL); addAndMakeVisible(mSR);

    for (auto* l : {&lblFront, &lblLFE, &lblSurround, &lblSpace, &lblCalib})
    {
        l->setFont(juce::Font(juce::FontOptions("Consolas", 11.5f, juce::Font::bold)));
        l->setMinimumHorizontalScale(0.80f);
        l->setColour(juce::Label::textColourId, COL_MUT.brighter(0.55f));
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setBorderSize({ 0, 0, 9, 0 });
        l->setJustificationType(juce::Justification::centredLeft);
        paramsContent.addAndMakeVisible(l);
    }
    lblFront.setText   ("FRONT STAGE - FL / FR / FC", juce::dontSendNotification);
    lblLFE.setText     ("SUB BASS - LFE", juce::dontSendNotification);
    lblSurround.setText("SURROUND FIELD - SL / SR", juce::dontSendNotification);
    lblSpace.setText   ("SPACE / DECORRELATION", juce::dontSendNotification);
    lblCalib.setText   ("SPEAKER CALIBRATION - TRIM / DELAY / POLARITY", juce::dontSendNotification);
    lblFront.setColour   (juce::Label::textColourId, COL_ACC);
    lblLFE.setColour     (juce::Label::textColourId, COL_YLW);
    lblSurround.setColour(juce::Label::textColourId, COL_ORG);
    lblSpace.setColour   (juce::Label::textColourId, COL_PRP);
    lblCalib.setColour   (juce::Label::textColourId, juce::Colour(0xff8dd6ff));

    auto wire = [this](ParamSlider& ps) {
        ps.onChange = [this, &ps](float) {
            if (suppressSliderCallbacks)
                return;
            syncParamsFromSliders();
            onSliderEdited(&ps);
        };
        ps.slider.onDragEnd = [this] { saveSessionState(); };
        paramsContent.addAndMakeVisible(ps);
    };
    for (auto* ps : {&sFrontGain,&sCenterGain,&sCenterHPF,
                     &sLFEGain,&sLFEHz,&sLFEShelf,&sExciter,
                     &sSurrGain,&sHaasMs,&sSurrHPF,&sSideBlend,&sMidBlend,
                     &sReverbWet,&sRoomSize,&sVelvetDens})
        wire(*ps);

    for (int ch = 0; ch < 6; ++ch)
    {
        const auto name = juce::String(channelShortNames[(size_t)ch]);

        calibTrim[(size_t)ch] = std::make_unique<ParamSlider>(
            name + " Trim",
            -12.0f, 12.0f, 0.0f, "dB",
            juce::Colour(0xff66d0ff));
        calibTrim[(size_t)ch]->onChange = [this, ch](float v)
        {
            calibTrimGainAtomic[(size_t)ch].store(juce::Decibels::decibelsToGain(v), std::memory_order_relaxed);
            flashChannelFocus({ ch }, juce::String(channelShortNames[(size_t)ch]) + " Trim");
        };
        calibTrim[(size_t)ch]->slider.onDragEnd = [this] { saveSessionState(); };
        paramsContent.addAndMakeVisible(*calibTrim[(size_t)ch]);

        calibDelay[(size_t)ch] = std::make_unique<ParamSlider>(
            name + " Delay",
            0.0f, 40.0f, 0.0f, "ms",
            juce::Colour(0xff66d0ff));
        calibDelay[(size_t)ch]->onChange = [this, ch](float v)
        {
            calibDelayMsAtomic[(size_t)ch].store(v, std::memory_order_relaxed);
            flashChannelFocus({ ch }, juce::String(channelShortNames[(size_t)ch]) + " Delay");
        };
        calibDelay[(size_t)ch]->slider.onDragEnd = [this] { saveSessionState(); };
        paramsContent.addAndMakeVisible(*calibDelay[(size_t)ch]);

        calibPolarity[(size_t)ch] = std::make_unique<juce::ToggleButton>("Invert " + name);
        calibPolarity[(size_t)ch]->setColour(juce::ToggleButton::textColourId, COL_MUT.brighter(0.6f));
        calibPolarity[(size_t)ch]->setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff66d0ff));
        calibPolarity[(size_t)ch]->onClick = [this, ch]
        {
            const int state = calibPolarity[(size_t)ch]->getToggleState() ? 1 : 0;
            calibPolarityAtomic[(size_t)ch].store(state, std::memory_order_relaxed);
            flashChannelFocus({ ch }, juce::String(channelShortNames[(size_t)ch]) + " Polarity");
            saveSessionState();
        };
        paramsContent.addAndMakeVisible(*calibPolarity[(size_t)ch]);

        calibTrimGainAtomic[(size_t)ch].store(1.0f, std::memory_order_relaxed);
        calibDelayMsAtomic[(size_t)ch].store(0.0f, std::memory_order_relaxed);
        calibPolarityAtomic[(size_t)ch].store(0, std::memory_order_relaxed);
    }

    // Audio device setup
    setAudioChannels(0, 2);
    configureOutputForCurrentDevice();
    deviceManager.addChangeListener(this);

    // Prime lock-free parameter slots and gain state from UI defaults.
    paramSlots[0] = captureParamsFromSliders();
    paramSlots[1] = paramSlots[0];
    activeParamSlot.store(0, std::memory_order_release);
    masterGainAtomic.store(float(masterVol.getValue()), std::memory_order_relaxed);

    refreshStatusLabel();
    updateTimelineFromTransport();
    suppressSessionPersistence = true;
    loadDefaultPreset();
    restoreSessionState();
    suppressSessionPersistence = false;
    saveSessionState();
    refreshSnapshotLabel();
    resetProMeters();

    setSize(1120, 860);
    startTimerHz(30);
}

// Cleans up listeners and shuts down audio safely.
MainComponent::~MainComponent()
{
    saveSessionState();
    saveDefaultPreset();
    deviceManager.removeChangeListener(this);
    shutdownAudio();
    setLookAndFeel(nullptr);
}

// Called by JUCE when the audio device starts or changes sample rate/buffer size.
void MainComponent::prepareToPlay(int blockSize, double sampleRate)
{
    currentSampleRate = sampleRate;
    transport.prepareToPlay(blockSize, sampleRate);
    sixChBuf.setSize(6, blockSize, false, true);
    stereoInBuf.setSize(2, blockSize, false, true);
    engine.prepare(sampleRate, blockSize);

    const int activeSlot = juce::jlimit(0, 1, activeParamSlot.load(std::memory_order_acquire));
    engine.params = paramSlots[(size_t)activeSlot];

    limiterGainState = 1.0f;
    limiterReleaseCoeff = std::exp(-1.0f / float(juce::jmax(1.0, sampleRate * 0.18)));
    limiterReductionDbAtomic.store(0.0f, std::memory_order_relaxed);
    limiterHoldBlocksAtomic.store(0, std::memory_order_relaxed);

    speakerDelayCapacity = juce::jmax(8, int(sampleRate * 0.200) + blockSize + 8);
    speakerDelayBuffer.setSize(6, speakerDelayCapacity, false, true, true);
    speakerDelayBuffer.clear();
    speakerDelayWritePos = 0;
}

// Called by JUCE when playback is stopping or audio device is being released.
void MainComponent::releaseResources()
{
    transport.releaseResources();
    engine.reset();
    speakerDelayBuffer.clear();
    speakerDelayWritePos = 0;
    speakerDelayCapacity = 0;
}

// Real-time audio callback: reads stereo file data, upmixes to 5.1, and writes to hardware output.
void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();

    if (!fileLoaded || !transport.isPlaying())
    {
        std::fill(std::begin(meterRms), std::end(meterRms), 0.0f);
        limiterReductionDbAtomic.store(0.0f, std::memory_order_relaxed);
        limiterHoldBlocksAtomic.store(0, std::memory_order_relaxed);
        return;
    }

    const int numSamples = info.numSamples;
    if (numSamples <= 0
        || numSamples > stereoInBuf.getNumSamples()
        || numSamples > sixChBuf.getNumSamples())
    {
        jassertfalse;
        std::fill(std::begin(meterRms), std::end(meterRms), 0.0f);
        return;
    }

    // Pull stereo source into reusable callback buffer.
    stereoInBuf.clear(0, numSamples);
    juce::AudioSourceChannelInfo stereoInfo(&stereoInBuf, 0, numSamples);
    transport.getNextAudioBlock(stereoInfo);

    // Atomically switch to the latest UI parameters.
    const int activeSlot = juce::jlimit(0, 1, activeParamSlot.load(std::memory_order_acquire));
    engine.params = paramSlots[(size_t)activeSlot];

    // Process into 6-channel output.
    sixChBuf.clear(0, numSamples);
    engine.process(stereoInBuf, sixChBuf, numSamples, surround51Active);

    const float master = masterGainAtomic.load(std::memory_order_relaxed);
    if (master != 1.0f)
        sixChBuf.applyGain(0, numSamples, master);

    applySpeakerCalibration(sixChBuf, numSamples);
    applyChannelMasking(sixChBuf, numSamples);

    float reductionDb = 0.0f;
    int limiterHold = limiterHoldBlocksAtomic.load(std::memory_order_relaxed);
    applySafetyLimiter(sixChBuf, 6, numSamples, limiterThreshold, limiterReleaseCoeff,
                       limiterGainState, reductionDb, limiterHold);
    limiterReductionDbAtomic.store(reductionDb, std::memory_order_relaxed);
    limiterHoldBlocksAtomic.store(limiterHold, std::memory_order_relaxed);

    // Write to the currently active hardware outputs.
    const int outputsToWrite = juce::jmin(6, info.buffer->getNumChannels());
    for (int ch = 0; ch < outputsToWrite; ++ch)
    {
        float* dest = info.buffer->getWritePointer(ch, info.startSample);
        const float* src = sixChBuf.getReadPointer(ch);
        juce::FloatVectorOperations::copy(dest, src, numSamples);
    }

    // Feed meters and statistics from the processed 6-channel bus.
    updateMeterStats(sixChBuf, numSamples);

    // Spectrum follows FL channel.
    spectrum.pushBuffer(sixChBuf.getReadPointer(0), numSamples);
}

// UI timer: refreshes meters, timeline, and device-follow checks on the message thread.
void MainComponent::timerCallback()
{
    // End-of-track handling: auto-return to start and expose a one-click restart state.
    if (fileLoaded && transport.hasStreamFinished() && playBtn.getButtonText() != "Start")
    {
        transport.stop();
        transport.setPosition(0.0);
        setPlayState(false);
        playBtn.setButtonText("Start");
        saveSessionState();
    }

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

    const float limiterReduction = limiterReductionDbAtomic.load(std::memory_order_relaxed);
    const int limiterClipHold = limiterHoldBlocksAtomic.load(std::memory_order_relaxed);
    if (limiterClipHold > 0)
        limiterLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
    else if (limiterReduction > 0.10f)
        limiterLabel.setColour(juce::Label::textColourId, juce::Colour(0xffffc85a));
    else
        limiterLabel.setColour(juce::Label::textColourId, COL_GRN.withAlpha(0.90f));

    limiterLabel.setText("Limiter: -" + juce::String(limiterReduction, 1) + " dB",
                         juce::dontSendNotification);

    meterStatsLabel.setText(
        "Peak: " + juce::String(peakDbAtomic.load(std::memory_order_relaxed), 1) + " dBFS"
        + "  |  LUFS approx: " + juce::String(lufsApproxAtomic.load(std::memory_order_relaxed), 1)
        + "  |  Clips: " + juce::String(clipEventsAtomic.load(std::memory_order_relaxed)),
        juce::dontSendNotification);

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
    if (b == &btnStereo) { surround51Active = false; engine.reset(); flashChannelFocus({0, 1}, "Stereo Mode"); saveSessionState(); }
    if (b == &btn51)     { surround51Active = true;  engine.reset(); flashChannelFocus({0, 1, 2, 3, 4, 5}, "5.1 Mode"); saveSessionState(); }
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
    publishParamsToAudioThread();
}

// Captures the current UI slider state into a single parameter bundle.
UpmixParams MainComponent::captureParamsFromSliders() const
{
    UpmixParams p;
    p.frontGain     = float(sFrontGain.slider.getValue());
    p.centerGain    = float(sCenterGain.slider.getValue());
    p.centerHPF     = float(sCenterHPF.slider.getValue());
    p.lfeGain       = float(sLFEGain.slider.getValue());
    p.lfeCrossover  = float(sLFEHz.slider.getValue());
    p.lfeShelfGain  = float(sLFEShelf.slider.getValue());
    p.exciterDrive  = float(sExciter.slider.getValue());
    p.surroundGain  = float(sSurrGain.slider.getValue());
    p.haasDelayMs   = float(sHaasMs.slider.getValue());
    p.surroundHPF   = float(sSurrHPF.slider.getValue());
    p.sideBlend     = float(sSideBlend.slider.getValue());
    p.midBlend      = float(sMidBlend.slider.getValue());
    p.reverbWet     = float(sReverbWet.slider.getValue());
    p.roomSize      = float(sRoomSize.slider.getValue());
    p.velvetDensity = float(sVelvetDens.slider.getValue());
    return p;
}

// Publishes new UI parameters to the audio thread via lock-free double buffering.
void MainComponent::publishParamsToAudioThread()
{
    const UpmixParams latest = captureParamsFromSliders();

    const int current = juce::jlimit(0, 1, activeParamSlot.load(std::memory_order_relaxed));
    const int inactive = 1 - current;
    paramSlots[(size_t)inactive] = latest;
    activeParamSlot.store(inactive, std::memory_order_release);
}

// Returns compact channel-control mode: 0=normal, 1=mute, 2=solo.
int MainComponent::getChannelControlMode(int ch) const
{
    if (!juce::isPositiveAndBelow(ch, 6))
        return 0;

    const bool solo = soloAtomic[(size_t)ch].load(std::memory_order_relaxed) != 0;
    const bool mute = muteAtomic[(size_t)ch].load(std::memory_order_relaxed) != 0;
    if (solo) return 2;
    if (mute) return 1;
    return 0;
}

// Refreshes one channel-control button text and style.
void MainComponent::refreshChannelControlButton(int ch)
{
    if (!juce::isPositiveAndBelow(ch, 6))
        return;

    const int mode = getChannelControlMode(ch);
    auto& btn = soloBtns[(size_t)ch];
    const juce::String base = channelShortNames[(size_t)ch];
    const auto channelCol = getChannelUiColour(ch);
    const auto darkBase = COL_CARD.interpolatedWith(COL_BG2, 0.48f);

    auto applyChannelButtonColour = [&](juce::Colour fill)
    {
        btn.setColour(juce::TextButton::buttonColourId, fill);
        btn.setColour(juce::TextButton::textColourOffId, COL_TXT.withAlpha(0.95f));
        btn.setColour(juce::TextButton::textColourOnId, COL_TXT);
    };

    if (mode == 2)
    {
        btn.setButtonText(base + " SOLO");
        applyChannelButtonColour(darkBase.interpolatedWith(channelCol, 0.34f).withAlpha(0.86f));
    }
    else if (mode == 1)
    {
        btn.setButtonText(base + " MUTE");
        const auto muteColour = darkBase
            .interpolatedWith(juce::Colour(0xffcc5a6f), 0.20f)
            .interpolatedWith(channelCol, 0.16f);
        applyChannelButtonColour(muteColour.withAlpha(0.82f));
    }
    else
    {
        btn.setButtonText(base);
        applyChannelButtonColour(darkBase.interpolatedWith(channelCol, 0.18f).withAlpha(0.79f));
    }
}

// Applies mode to one channel-control button and corresponding audio-thread atomics.
void MainComponent::setChannelControlMode(int ch, int mode, bool shouldSave)
{
    if (!juce::isPositiveAndBelow(ch, 6))
        return;

    mode = juce::jlimit(0, 2, mode);
    soloAtomic[(size_t)ch].store(mode == 2 ? 1 : 0, std::memory_order_relaxed);
    muteAtomic[(size_t)ch].store(mode == 1 ? 1 : 0, std::memory_order_relaxed);
    refreshChannelControlButton(ch);

    if (shouldSave)
        saveSessionState();
}

// Updates the A/B status text line.
void MainComponent::refreshSnapshotLabel()
{
    const juce::String aState = snapshotA.valid ? "A:ready" : "A:empty";
    const juce::String bState = snapshotB.valid ? "B:ready" : "B:empty";
    snapshotLabel.setText("A/B Snapshots: " + aState + "  |  " + bState, juce::dontSendNotification);
}

// Stores current settings into snapshot A or B.
void MainComponent::storeSnapshot(bool toA)
{
    auto& slot = toA ? snapshotA : snapshotB;
    slot.params = captureParamsFromSliders();
    slot.masterGain = float(masterVol.getValue());
    slot.surroundMode = surround51Active;
    slot.valid = true;

    refreshSnapshotLabel();
    channelImpactLabel.setText("Stored snapshot " + juce::String(toA ? "A" : "B"), juce::dontSendNotification);
}

// Recalls snapshot A or B if available.
void MainComponent::recallSnapshot(bool fromA)
{
    const auto& slot = fromA ? snapshotA : snapshotB;
    if (!slot.valid)
    {
        channelImpactLabel.setText("Snapshot " + juce::String(fromA ? "A" : "B") + " is empty.", juce::dontSendNotification);
        return;
    }

    applyPreset(slot.params);
    masterVol.setValue(slot.masterGain, juce::dontSendNotification);
    masterGainAtomic.store(slot.masterGain, std::memory_order_relaxed);

    surround51Active = slot.surroundMode;
    btnStereo.setToggleState(!surround51Active, juce::dontSendNotification);
    btn51.setToggleState(surround51Active, juce::dontSendNotification);
    engine.reset();

    refreshStatusLabel();
    channelImpactLabel.setText("Recalled snapshot " + juce::String(fromA ? "A" : "B"), juce::dontSendNotification);
    flashChannelFocus({0, 1, 2, 3, 4, 5}, "Snapshot Recall");
    saveSessionState();
}

// Resets all pro metering counters/history.
void MainComponent::resetProMeters()
{
    peakDbAtomic.store(-120.0f, std::memory_order_relaxed);
    lufsApproxAtomic.store(-70.0f, std::memory_order_relaxed);
    clipEventsAtomic.store(0, std::memory_order_relaxed);
    lufsIntegrator = -70.0f;
}

// Applies channel solo/mute matrix to post-processed output.
void MainComponent::applyChannelMasking(juce::AudioBuffer<float>& buffer, int numSamples)
{
    bool anySolo = false;
    std::array<int, 6> solo {};
    std::array<int, 6> mute {};

    for (int ch = 0; ch < 6; ++ch)
    {
        solo[(size_t)ch] = soloAtomic[(size_t)ch].load(std::memory_order_relaxed);
        mute[(size_t)ch] = muteAtomic[(size_t)ch].load(std::memory_order_relaxed);
        anySolo = anySolo || (solo[(size_t)ch] != 0);
    }

    for (int ch = 0; ch < 6; ++ch)
    {
        const bool audible = anySolo ? (solo[(size_t)ch] != 0) : (mute[(size_t)ch] == 0);
        if (!audible)
            buffer.clear(ch, 0, numSamples);
    }
}

// Applies per-channel trim, delay, and polarity calibration.
void MainComponent::applySpeakerCalibration(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0)
        return;

    std::array<float, 6> trim {};
    std::array<float, 6> polarity {};
    std::array<int, 6> delaySamples {};

    for (int ch = 0; ch < 6; ++ch)
    {
        trim[(size_t)ch] = calibTrimGainAtomic[(size_t)ch].load(std::memory_order_relaxed);
        polarity[(size_t)ch] = calibPolarityAtomic[(size_t)ch].load(std::memory_order_relaxed) != 0 ? -1.0f : 1.0f;
        const float delayMs = juce::jmax(0.0f, calibDelayMsAtomic[(size_t)ch].load(std::memory_order_relaxed));
        delaySamples[(size_t)ch] = int(delayMs * float(currentSampleRate) * 0.001f);
    }

    if (speakerDelayCapacity <= 2 || speakerDelayBuffer.getNumSamples() != speakerDelayCapacity)
    {
        for (int ch = 0; ch < 6; ++ch)
            buffer.applyGain(ch, 0, numSamples, trim[(size_t)ch] * polarity[(size_t)ch]);
        return;
    }

    auto wrap = [this](int index)
    {
        while (index < 0) index += speakerDelayCapacity;
        while (index >= speakerDelayCapacity) index -= speakerDelayCapacity;
        return index;
    };

    int writePos = speakerDelayWritePos;
    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < 6; ++ch)
        {
            const float input = buffer.getSample(ch, i) * trim[(size_t)ch] * polarity[(size_t)ch];
            speakerDelayBuffer.setSample(ch, writePos, input);

            const int d = juce::jlimit(0, speakerDelayCapacity - 1, delaySamples[(size_t)ch]);
            const int readPos = wrap(writePos - d);
            buffer.setSample(ch, i, speakerDelayBuffer.getSample(ch, readPos));
        }

        writePos = wrap(writePos + 1);
    }

    speakerDelayWritePos = writePos;
}

// Updates RMS, peak, LUFS-approx, and clip counters from output audio.
void MainComponent::updateMeterStats(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (numSamples <= 0)
        return;

    float blockPeak = 0.0f;
    float stereoSum = 0.0f;

    for (int ch = 0; ch < 6; ++ch)
    {
        const float* data = buffer.getReadPointer(ch);
        float sum = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float v = data[i];
            const float a = std::abs(v);
            sum += v * v;
            blockPeak = juce::jmax(blockPeak, a);
            if (ch < 2)
                stereoSum += v * v;
        }
        meterRms[ch] = std::sqrt(sum / float(numSamples));
    }

    const float blockPeakDb = juce::Decibels::gainToDecibels(blockPeak + 1.0e-9f, -120.0f);
    const float prevPeakDb = peakDbAtomic.load(std::memory_order_relaxed);
    const float heldPeakDb = blockPeakDb >= prevPeakDb ? blockPeakDb : juce::jmax(blockPeakDb, prevPeakDb - 0.8f);
    peakDbAtomic.store(heldPeakDb, std::memory_order_relaxed);

    if (blockPeak >= clipDetectThreshold)
        clipEventsAtomic.fetch_add(1, std::memory_order_relaxed);

    const float stereoRms = std::sqrt(stereoSum / float(juce::jmax(1, numSamples * 2)));
    const float db = juce::Decibels::gainToDecibels(stereoRms + 1.0e-9f, -120.0f);
    const float lufsApprox = db - 0.7f;
    lufsIntegrator = lufsIntegrator * 0.92f + lufsApprox * 0.08f;
    lufsApproxAtomic.store(lufsIntegrator, std::memory_order_relaxed);
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
    saveSessionState();
}

// Preset tuned for stronger movie/cinema impact.
void MainComponent::applyPresetCinema()
{
    activePreset = 0;
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
    activePreset = 1;
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
    activePreset = 2;
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
    activePreset = -1;
    applyPreset(UpmixParams{});

    // Reset channel controls to normal.
    for (int ch = 0; ch < 6; ++ch)
        setChannelControlMode(ch, 0, false);

    // Reset speaker calibration (trim/delay/polarity) to neutral defaults.
    for (int ch = 0; ch < 6; ++ch)
    {
        if (calibTrim[(size_t)ch] != nullptr)
            calibTrim[(size_t)ch]->slider.setValue(0.0, juce::sendNotificationSync);
        if (calibDelay[(size_t)ch] != nullptr)
            calibDelay[(size_t)ch]->slider.setValue(0.0, juce::sendNotificationSync);
        if (calibPolarity[(size_t)ch] != nullptr)
            calibPolarity[(size_t)ch]->setToggleState(false, juce::sendNotificationSync);

        calibTrimGainAtomic[(size_t)ch].store(1.0f, std::memory_order_relaxed);
        calibDelayMsAtomic[(size_t)ch].store(0.0f, std::memory_order_relaxed);
        calibPolarityAtomic[(size_t)ch].store(0, std::memory_order_relaxed);
    }

    saveSessionState();
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
        saveSessionState();
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

// Checks whether a file appears to be one of the currently supported audio formats.
bool MainComponent::isSupportedAudioFile(const juce::File& file) const
{
    if (!file.existsAsFile())
        return false;

    const auto extension = file.getFileExtension()
        .fromLastOccurrenceOf(".", false, false)
        .toLowerCase();

    if (extension.isEmpty())
        return false;

    return formatManager.findFormatForFileExtension(extension) != nullptr;
}

// Shared file-loading path used by chooser and drag-and-drop.
bool MainComponent::loadAudioFileForPlayback(const juce::File& fileToLoad, bool shouldAutoPlay)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(fileToLoad));
    if (reader == nullptr)
    {
        fileLabel.setText("Could not open: " + fileToLoad.getFileName(), juce::dontSendNotification);
        return false;
    }

    const double sourceRate = reader->sampleRate > 0.0 ? reader->sampleRate : currentSampleRate;
    trackLengthSeconds = reader->sampleRate > 0.0
        ? (double(reader->lengthInSamples) / reader->sampleRate)
        : 0.0;

    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    transport.stop();
    transport.setSource(newSource.get(), 0, nullptr, sourceRate);
    readerSource = std::move(newSource);

    loadedFile = fileToLoad;
    fileLoaded = true;
    fileLabel.setText(fileToLoad.getFileName(), juce::dontSendNotification);
    timelineSlider.setValue(0.0, juce::dontSendNotification);

    if (shouldAutoPlay)
    {
        transport.setPosition(0.0);
        transport.start();
        setPlayState(true);
    }
    else
    {
        transport.setPosition(0.0);
        setPlayState(false);
    }

    refreshStatusLabel();
    updateTimelineFromTransport();
    saveSessionState();
    return true;
}

// File drag target: accept drops if at least one file is supported.
bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        if (isSupportedAudioFile(juce::File(path)))
            return true;
    }

    return false;
}

// Shows visual drop feedback while dragging supported files over the window.
void MainComponent::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    dragDropHighlightActive = isInterestedInFileDrag(files);
    if (dragDropHighlightActive)
    {
        channelImpactLabel.setText("Drop audio file to load and play.", juce::dontSendNotification);
        repaint();
    }
}

// Clears drag feedback when cursor leaves the app window.
void MainComponent::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    if (!dragDropHighlightActive)
        return;

    dragDropHighlightActive = false;
    repaint();
}

// Loads the first supported dropped file.
void MainComponent::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    dragDropHighlightActive = false;

    for (const auto& path : files)
    {
        const juce::File dropped(path);
        if (!isSupportedAudioFile(dropped))
            continue;

        if (loadAudioFileForPlayback(dropped, true))
            channelImpactLabel.setText("Loaded via drag and drop: " + dropped.getFileName(), juce::dontSendNotification);
        else
            channelImpactLabel.setText("Could not open dropped file: " + dropped.getFileName(), juce::dontSendNotification);

        repaint();
        return;
    }

    channelImpactLabel.setText("Drop a supported audio file (.wav, .mp3, .flac, .aiff, .ogg, .aac).",
                               juce::dontSendNotification);
    repaint();
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

            loadAudioFileForPlayback(results[0], true);
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
    const UpmixParams paramsSnapshot = captureParamsFromSliders();
    const float masterGainSnapshot = masterGainAtomic.load(std::memory_order_relaxed);

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

            float maxLimiterReduction = 0.0f;
            juce::String error;
            if (!renderFileTo51Wav(loadedFile, target, paramsSnapshot, masterGainSnapshot, maxLimiterReduction, error))
            {
                channelImpactLabel.setText("Export failed: " + error, juce::dontSendNotification);
                return;
            }

            channelImpactLabel.setText("Export complete: " + target.getFileName()
                                        + "  (Limiter max -" + juce::String(maxLimiterReduction, 1) + " dB)",
                                        juce::dontSendNotification);
        });
}

// Shared 5.1 render helper used by single-file and batch export.
bool MainComponent::renderFileTo51Wav(const juce::File& source,
                                      const juce::File& targetIn,
                                      const UpmixParams& paramsSnapshot,
                                      float masterGainSnapshot,
                                      float& maxLimiterReductionDb,
                                      juce::String& errorText)
{
    if (!source.existsAsFile())
    {
        errorText = "source file not found";
        return false;
    }

    auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager.createReaderFor(source));
    if (!reader)
    {
        errorText = "cannot read source file";
        return false;
    }

    juce::File target = targetIn;
    if (!target.hasFileExtension("wav"))
        target = target.withFileExtension(".wav");

    if (target.existsAsFile() && !target.deleteFile())
    {
        errorText = "cannot overwrite target file";
        return false;
    }

    auto fileOut = target.createOutputStream();
    if (!fileOut || !fileOut->openedOk())
    {
        errorText = "cannot create output file";
        return false;
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
        errorText = "WAV writer init error";
        return false;
    }

    constexpr int exportBlockSize = 2048;
    const int64 totalSamples = reader->lengthInSamples;

    UpmixEngine exportEngine;
    exportEngine.params = paramsSnapshot;
    exportEngine.prepare(reader->sampleRate, exportBlockSize);

    juce::AudioBuffer<float> stereoBlock(2, exportBlockSize);
    juce::AudioBuffer<float> surroundBlock(6, exportBlockSize);
    float exportLimiterGain = 1.0f;
    float exportLimiterReduction = 0.0f;
    int exportLimiterHold = 0;
    maxLimiterReductionDb = 0.0f;
    const float exportReleaseCoeff = std::exp(-1.0f / float(juce::jmax(1.0, reader->sampleRate * 0.2)));

    for (int64 readPos = 0; readPos < totalSamples; readPos += exportBlockSize)
    {
        const int numThisBlock = int(juce::jmin<int64>(exportBlockSize, totalSamples - readPos));

        stereoBlock.clear();
        surroundBlock.clear();

        if (!reader->read(&stereoBlock, 0, numThisBlock, readPos, true, true))
        {
            writer.reset();
            target.deleteFile();
            errorText = "read error during render";
            return false;
        }

        exportEngine.process(stereoBlock, surroundBlock, numThisBlock, true);

        if (masterGainSnapshot != 1.0f)
            surroundBlock.applyGain(0, numThisBlock, masterGainSnapshot);

        applySafetyLimiter(surroundBlock, 6, numThisBlock, limiterThreshold, exportReleaseCoeff,
                           exportLimiterGain, exportLimiterReduction, exportLimiterHold);
        maxLimiterReductionDb = juce::jmax(maxLimiterReductionDb, exportLimiterReduction);

        if (!writer->writeFromAudioSampleBuffer(surroundBlock, 0, numThisBlock))
        {
            writer.reset();
            target.deleteFile();
            errorText = "write error during render";
            return false;
        }
    }

    writer.reset();
    return true;
}

// Batch exports multiple source files to 5.1 WAV in a selected folder.
void MainComponent::exportBatchTo51Wav()
{
    batchSourceChooser = std::make_unique<juce::FileChooser>(
        "Select source audio files for batch export",
        loadedFile.existsAsFile() ? loadedFile.getParentDirectory() : juce::File{},
        "*.wav;*.mp3;*.flac;*.aiff;*.ogg;*.aac");

    batchSourceChooser->launchAsync(juce::FileBrowserComponent::openMode
                                    | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& chooser)
        {
            auto sources = chooser.getResults();
            if (sources.isEmpty())
                return;

            batchPendingFiles = sources;
            batchTargetChooser = std::make_unique<juce::FileChooser>(
                "Select output folder for batch export",
                loadedFile.existsAsFile() ? loadedFile.getParentDirectory() : juce::File{},
                juce::String{},
                true);

            batchTargetChooser->launchAsync(juce::FileBrowserComponent::openMode
                                            | juce::FileBrowserComponent::canSelectDirectories,
                [this](const juce::FileChooser& destChooser)
                {
                    auto folder = destChooser.getResult();
                    if (folder == juce::File{} || !folder.isDirectory())
                        return;

                    const UpmixParams paramsSnapshot = captureParamsFromSliders();
                    const float masterGainSnapshot = masterGainAtomic.load(std::memory_order_relaxed);

                    int okCount = 0;
                    int failCount = 0;
                    for (const auto& source : batchPendingFiles)
                    {
                        const auto target = folder.getChildFile(source.getFileNameWithoutExtension() + "_upmix_5_1.wav");
                        float limiterMax = 0.0f;
                        juce::String error;
                        if (renderFileTo51Wav(source, target, paramsSnapshot, masterGainSnapshot, limiterMax, error))
                            ++okCount;
                        else
                            ++failCount;
                    }

                    channelImpactLabel.setText("Batch export complete. Success: "
                                               + juce::String(okCount)
                                               + ", Failed: " + juce::String(failCount),
                                               juce::dontSendNotification);
                });
        });
}

// Updates play button text + callback so one button handles Play/Pause.
void MainComponent::setPlayState(bool playing)
{
    playBtn.setButtonText(playing ? "Pause" : "Play");
    playBtn.onClick = [this, playing] {
        if (playing) { transport.stop(); setPlayState(false); saveSessionState(); }
        else if (fileLoaded)
        {
            transport.start();
            setPlayState(true);
            saveSessionState();
        }
    };
}

// Draws the main panel, background gradients, and top banner text.
void MainComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Slightly darker premium gradient background.
    juce::ColourGradient bg(COL_BG0.darker(0.08f), 0.f, 0.f,
                            COL_BG1.darker(0.04f), 0.f, bounds.getBottom(), false);
    g.setGradientFill(bg);
    g.fillRect(bounds);

    // Ambient glows for depth without overpowering the layout.
    g.setColour(COL_ACC.withAlpha(0.09f));
    g.fillEllipse(-120.0f, -70.0f, 380.0f, 250.0f);
    g.setColour(COL_PRP.withAlpha(0.07f));
    g.fillEllipse(bounds.getRight() - 300.0f, -80.0f, 380.0f, 250.0f);
    g.setColour(COL_ORG.withAlpha(0.06f));
    g.fillEllipse(bounds.getCentreX() - 240.0f, bounds.getBottom() - 190.0f, 480.0f, 250.0f);

    // Subtle dot grid texture overlay
    g.setColour(juce::Colours::white.withAlpha(0.014f));
    const int gridSpacing = 28;
    for (int y = 0; y < getHeight(); y += gridSpacing)
        for (int x = 0; x < getWidth(); x += gridSpacing)
            g.fillRect(x, y, 1, 1);

    // Main shell card
    auto shell = bounds.reduced(8.0f);
    juce::ColourGradient shellBg(COL_PANEL.withAlpha(0.92f), shell.getX(), shell.getY(),
                                  COL_BG0.withAlpha(0.95f), shell.getX(), shell.getBottom(), false);
    g.setGradientFill(shellBg);
    g.fillRoundedRectangle(shell, 12.0f);

    // Decorative symbols/shapes inside the shell background.
    // They stay subtle so the controls remain readable.
    g.saveState();
    g.reduceClipRegion(shell.reduced(2.0f).toNearestInt());

    const auto symA = COL_ACC.withAlpha(0.07f);
    const auto symB = COL_PRP.withAlpha(0.06f);
    const auto symC = COL_ORG.withAlpha(0.055f);

    // Concentric rings.
    g.setColour(symA);
    g.drawEllipse(shell.getRight() - 250.0f, shell.getY() + 72.0f, 170.0f, 170.0f, 1.4f);
    g.drawEllipse(shell.getRight() - 220.0f, shell.getY() + 102.0f, 110.0f, 110.0f, 1.0f);
    g.setColour(symB);
    g.drawEllipse(shell.getX() + 12.0f, shell.getBottom() - 190.0f, 150.0f, 150.0f, 1.2f);

    // Triangle and diamond outlines.
    juce::Path tri;
    tri.startNewSubPath(shell.getX() + 78.0f, shell.getY() + 120.0f);
    tri.lineTo(shell.getX() + 128.0f, shell.getY() + 208.0f);
    tri.lineTo(shell.getX() + 28.0f, shell.getY() + 208.0f);
    tri.closeSubPath();
    g.setColour(symC);
    g.strokePath(tri, juce::PathStrokeType(1.1f));

    juce::Path diamond;
    const auto dx = shell.getRight() - 110.0f;
    const auto dy = shell.getBottom() - 140.0f;
    diamond.startNewSubPath(dx, dy - 26.0f);
    diamond.lineTo(dx + 26.0f, dy);
    diamond.lineTo(dx, dy + 26.0f);
    diamond.lineTo(dx - 26.0f, dy);
    diamond.closeSubPath();
    g.setColour(symB);
    g.strokePath(diamond, juce::PathStrokeType(1.0f));

    // Audio-wave line near the footer.
    juce::Path wave;
    const float waveY = shell.getBottom() - 78.0f;
    wave.startNewSubPath(shell.getX() + 38.0f, waveY);
    for (float x = shell.getX() + 38.0f; x <= shell.getRight() - 38.0f; x += 6.0f)
        wave.lineTo(x, waveY + std::sin((x - shell.getX()) * 0.030f) * 7.0f);
    g.setColour(COL_ACC.withAlpha(0.055f));
    g.strokePath(wave, juce::PathStrokeType(1.0f));

    // Tiny marker crosses.
    auto drawCross = [&](float cx, float cy, float r, juce::Colour c)
    {
        g.setColour(c);
        g.drawLine(cx - r, cy, cx + r, cy, 1.0f);
        g.drawLine(cx, cy - r, cx, cy + r, 1.0f);
    };
    drawCross(shell.getX() + 196.0f, shell.getY() + 98.0f, 5.0f, symA);
    drawCross(shell.getRight() - 176.0f, shell.getY() + 276.0f, 4.0f, symC);
    drawCross(shell.getX() + 280.0f, shell.getBottom() - 120.0f, 4.0f, symB);

    g.restoreState();

    // Shell outer glow (accent blue tint)
    g.setColour(COL_ACC.withAlpha(0.07f));
    g.drawRoundedRectangle(shell.expanded(1.0f), 13.0f, 2.0f);
    // Shell border
    g.setColour(COL_BRDR.withAlpha(0.75f));
    g.drawRoundedRectangle(shell, 12.0f, 1.0f);

    // Header area: horizontal separator line below title
    auto headerArea = shell.withHeight(62.0f);
    auto headerBand = headerArea.reduced(2.0f, 1.5f);
    juce::ColourGradient headerGrad(juce::Colour(0xff0f3a4a).withAlpha(0.88f),
                                    headerBand.getX(), headerBand.getY(),
                                    juce::Colour(0xff3f2418).withAlpha(0.86f),
                                    headerBand.getRight(), headerBand.getY(), false);
    headerGrad.addColour(0.58, juce::Colour(0xff172a3d).withAlpha(0.88f));
    g.setGradientFill(headerGrad);
    g.fillRoundedRectangle(headerBand, 10.0f);
    g.setColour(COL_BRDR.withAlpha(0.55f));
    g.drawRoundedRectangle(headerBand, 10.0f, 0.8f);
    // Thin accent line at top of shell
    juce::ColourGradient topAccent(COL_ACC.withAlpha(0.62f), shell.getX() + 20.0f, shell.getY(),
                                   COL_PRP.withAlpha(0.52f), shell.getX() + 95.0f, shell.getY(), false);
    g.setGradientFill(topAccent);
    g.fillRect(shell.getX() + 20.0f, shell.getY(), 75.0f, 2.0f);
    // Separator below header
    g.setColour(COL_BRDR.withAlpha(0.45f));
    g.drawLine(shell.getX() + 12.0f, shell.getY() + 62.0f,
               shell.getRight() - 12.0f, shell.getY() + 62.0f, 0.7f);
    // Title text
    auto titleArea = juce::Rectangle<float>(shell.getX() + 16.0f, shell.getY() + 9.0f,
                                            shell.getWidth() - 32.0f, 26.0f);
    g.setColour(juce::Colours::white.withAlpha(0.96f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 19.0f, juce::Font::bold)));
    g.drawText("Surround 5.1 Upmixer", titleArea.toNearestInt(), juce::Justification::centredLeft);

    auto subtitleArea = titleArea.withY(titleArea.getBottom() + 10.0f).withHeight(14.0f);
    g.setColour(COL_MUT.withAlpha(0.75f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 12.0f, juce::Font::plain)));
    g.drawText("Discrete hardware output  |  Psychoacoustic upmixing  |  Live tuning",
               subtitleArea.toNearestInt(), juce::Justification::centredLeft);

    // Author credit (right side of header)
    auto signatureArea = juce::Rectangle<int>(int(shell.getRight()) - 280, int(shell.getY() + 10), 264, 13);
    g.setColour(COL_MUT.withAlpha(0.40f));
    g.setFont(juce::Font(juce::FontOptions("Consolas", 12.0f, juce::Font::plain)));
    g.drawText("by Mohamed Moslem Allouch", signatureArea, juce::Justification::centredRight);

    // Draw frosted-glass section panes behind UI groups
    auto drawPane = [&](juce::Rectangle<int> r, bool highlight = false)
    {
        if (r.isEmpty()) return;
        auto b = r.expanded(4, 4).toFloat();
        juce::ColourGradient pane(COL_BG2.withAlpha(0.40f), b.getX(), b.getY(),
                                  COL_BG0.withAlpha(0.58f), b.getX(), b.getBottom(), false);
        g.setGradientFill(pane);
        g.fillRoundedRectangle(b, 9.0f);
        if (highlight)
            g.setColour(COL_ACC.withAlpha(0.18f));
        else
            g.setColour(COL_BRDR.withAlpha(0.55f));
        g.drawRoundedRectangle(b, 9.0f, 0.9f);
        if (highlight)
        {
            g.setColour(COL_PRP.withAlpha(0.10f));
            g.fillRoundedRectangle(b.reduced(1.0f), 8.0f);
        }
        // Inner sheen
        g.setColour(juce::Colours::white.withAlpha(0.028f));
        g.drawRoundedRectangle(b.reduced(1.0f), 8.0f, 0.7f);
    };

    // Mode toggle pane (highlight it)
    drawPane(btnStereo.getBounds().getUnion(btn51.getBounds()), true);
    // File/transport row
    drawPane(fileBtn.getBounds()
             .getUnion(exportBtn.getBounds())
             .getUnion(batchExportBtn.getBounds())
             .getUnion(settingsBtn.getBounds())
             .getUnion(fileLabel.getBounds())
             .getUnion(stopBtn.getBounds())
             .getUnion(playBtn.getBounds()));
    // Status bar (inline, no pane - just divider lines)
    // Preset pane
    drawPane(presetLabel.getBounds()
             .getUnion(presetCinema.getBounds())
             .getUnion(presetMusic.getBounds())
             .getUnion(presetVocal.getBounds())
             .getUnion(presetReset.getBounds())
             .getUnion(presetSave.getBounds())
             .getUnion(presetLoad.getBounds())
             .getUnion(snapshotStoreA.getBounds())
             .getUnion(snapshotStoreB.getBounds())
             .getUnion(snapshotRecallA.getBounds())
             .getUnion(snapshotRecallB.getBounds())
             .getUnion(snapshotLabel.getBounds()));
    // Effect chain label (no border, inline)
    // Channel solo/mute row
    drawPane(channelControlLabel.getBounds()
             .getUnion(soloBtns[0].getBounds()).getUnion(soloBtns[1].getBounds())
             .getUnion(soloBtns[2].getBounds()).getUnion(soloBtns[3].getBounds())
             .getUnion(soloBtns[4].getBounds()).getUnion(soloBtns[5].getBounds()));
    // Timeline pane
    drawPane(timelineSlider.getBounds().getUnion(timelineLabel.getBounds()));
    // Spectrum pane
    drawPane(spectrum.getBounds());
    // Master volume
    drawPane(masterVolLabel.getBounds()
             .getUnion(masterVol.getBounds())
             .getUnion(limiterLabel.getBounds())
             .getUnion(meterStatsLabel.getBounds())
             .getUnion(resetMeterStatsBtn.getBounds()));
    // Meters
    auto meterBounds = mFL.getBounds().getUnion(mFR.getBounds())
        .getUnion(mFC.getBounds()).getUnion(mLFE.getBounds())
        .getUnion(mSL.getBounds()).getUnion(mSR.getBounds());
    drawPane(meterBounds);
    // Params viewport
    drawPane(paramsViewport.getBounds());
    // Channel scope
    drawPane(channelScope.getBounds());

    // Drag-and-drop overlay.
    if (dragDropHighlightActive)
    {
        auto dropArea = shell.reduced(20.0f).withTrimmedTop(70.0f).withTrimmedBottom(14.0f);
        g.setColour(COL_ACC.withAlpha(0.13f));
        g.fillRoundedRectangle(dropArea, 12.0f);

        g.setColour(COL_ACC.withAlpha(0.75f));
        g.drawRoundedRectangle(dropArea, 12.0f, 1.6f);

        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(juce::Font(juce::FontOptions("Consolas", 17.0f, juce::Font::bold)));
        g.drawFittedText("Drop Audio File Here",
                         dropArea.toNearestInt().reduced(12, 20),
                         juce::Justification::centred,
                         1);

        g.setColour(COL_MUT.brighter(0.35f));
        g.setFont(juce::Font(juce::FontOptions("Consolas", 12.5f, juce::Font::plain)));
        g.drawFittedText("Supported: WAV, MP3, FLAC, AIFF, OGG, AAC",
                         dropArea.toNearestInt().reduced(12, 52),
                         juce::Justification::centredBottom,
                         1);
    }
}

// Lays out all controls, meters, visualizers, and scrollable parameter panels.
void MainComponent::resized()
{
    constexpr int gap = 7;
    auto root = getLocalBounds().reduced(12);

    // Reserved for title/subtitle text drawn in paint().
    root.removeFromTop(66);
    root.removeFromTop(gap);

    const int scopeH = juce::jlimit(42, 64, getHeight() / 14);
    auto scopeArea = root.removeFromBottom(scopeH);
    channelScope.setBounds(scopeArea);
    root.removeFromBottom(gap);

    const int minTop = 248;
    const int minParams = 190;
    const int maxTop = juce::jmax(minTop, root.getHeight() - minParams - gap);
    const int topH = juce::jlimit(minTop, maxTop, int(float(root.getHeight()) * 0.62f));
    auto top = root.removeFromTop(topH);
    root.removeFromTop(3);

    // File/transport row
    auto fileRow = top.removeFromTop(34);
    fileBtn.setBounds(fileRow.removeFromLeft(108));
    fileRow.removeFromLeft(gap);
    exportBtn.setBounds(fileRow.removeFromLeft(138));
    fileRow.removeFromLeft(gap);
    batchExportBtn.setBounds(fileRow.removeFromLeft(132));
    fileRow.removeFromLeft(gap);
    settingsBtn.setBounds(fileRow.removeFromLeft(124));
    fileRow.removeFromLeft(gap);
    playBtn.setBounds(fileRow.removeFromRight(88));
    fileRow.removeFromRight(gap);
    stopBtn.setBounds(fileRow.removeFromRight(78));
    fileRow.removeFromRight(gap);
    fileLabel.setBounds(fileRow);

    // Extra breathing room between channel controls and timeline/master area.
    top.removeFromTop(gap + 3);
    // Mode row
    auto modeRow = top.removeFromTop(46);
    auto leftMode = modeRow.removeFromLeft((modeRow.getWidth() - gap) / 2);
    modeRow.removeFromLeft(gap);
    btnStereo.setBounds(leftMode);
    btn51.setBounds(modeRow);

    top.removeFromTop(gap);
    // Status bar (inline, no background pane)
    statusLabel.setBounds(top.removeFromTop(18));

    top.removeFromTop(gap);
    // Preset row
    presetLabel.setBounds(top.removeFromTop(14));
    top.removeFromTop(3);
    auto presetRow = top.removeFromTop(26);
    const int presetW = (presetRow.getWidth() - 3 * gap) / 4;
    presetCinema.setBounds(presetRow.removeFromLeft(presetW));
    presetRow.removeFromLeft(gap);
    presetMusic.setBounds(presetRow.removeFromLeft(presetW));
    presetRow.removeFromLeft(gap);
    presetVocal.setBounds(presetRow.removeFromLeft(presetW));
    presetRow.removeFromLeft(gap);
    presetReset.setBounds(presetRow);

    top.removeFromTop(3);
    auto presetUtilityRow = top.removeFromTop(24);
    const int utilW = (presetUtilityRow.getWidth() - gap) / 2;
    presetSave.setBounds(presetUtilityRow.removeFromLeft(utilW));
    presetUtilityRow.removeFromLeft(gap);
    presetLoad.setBounds(presetUtilityRow);

    top.removeFromTop(3);
    auto snapshotRow = top.removeFromTop(23);
    const int snapW = (snapshotRow.getWidth() - 3 * gap) / 4;
    snapshotStoreA.setBounds(snapshotRow.removeFromLeft(snapW));
    snapshotRow.removeFromLeft(gap);
    snapshotRecallA.setBounds(snapshotRow.removeFromLeft(snapW));
    snapshotRow.removeFromLeft(gap);
    snapshotStoreB.setBounds(snapshotRow.removeFromLeft(snapW));
    snapshotRow.removeFromLeft(gap);
    snapshotRecallB.setBounds(snapshotRow);

    top.removeFromTop(3);
    snapshotLabel.setBounds(top.removeFromTop(15));

    top.removeFromTop(gap);
    // Effect chain label
    channelImpactLabel.setBounds(top.removeFromTop(18));

    top.removeFromTop(3);
    channelControlLabel.setBounds(top.removeFromTop(14));
    top.removeFromTop(3);
    auto soloRow = top.removeFromTop(22);
    const int chBtnW = (soloRow.getWidth() - gap * 5) / 6;
    for (int ch = 0; ch < 6; ++ch)
    {
        soloBtns[(size_t)ch].setBounds(soloRow.removeFromLeft(chBtnW));
        if (ch < 5)
            soloRow.removeFromLeft(gap);
    }

    // Keep enough visual separation before timeline/master controls.
    top.removeFromTop(gap + 2);
    // Timeline row
    auto timelineRow = top.removeFromTop(20);
    timelineLabel.setBounds(timelineRow.removeFromRight(126));
    timelineRow.removeFromRight(gap);
    timelineSlider.setBounds(timelineRow);

    top.removeFromTop(3);
    // Meters row at the bottom of top section
    auto meterRow = top.removeFromBottom(juce::jlimit(42, 58, top.getHeight() / 3));
    top.removeFromBottom(gap);
    auto meterStatsRow = top.removeFromBottom(20);
    top.removeFromBottom(3);
    // Master volume row
    auto volRow = top.removeFromBottom(20);
    top.removeFromBottom(3);
    // Spectrum fills the remaining top section (no empty gap)
    spectrum.setBounds(top);

    resetMeterStatsBtn.setBounds(meterStatsRow.removeFromRight(112));
    meterStatsRow.removeFromRight(gap);
    meterStatsLabel.setBounds(meterStatsRow);

    masterVolLabel.setBounds(volRow.removeFromLeft(108));
    volRow.removeFromLeft(gap);
    limiterLabel.setBounds(volRow.removeFromRight(150));
    volRow.removeFromRight(gap);
    masterVol.setBounds(volRow);

    const int meterGap = 5;
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
    const int contentWidth = juce::jmax(520, paramsViewport.getWidth() - scrollThickness - 4);
    const int xPad = 8;
    const int colGap = 12;
    const int sectionGap = 10;
    const int sectionPad = 12;
    const int titleH = 20;
    const int sliderH = 36;
    const int sliderGap = 5;
    const bool oneColumn = contentWidth < 760;
    const int usableWidth = contentWidth - xPad * 2;
    const int colWidth = oneColumn ? usableWidth : (usableWidth - colGap) / 2;
    int y = xPad;

    auto placeSection = [&](int x, int yPos, juce::Label& title, std::initializer_list<ParamSlider*> sliders)
    {
        int yy = yPos + sectionPad - 2;
        title.setBounds(x + sectionPad, yy, colWidth - sectionPad * 2, titleH);
        yy += titleH + 6;

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
    juce::Rectangle<int> calibCard;

    auto placeCalibrationSection = [&](int x, int yPos, int width)
    {
        int yy = yPos + sectionPad - 2;
        lblCalib.setBounds(x + sectionPad, yy, width - sectionPad * 2, titleH);
        yy += titleH + 6;

        for (int ch = 0; ch < 6; ++ch)
        {
            auto row = juce::Rectangle<int>(x + sectionPad, yy, width - sectionPad * 2, sliderH);
            const int half = (row.getWidth() - colGap) / 2;
            calibTrim[(size_t)ch]->setBounds(row.removeFromLeft(half));
            row.removeFromLeft(colGap);
            calibDelay[(size_t)ch]->setBounds(row);
            yy += sliderH + 1;

            calibPolarity[(size_t)ch]->setBounds(x + sectionPad, yy, width - sectionPad * 2, 18);
            yy += 18 + sliderGap;
        }

        yy += sectionPad - sliderGap;
        return juce::Rectangle<int>(x, yPos, width, yy - yPos);
    };

    if (oneColumn)
    {
        frontCard = placeSection(xPad, y, lblFront, { &sFrontGain, &sCenterGain, &sCenterHPF });
        y = frontCard.getBottom() + sectionGap;
        lfeCard = placeSection(xPad, y, lblLFE, { &sLFEGain, &sLFEHz, &sLFEShelf, &sExciter });
        y = lfeCard.getBottom() + sectionGap;
        surroundCard = placeSection(xPad, y, lblSurround, { &sSurrGain, &sHaasMs, &sSurrHPF, &sSideBlend, &sMidBlend });
        y = surroundCard.getBottom() + sectionGap;
        spaceCard = placeSection(xPad, y, lblSpace, { &sReverbWet, &sRoomSize, &sVelvetDens });
        y = spaceCard.getBottom() + sectionGap;
        calibCard = placeCalibrationSection(xPad, y, colWidth);
        y = calibCard.getBottom() + xPad;
    }
    else
    {
        const int rightX = xPad + colWidth + colGap;
        frontCard = placeSection(xPad, y, lblFront, { &sFrontGain, &sCenterGain, &sCenterHPF });
        lfeCard = placeSection(rightX, y, lblLFE, { &sLFEGain, &sLFEHz, &sLFEShelf, &sExciter });
        y = juce::jmax(frontCard.getBottom(), lfeCard.getBottom()) + sectionGap;

        surroundCard = placeSection(xPad, y, lblSurround, { &sSurrGain, &sHaasMs, &sSurrHPF, &sSideBlend, &sMidBlend });
        spaceCard = placeSection(rightX, y, lblSpace, { &sReverbWet, &sRoomSize, &sVelvetDens });
        y = juce::jmax(surroundCard.getBottom(), spaceCard.getBottom()) + sectionGap;
        calibCard = placeCalibrationSection(xPad, y, usableWidth);
        y = calibCard.getBottom() + xPad;
    }

    paramsContent.setCards(frontCard, lfeCard, surroundCard, spaceCard, calibCard);
    paramsContent.setSize(contentWidth, y);
}

// Returns the app state directory under the current user profile.
juce::File MainComponent::getStateDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Surround51Upmixer");
}

// Returns the JSON file used to persist app session state.
juce::File MainComponent::getSessionFile() const
{
    return getStateDirectory().getChildFile("session.json");
}

// Returns the JSON file used for default preset persistence.
juce::File MainComponent::getDefaultPresetFile() const
{
    return getStateDirectory().getChildFile("default-preset.json");
}

// Writes the current slider configuration to a preset JSON file.
bool MainComponent::savePresetToFile(const juce::File& file)
{
    auto* rootObj = new juce::DynamicObject();
    rootObj->setProperty("schemaVersion", 1);
    rootObj->setProperty("savedAtUtc", juce::Time::getCurrentTime().toISO8601(true));
    rootObj->setProperty("activePreset", activePreset);
    rootObj->setProperty("masterGain", float(masterVol.getValue()));
    rootObj->setProperty("surround51Active", surround51Active);
    rootObj->setProperty("params", upmixParamsToVar(captureParamsFromSliders()));
    return writeJsonFile(file, juce::var(rootObj));
}

// Loads a preset JSON and applies it to UI and audio processing.
bool MainComponent::loadPresetFromFile(const juce::File& file)
{
    juce::var rootVar;
    if (!readJsonFile(file, rootVar))
        return false;

    const UpmixParams fallback = captureParamsFromSliders();
    const juce::var paramsVar = rootVar.getDynamicObject() != nullptr
        ? rootVar.getDynamicObject()->getProperty("params")
        : juce::var();

    const UpmixParams loaded = paramsVar.isVoid()
        ? upmixParamsFromVar(rootVar, fallback)
        : upmixParamsFromVar(paramsVar, fallback);

    applyPreset(loaded);
    const float storedMaster = juce::jlimit(0.0f, 1.5f, readFloatProperty(rootVar, "masterGain", float(masterVol.getValue())));
    masterVol.setValue(storedMaster, juce::dontSendNotification);
    masterGainAtomic.store(storedMaster, std::memory_order_relaxed);
    surround51Active = readBoolProperty(rootVar, "surround51Active", surround51Active);
    btnStereo.setToggleState(!surround51Active, juce::dontSendNotification);
    btn51.setToggleState(surround51Active, juce::dontSendNotification);
    activePreset = juce::jlimit(-1, 2, readIntProperty(rootVar, "activePreset", activePreset));
    saveSessionState();
    return true;
}

// Saves app session so the next launch resumes where the user left off.
void MainComponent::saveSessionState()
{
    if (suppressSessionPersistence)
        return;

    auto* rootObj = new juce::DynamicObject();
    rootObj->setProperty("schemaVersion", 1);
    rootObj->setProperty("savedAtUtc", juce::Time::getCurrentTime().toISO8601(true));
    rootObj->setProperty("fileLoaded", fileLoaded && loadedFile.existsAsFile());
    rootObj->setProperty("loadedFile", loadedFile.getFullPathName());
    rootObj->setProperty("transportPositionSec", transport.getCurrentPosition());
    rootObj->setProperty("transportPlaying", transport.isPlaying());
    rootObj->setProperty("surround51Active", surround51Active);
    rootObj->setProperty("masterGain", float(masterVol.getValue()));
    rootObj->setProperty("activePreset", activePreset);
    rootObj->setProperty("lastOutputDevice", lastKnownOutputDeviceName);
    rootObj->setProperty("params", upmixParamsToVar(captureParamsFromSliders()));

    for (int ch = 0; ch < 6; ++ch)
    {
        rootObj->setProperty("solo_" + juce::String(channelShortNames[(size_t)ch]),
                             soloAtomic[(size_t)ch].load(std::memory_order_relaxed) != 0);
        rootObj->setProperty("mute_" + juce::String(channelShortNames[(size_t)ch]),
                             muteAtomic[(size_t)ch].load(std::memory_order_relaxed) != 0);
        if (calibTrim[(size_t)ch] != nullptr)
            rootObj->setProperty("calibTrimDb_" + juce::String(channelShortNames[(size_t)ch]),
                                 float(calibTrim[(size_t)ch]->slider.getValue()));
        if (calibDelay[(size_t)ch] != nullptr)
            rootObj->setProperty("calibDelayMs_" + juce::String(channelShortNames[(size_t)ch]),
                                 float(calibDelay[(size_t)ch]->slider.getValue()));
        if (calibPolarity[(size_t)ch] != nullptr)
            rootObj->setProperty("calibInvert_" + juce::String(channelShortNames[(size_t)ch]),
                                 calibPolarity[(size_t)ch]->getToggleState());
    }

    auto makeSnapshotVar = [](const SnapshotState& s)
    {
        auto* snapObj = new juce::DynamicObject();
        snapObj->setProperty("valid", s.valid);
        snapObj->setProperty("masterGain", s.masterGain);
        snapObj->setProperty("surroundMode", s.surroundMode);
        snapObj->setProperty("params", upmixParamsToVar(s.params));
        return juce::var(snapObj);
    };
    rootObj->setProperty("snapshotA", makeSnapshotVar(snapshotA));
    rootObj->setProperty("snapshotB", makeSnapshotVar(snapshotB));

    juce::ignoreUnused(writeJsonFile(getSessionFile(), juce::var(rootObj)));
}

// Restores session fields (file, transport position/state, mode, and parameters).
void MainComponent::restoreSessionState()
{
    juce::ScopedValueSetter<bool> guard(suppressSessionPersistence, true);

    juce::var rootVar;
    if (!readJsonFile(getSessionFile(), rootVar))
        return;

    surround51Active = readBoolProperty(rootVar, "surround51Active", surround51Active);
    btnStereo.setToggleState(!surround51Active, juce::dontSendNotification);
    btn51.setToggleState(surround51Active, juce::dontSendNotification);

    const float restoredMaster = juce::jlimit(0.0f, 1.5f, readFloatProperty(rootVar, "masterGain", 1.0f));
    masterVol.setValue(restoredMaster, juce::dontSendNotification);
    masterGainAtomic.store(restoredMaster, std::memory_order_relaxed);

    const UpmixParams fallback = captureParamsFromSliders();
    const juce::var paramsVar = rootVar.getDynamicObject() != nullptr
        ? rootVar.getDynamicObject()->getProperty("params")
        : juce::var();

    const UpmixParams loaded = paramsVar.isVoid()
        ? upmixParamsFromVar(rootVar, fallback)
        : upmixParamsFromVar(paramsVar, fallback);

    applyPreset(loaded);
    activePreset = juce::jlimit(-1, 2, readIntProperty(rootVar, "activePreset", activePreset));

    for (int ch = 0; ch < 6; ++ch)
    {
        const bool solo = readBoolProperty(rootVar, "solo_" + juce::String(channelShortNames[(size_t)ch]), false);
        const bool mute = readBoolProperty(rootVar, "mute_" + juce::String(channelShortNames[(size_t)ch]), false);
        const int mode = solo ? 2 : (mute ? 1 : 0);
        setChannelControlMode(ch, mode, false);

        const float trimDb = readFloatProperty(rootVar, "calibTrimDb_" + juce::String(channelShortNames[(size_t)ch]), 0.0f);
        const float delayMs = readFloatProperty(rootVar, "calibDelayMs_" + juce::String(channelShortNames[(size_t)ch]), 0.0f);
        const bool inv = readBoolProperty(rootVar, "calibInvert_" + juce::String(channelShortNames[(size_t)ch]), false);

        if (calibTrim[(size_t)ch] != nullptr)
            calibTrim[(size_t)ch]->slider.setValue(trimDb, juce::sendNotificationSync);
        if (calibDelay[(size_t)ch] != nullptr)
            calibDelay[(size_t)ch]->slider.setValue(delayMs, juce::sendNotificationSync);
        if (calibPolarity[(size_t)ch] != nullptr)
            calibPolarity[(size_t)ch]->setToggleState(inv, juce::sendNotificationSync);
    }

    auto restoreSnapshot = [](const juce::var& root, const juce::Identifier& id, SnapshotState& outState)
    {
        if (const auto* rootObj = root.getDynamicObject())
        {
            const auto snapVar = rootObj->getProperty(id);
            outState.valid = readBoolProperty(snapVar, "valid", false);
            outState.masterGain = readFloatProperty(snapVar, "masterGain", 1.0f);
            outState.surroundMode = readBoolProperty(snapVar, "surroundMode", false);

            const juce::var pVar = snapVar.getDynamicObject() != nullptr
                ? snapVar.getDynamicObject()->getProperty("params")
                : juce::var();
            outState.params = upmixParamsFromVar(pVar, UpmixParams{});
        }
    };

    restoreSnapshot(rootVar, "snapshotA", snapshotA);
    restoreSnapshot(rootVar, "snapshotB", snapshotB);
    refreshSnapshotLabel();

    const bool wantsFile = readBoolProperty(rootVar, "fileLoaded", false);
    const juce::String filePath = readStringProperty(rootVar, "loadedFile", {});
    if (wantsFile && filePath.isNotEmpty())
    {
        const juce::File restoredFile(filePath);
        if (restoredFile.existsAsFile())
        {
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(restoredFile));
            if (reader)
            {
                const double sourceRate = reader->sampleRate;
                trackLengthSeconds = reader->sampleRate > 0.0
                    ? (double(reader->lengthInSamples) / reader->sampleRate)
                    : 0.0;

                auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
                transport.stop();
                transport.setSource(newSource.get(), 0, nullptr, sourceRate > 0.0 ? sourceRate : currentSampleRate);
                readerSource = std::move(newSource);
                loadedFile = restoredFile;
                fileLoaded = true;
                fileLabel.setText(restoredFile.getFileName(), juce::dontSendNotification);
            }
        }
    }

    if (!fileLoaded)
    {
        fileLabel.setText("  No file loaded", juce::dontSendNotification);
        setPlayState(false);
        refreshStatusLabel();
        updateTimelineFromTransport();
        refreshSnapshotLabel();
        return;
    }

    const double savedPosition = juce::jlimit(0.0, juce::jmax(0.0, trackLengthSeconds),
                                              double(readFloatProperty(rootVar, "transportPositionSec", 0.0f)));
    transport.setPosition(savedPosition);

    const bool shouldResumePlayback = readBoolProperty(rootVar, "transportPlaying", false);
    if (shouldResumePlayback)
    {
        transport.start();
        setPlayState(true);
    }
    else
    {
        transport.stop();
        setPlayState(false);
    }

    refreshStatusLabel();
    updateTimelineFromTransport();
    refreshSnapshotLabel();
}

// Saves the current settings as the app's default preset.
void MainComponent::saveDefaultPreset()
{
    juce::ignoreUnused(savePresetToFile(getDefaultPresetFile()));
}

// Loads default preset if available; otherwise keeps built-in defaults.
void MainComponent::loadDefaultPreset()
{
    juce::ScopedValueSetter<bool> guard(suppressSessionPersistence, true);

    const auto file = getDefaultPresetFile();
    if (!file.existsAsFile())
        return;

    if (!loadPresetFromFile(file))
        channelImpactLabel.setText("Default preset could not be read, using built-in values.", juce::dontSendNotification);
}





















