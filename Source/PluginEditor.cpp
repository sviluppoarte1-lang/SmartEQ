#include "PluginEditor.h"
#include "PluginProcessor.h"

SmartEQAudioProcessorEditor::BandStrip::BandStrip(int i) : idx(i)
{
    name.setText("B" + juce::String(i+1), juce::dontSendNotification);
    name.setJustificationType(juce::Justification::centred);
    name.setColour(juce::Label::textColourId, juce::Colour(0xff00d4ff));
    name.setFont(juce::Font(11.f).withStyle(juce::Font::bold));

    gainL.setText("Gain", juce::dontSendNotification); gainL.setJustificationType(juce::Justification::centred); gainL.setFont(9.f);
    freqL.setText("Freq", juce::dontSendNotification); freqL.setJustificationType(juce::Justification::centred); freqL.setFont(9.f);
    qL.setText("Q", juce::dontSendNotification); qL.setJustificationType(juce::Justification::centred); qL.setFont(9.f);
    typeL.setText("Type", juce::dontSendNotification); typeL.setJustificationType(juce::Justification::centred); typeL.setFont(9.f);

    addAndMakeVisible(name); addAndMakeVisible(gain); addAndMakeVisible(gainL);
    addAndMakeVisible(freq); addAndMakeVisible(freqL);
    addAndMakeVisible(q); addAndMakeVisible(qL);
    addAndMakeVisible(type); addAndMakeVisible(typeL);
    addAndMakeVisible(enabled);

    gain.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00d4ff));
    freq.setColour(juce::Slider::thumbColourId, juce::Colour(0xff9a7bff));
    q.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffa500));

    enabled.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00ff88));
}

void SmartEQAudioProcessorEditor::BandStrip::resized()
{
    auto b = getLocalBounds();
    name.setBounds(b.removeFromTop(16));
    enabled.setBounds(b.removeFromTop(18));
    // Filter type selector on top of the strip (full width)
    typeL.setBounds(b.removeFromTop(12));
    type.setBounds(b.removeFromTop(22));
    auto top = b;
    gain.setBounds(top.removeFromLeft(36));
    gainL.setBounds(gain.getBounds().withY(gain.getBottom()).withHeight(12));
    auto right = top;
    freq.setBounds(right.removeFromTop(52));
    freqL.setBounds(right.removeFromTop(12));
    q.setBounds(right.removeFromTop(52));
    qL.setBounds(right.removeFromTop(12));
}

// (no demo-expired overlay in the public build: always unlocked)

SmartEQAudioProcessorEditor::SmartEQAudioProcessorEditor(SmartEQAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p)
{
    // Non chiamare setSize qui - lo facciamo alla fine dopo aver creato tutti i componenti
    // altrimenti resized() viene chiamato con spectrum==nullptr e crasha (Bitwig discovery)

#ifndef SMARTEQ_FREE_VERSION
    // Spectrum + intelligent analyzer (Full version only)
    spectrum = std::make_unique<SpectrumComponent>(processor.getSpectrumAnalyzer(), processor.getEQ(), processor.getIntelligentAnalyzer());
#else
    spectrum.reset(); // Free: 8 bands, no analyzer
#endif
    if (spectrum)
    {
        spectrum->onBandChanged = [this](int idx, float g, float f, float qv){
            if (auto* par = processor.apvts.getParameter("band" + juce::String(idx) + "_gain"))
                par->setValueNotifyingHost(par->convertTo0to1(g));
            if (auto* par = processor.apvts.getParameter("band" + juce::String(idx) + "_freq"))
                par->setValueNotifyingHost(par->convertTo0to1(f));
            if (auto* par = processor.apvts.getParameter("band" + juce::String(idx) + "_q"))
                par->setValueNotifyingHost(par->convertTo0to1(qv));
        };
        addAndMakeVisible(*spectrum);
    }

    // Title (ASCII only: "•" renders as mojibake on systems without the glyph)
#ifdef SMARTEQ_FREE_VERSION
    titleLabel.setText("SmartEQ Free  |  8 Band Equalizer", juce::dontSendNotification);
#else
    titleLabel.setText("SmartEQ  |  16 Band Intelligent Equalizer  |  Mastering & Editing", juce::dontSendNotification);
#endif
    titleLabel.setFont(juce::Font(18.f).withStyle(juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);

    // Analyze button - prominent
    analyzeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffff3b30));
    analyzeButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    analyzeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    analyzeButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00c853));
    analyzeButton.setTooltip("Analyze audio on the track (needs 2-4 seconds) and automatically fix excess/harsh/deficiencies");
    analyzeButton.onClick = [this]{
        analyzeButton.setEnabled(false);
        analyzeButton.setButtonText("ANALYZING...");
        processor.triggerAnalysis();
        juce::Timer::callAfterDelay(120, [this]{
            processor.applyAutoFix();
            auto& res = processor.lastAnalysisResult;
            {
                juce::ScopedLock sl(processor.analysisLock);
                statusLabel.setText(res.summary, juce::dontSendNotification);
                if (spectrum)
                {
                    spectrum->setAnalysisResult(&processor.lastAnalysisResult);
                    spectrum->setShowIntelligentOverlay(true);
                }
            }
            analyzerStatus.setText("Fix applied! Score: " + juce::String((int)res.overallScore) + "/100  |  " + juce::String(res.issues.size()) + " corrections", juce::dontSendNotification);
            analyzeButton.setButtonText("ANALYZE & FIX");
            analyzeButton.setEnabled(true);
            if (spectrum)
                juce::Timer::callAfterDelay(8000, [this]{ if (spectrum) spectrum->setShowIntelligentOverlay(false); });
        });
    };
    addAndMakeVisible(analyzeButton);

#ifndef SMARTEQ_FREE_VERSION
    // Song-map: impara il brano battuta per battuta, poi seguilo in real time
    songLearnButton.setClickingTogglesState(true);
    songLearnButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3a));
    songLearnButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb71c1c));
    songLearnButton.setTooltip("SONG LEARN: play the whole song - SmartEQ memorizza le variazioni timbriche battuta per battuta");
    addAndMakeVisible(songLearnButton);
    songFollowButton.setClickingTogglesState(true);
    songFollowButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3a));
    songFollowButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff00c853));
    songFollowButton.setTooltip("FOLLOW: l'EQ segue la mappa del brano in tempo reale (glide 400ms)");
    addAndMakeVisible(songFollowButton);
    songClearButton.setTooltip("Cancella la mappa del brano");
    songClearButton.onClick = [this]{
        processor.clearSongMap();
        statusLabel.setText("Song map cleared", juce::dontSendNotification);
    };
    addAndMakeVisible(songClearButton);
#endif

    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a3a));
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6a6a00));
    addAndMakeVisible(bypassButton);

    resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1e1e2a));
    resetButton.onClick = [this]{
        for(int i=0;i<EQProcessor::NumBands;++i) if(auto* par = processor.apvts.getParameter("band"+juce::String(i)+"_gain")) par->setValueNotifyingHost(par->convertTo0to1(0.f));
        statusLabel.setText("EQ reset - Flat", juce::dontSendNotification);
        if (spectrum) spectrum->setShowIntelligentOverlay(false);
        // Clear last preset memory on reset
        processor.setLastPreset("", "");
        presetBox.setText("Choose preset...", juce::dontSendNotification);
    };
    addAndMakeVisible(resetButton);

    // Preset - avoid auto-apply during init (caused Bitwig sandbox crash)
    isInitializing = true;
    categoryLabel.setText("Category:", juce::dontSendNotification); categoryLabel.setFont(11.f); categoryLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    presetLabel.setText("Preset:", juce::dontSendNotification); presetLabel.setFont(11.f); presetLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    addAndMakeVisible(categoryLabel); addAndMakeVisible(categoryBox);
    addAndMakeVisible(presetLabel); addAndMakeVisible(presetBox);
    categoryBox.addItemList(processor.getPresetManager().getCategories(), 1);
    // Restore last preset category if any
    {
        auto lastCat = processor.getLastPresetCategory();
        if(lastCat.isNotEmpty() && processor.getPresetManager().getCategories().contains(lastCat))
        {
            int idx = processor.getPresetManager().getCategories().indexOf(lastCat);
            categoryBox.setSelectedId(idx+1, juce::dontSendNotification);
        }
        else
            categoryBox.setSelectedId(1, juce::dontSendNotification);
    }
    updatePresetBox();
    // Restore last preset name if any
    {
        auto lastName = processor.getLastPresetName();
        if(lastName.isNotEmpty() && processor.getPresetManager().findPresetByName(lastName) >= 0)
        {
            presetBox.setText(lastName, juce::dontSendNotification);
            statusLabel.setText("Preset: " + lastName, juce::dontSendNotification);
        }
    }
    categoryBox.onChange = [this]{ if (!isInitializing) updatePresetBox(); };
    presetBox.onChange = [this]{ if (!isInitializing) applyPresetFromBox(); };

    // Gains
    inputLabel.setText("Input", juce::dontSendNotification); inputLabel.setFont(11.f); inputLabel.setJustificationType(juce::Justification::centred);
    outputLabel.setText("Output", juce::dontSendNotification); outputLabel.setFont(11.f); outputLabel.setJustificationType(juce::Justification::centred);
    strengthLabel.setText("AutoFix Strength", juce::dontSendNotification); strengthLabel.setFont(11.f); strengthLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(inputLabel); addAndMakeVisible(inputGainSlider);
    addAndMakeVisible(outputLabel); addAndMakeVisible(outputGainSlider);
    addAndMakeVisible(strengthLabel); addAndMakeVisible(strengthSlider);
    inputGainSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag); inputGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    outputGainSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag); outputGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    strengthSlider.setSliderStyle(juce::Slider::LinearHorizontal); strengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 16);
    strengthSlider.setRange(0,1,0.01);
    inputGainSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00d4ff));
    outputGainSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00d4ff));
    strengthSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xff00ff88));

#ifdef SMARTEQ_FREE_VERSION
    statusLabel.setText("SmartEQ Free ready - 8 bands. No analyzer in the Free version.", juce::dontSendNotification);
#else
    statusLabel.setText("Press ANALYZE while the track is playing to detect and auto-correct.", juce::dontSendNotification);
#endif
    statusLabel.setFont(juce::Font(11.f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffaaaaaa));
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    analyzerStatus.setText("Spectrum: 20Hz - 20kHz  |  FFT 2048  |  Ready", juce::dontSendNotification);
    analyzerStatus.setFont(juce::Font(10.f));
    analyzerStatus.setColour(juce::Label::textColourId, juce::Colour(0xff6a6a80));
    analyzerStatus.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(analyzerStatus);

#ifdef SMARTEQ_FREE_VERSION
    // Free: no analyzer, no auto-fix strength - hide those controls
    analyzeButton.setVisible(false);
    strengthLabel.setVisible(false);
    strengthSlider.setVisible(false);
    analyzerStatus.setVisible(false);
#else
    // (no license bar in the public build: full version, always unlocked)
#endif

    // Band strips (taller to fit Type selector) + modern styling
    bandContainer.setSize(1280, 250);
    for(int i=0;i<EQProcessor::NumBands;++i)
    {
        auto* strip = new BandStrip(i);
        strip->gain.setLookAndFeel(&modernLF);
        strip->freq.setLookAndFeel(&modernLF);
        strip->q.setLookAndFeel(&modernLF);
        strip->type.setLookAndFeel(&modernLF);
        strip->enabled.setLookAndFeel(&modernLF);
        bandStrips.add(strip);
        bandContainer.addAndMakeVisible(strip);
    }
    bandViewport.setViewedComponent(&bandContainer, false);
    bandViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(bandViewport);

    // Modern styling for global controls
    inputGainSlider.setLookAndFeel(&modernLF);
    outputGainSlider.setLookAndFeel(&modernLF);
    strengthSlider.setLookAndFeel(&modernLF);
    presetBox.setLookAndFeel(&modernLF);
    categoryBox.setLookAndFeel(&modernLF);
    analyzeButton.setLookAndFeel(&modernLF);
#ifndef SMARTEQ_FREE_VERSION
    songLearnButton.setLookAndFeel(&modernLF);
    songFollowButton.setLookAndFeel(&modernLF);
    songClearButton.setLookAndFeel(&modernLF);
#endif
    bypassButton.setLookAndFeel(&modernLF);
    resetButton.setLookAndFeel(&modernLF);

    // Attachments
    inputAttach = new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, "inputGain", inputGainSlider);
    outputAttach= new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, "outputGain", outputGainSlider);
    strengthAttach= new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, "autoFixStrength", strengthSlider);
    bypassAttach = new juce::AudioProcessorValueTreeState::ButtonAttachment(processor.apvts, "bypass", bypassButton);
#ifndef SMARTEQ_FREE_VERSION
    songLearnAttach = new juce::AudioProcessorValueTreeState::ButtonAttachment(processor.apvts, "songLearn", songLearnButton);
    songFollowAttach = new juce::AudioProcessorValueTreeState::ButtonAttachment(processor.apvts, "songFollow", songFollowButton);
#endif
    for(int i=0;i<EQProcessor::NumBands;++i)
    {
        auto* s = bandStrips[i];
        s->gain.setRange(-18,18,0.1); s->gain.setTextValueSuffix(" dB");
        s->freq.setRange(20,20000,1); s->freq.setSkewFactorFromMidPoint(1000); s->freq.setTextValueSuffix(" Hz");
        s->q.setRange(0.1,10,0.05);
        s->type.addItemList(Biquad::getTypeNames(), 1);
        s->type.setSelectedId(1, juce::dontSendNotification);
        s->type.setTooltip("Filter type: Bell, Shelves, Low/High Pass 12/24, Band Pass, Notch, All Pass");
        bandGainAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, "band"+juce::String(i)+"_gain", s->gain));
        bandFreqAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, "band"+juce::String(i)+"_freq", s->freq));
        bandQAttachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(processor.apvts, "band"+juce::String(i)+"_q", s->q));
        bandEnabledAttachments.add(new juce::AudioProcessorValueTreeState::ButtonAttachment(processor.apvts, "band"+juce::String(i)+"_enabled", s->enabled));
        bandTypeAttachments.add(new juce::AudioProcessorValueTreeState::ComboBoxAttachment(processor.apvts, "band"+juce::String(i)+"_type", s->type));
    }

    isInitializing = false;
    setResizable(true, true);
    setResizeLimits(1100, 700, 1920, 1200);
    setSize(1280, 860);
    startTimerHz(12);
}

SmartEQAudioProcessorEditor::~SmartEQAudioProcessorEditor()
{
    stopTimer();
    // Detach LookAndFeel before members are destroyed (modernLF dies last, but be explicit)
    inputGainSlider.setLookAndFeel(nullptr);
    outputGainSlider.setLookAndFeel(nullptr);
    strengthSlider.setLookAndFeel(nullptr);
    presetBox.setLookAndFeel(nullptr);
    categoryBox.setLookAndFeel(nullptr);
    analyzeButton.setLookAndFeel(nullptr);
#ifndef SMARTEQ_FREE_VERSION
    songLearnButton.setLookAndFeel(nullptr);
    songFollowButton.setLookAndFeel(nullptr);
    songClearButton.setLookAndFeel(nullptr);
#endif
    bypassButton.setLookAndFeel(nullptr);
    resetButton.setLookAndFeel(nullptr);
    for (auto* s : bandStrips)
    {
        if (s == nullptr) continue;
        s->gain.setLookAndFeel(nullptr);
        s->freq.setLookAndFeel(nullptr);
        s->q.setLookAndFeel(nullptr);
        s->type.setLookAndFeel(nullptr);
        s->enabled.setLookAndFeel(nullptr);
    }
    delete inputAttach; delete outputAttach; delete strengthAttach; delete bypassAttach;
#ifndef SMARTEQ_FREE_VERSION
    delete songLearnAttach; delete songFollowAttach;
#endif
}

void SmartEQAudioProcessorEditor::updatePresetBox()
{
    auto cat = categoryBox.getText();
    presetBox.setSelectedId(0, juce::dontSendNotification);
    presetBox.clear(juce::dontSendNotification);
    auto list = processor.getPresetManager().getPresetsForCategory(cat);
    int id=1;
    for(auto &p: list) presetBox.addItem(p.name, id++);
    if (presetBox.getNumItems()>0) presetBox.setSelectedId(0, juce::dontSendNotification);
    presetBox.setText("Choose preset...", juce::dontSendNotification);
    // If we had a last preset in this category, restore it
    auto lastName = processor.getLastPresetName();
    if(lastName.isNotEmpty() && processor.getPresetManager().findPresetByName(lastName) >= 0)
    {
        auto& last = processor.getPresetManager().getPreset(processor.getPresetManager().findPresetByName(lastName));
        if(last.category == cat)
            presetBox.setText(lastName, juce::dontSendNotification);
    }
}

void SmartEQAudioProcessorEditor::applyPresetFromBox()
{
    if (isInitializing) return;
    auto name = presetBox.getText();
    if (name == "Choose preset..." || name.isEmpty()) return;
    int idx = processor.getPresetManager().findPresetByName(name);
    if(idx>=0)
    {
        processor.getPresetManager().applyPreset(processor.getEQ(), idx);
        // Propaga a parametri - usa setValueNotifyingHost solo se host pronto
        for(int i=0;i<EQProcessor::NumBands;++i)
        {
            float g = (float)processor.getEQ().getBand(i).gainDB;
            float q = (float)processor.getEQ().getBand(i).Q;
            if(auto* par = processor.apvts.getParameter("band"+juce::String(i)+"_gain")) par->setValueNotifyingHost(par->convertTo0to1(g));
            if(auto* par = processor.apvts.getParameter("band"+juce::String(i)+"_q")) par->setValueNotifyingHost(par->convertTo0to1(q));
        }
        auto &p = processor.getPresetManager().getPreset(idx);
        statusLabel.setText("Preset: " + p.name + " - " + p.description, juce::dontSendNotification);
        // Remember last preset for persistence across reopen / save
        processor.setLastPreset(p.name, p.category);
    }
}

void SmartEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a0a0f));
    // Top bar gradient
    auto top = getLocalBounds().removeFromTop(38).toFloat();
    juce::ColourGradient grad(juce::Colour(0xff1a1a2e), top.getCentreX(), top.getY(),
                              juce::Colour(0xff0f0f1a), top.getCentreX(), top.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRect(top);
    g.setColour(juce::Colour(0xff2a2a40));
    g.drawHorizontalLine((int)top.getBottom(), top.getX(), top.getRight());
}

void SmartEQAudioProcessorEditor::resized()
{
    // Guard contro chiamate premature (Bitwig discovery crea editor temporaneo)
    // (Free has no spectrum component at all)
#ifndef SMARTEQ_FREE_VERSION
    if (!spectrum) return;
#endif
    if (bandStrips.size() < EQProcessor::NumBands) return;
    if (getWidth() < 50 || getHeight() < 50) return;
    auto b = getLocalBounds();
    auto top = b.removeFromTop(38);
    titleLabel.setBounds(top);

    auto controls = b.removeFromTop(52);
    controls.reduce(6,4);
    // Layout precisa: categoria, preset, analizza, bypass, reset, input, output, strength
    auto left = controls;
    categoryLabel.setBounds(left.removeFromLeft(62).removeFromTop(14));
    // riallinea: categoria box sotto label
    // Soluzione: split verticale per categoria/preset
    left = controls;
    auto catArea = left.removeFromLeft(150);
    categoryLabel.setBounds(catArea.removeFromTop(14));
    categoryBox.setBounds(catArea.removeFromTop(26));

    left.removeFromLeft(6);
#ifdef SMARTEQ_FREE_VERSION
    auto presetArea = left.removeFromLeft(338); // wider: no analyze button in Free
#else
    auto presetArea = left.removeFromLeft(150);
#endif
    presetLabel.setBounds(presetArea.removeFromTop(14));
    presetBox.setBounds(presetArea.removeFromTop(26));

#ifndef SMARTEQ_FREE_VERSION
    left.removeFromLeft(8);
    analyzeButton.setBounds(left.removeFromLeft(120));
    left.removeFromLeft(4);
    songLearnButton.setBounds(left.removeFromLeft(104).removeFromTop(30).withTrimmedTop(6));
    left.removeFromLeft(4);
    songFollowButton.setBounds(left.removeFromLeft(84).removeFromTop(30).withTrimmedTop(6));
    left.removeFromLeft(4);
    songClearButton.setBounds(left.removeFromLeft(60).removeFromTop(30).withTrimmedTop(6));
    left.removeFromLeft(6);
#endif
    bypassButton.setBounds(left.removeFromLeft(76).removeFromTop(30).withTrimmedTop(6));
    resetButton.setBounds(left.removeFromLeft(66).removeFromTop(30).withTrimmedTop(6));

    left.removeFromLeft(8);
    auto inArea = left.removeFromLeft(70);
    inputLabel.setBounds(inArea.removeFromTop(14));
    inputGainSlider.setBounds(inArea);

    auto outArea = left.removeFromLeft(70);
    outputLabel.setBounds(outArea.removeFromTop(14));
    outputGainSlider.setBounds(outArea);

#ifndef SMARTEQ_FREE_VERSION
    left.removeFromLeft(6);
    auto strArea = left;
    strengthLabel.setBounds(strArea.removeFromTop(14));
    strengthSlider.setBounds(strArea.removeFromTop(24));
#endif

    b.removeFromTop(2);
    // Bottom dock: band strips (+ license bar in Full). Everything else goes
    // to the spectrum analyzer (Full) or stays empty (Free).
    constexpr int bandDockH = 250;
    constexpr int statusH = 16 + 22;
#ifdef SMARTEQ_FREE_VERSION
    auto bottom = b.removeFromBottom(bandDockH);
    auto statusArea = b.removeFromBottom(statusH);
    analyzerStatus.setBounds(statusArea.removeFromTop(16));
    statusLabel.setBounds(statusArea);
    // Free: no spectrum - leave the freed area empty (dark background)
#else
    auto bottom = b.removeFromBottom(bandDockH);
    auto statusArea = b.removeFromBottom(statusH);
    analyzerStatus.setBounds(statusArea.removeFromTop(16));
    statusLabel.setBounds(statusArea);
    auto spectrumArea = b; // whatever is left -> full panel analyzer
    spectrumArea.reduce(6, 2);
    spectrum->setBounds(spectrumArea);
#endif

    bottom.reduce(6, 2);
    bandViewport.setBounds(bottom);

    int stripW = 84;
    int h = bottom.getHeight();
    bandContainer.setSize(EQProcessor::NumBands * stripW + 10, h);
    for(int i=0;i<EQProcessor::NumBands;++i) bandStrips[i]->setBounds(i*stripW + 1, 0, stripW-1, h);
}

void SmartEQAudioProcessorEditor::timerCallback()
{
#ifndef SMARTEQ_FREE_VERSION
    float prog = processor.getIntelligentAnalyzer().getProgress();
    juce::String songTxt = processor.getSongAnalyzer().getStatusText();
    if (prog < 1.0f && prog > 0.05f)
        analyzerStatus.setText("Analyzing: " + juce::String((int)(prog*100)) + "% - keep playing track  |  " + songTxt, juce::dontSendNotification);
    else if (processor.getIntelligentAnalyzer().isReady())
        analyzerStatus.setText("Ready for ANALYZE | " + juce::String(processor.getSpectrumAnalyzer().isReady() ? "Spectrum active" : "Waiting for audio") + "  |  " + songTxt, juce::dontSendNotification);
    else
        analyzerStatus.setText(songTxt, juce::dontSendNotification);
#endif
    repaint();
}
