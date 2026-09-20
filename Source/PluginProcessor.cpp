#include "PluginProcessor.h"
#include "PluginEditor.h"

SmartEQAudioProcessor::SmartEQAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Public build: always unlocked, no license handling.
}

SmartEQAudioProcessor::~SmartEQAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout SmartEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("inputGain", "Input Gain",
        juce::NormalisableRange<float>(-24.f, 12.f, 0.1f), 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("outputGain", "Output Gain",
        juce::NormalisableRange<float>(-24.f, 12.f, 0.1f), 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("autoFixStrength", "AutoFix Strength",
        juce::NormalisableRange<float>(0.f, 1.f, 0.01f), 1.f));

#ifndef SMARTEQ_FREE_VERSION
    // Full-song map: learn variations bar-by-bar, then follow them in real time
    layout.add(std::make_unique<juce::AudioParameterBool>("songLearn", "Song Learn", false));
    layout.add(std::make_unique<juce::AudioParameterBool>("songFollow", "Song Follow", false));
    layout.add(std::make_unique<juce::AudioParameterFloat>("songGlide", "Song Glide",
        juce::NormalisableRange<float>(0.f, 2000.f, 1.f, 0.4f), 400.f));
    // Room calibration - mic profile selection
    {
        auto micNames = MicProfile::getProfileNames();
        layout.add(std::make_unique<juce::AudioParameterChoice>("micProfile", "Mic Profile", micNames, 0));
    }
#endif

    // All bands - all professional filter types per band (8 Free / 16 Full)
    auto typeChoices = Biquad::getTypeNames();
    for (int i=0;i<EQProcessor::NumBands;++i)
    {
        auto gainId = "band" + juce::String(i) + "_gain";
        auto freqId = "band" + juce::String(i) + "_freq";
        auto qId    = "band" + juce::String(i) + "_q";
        auto enId   = "band" + juce::String(i) + "_enabled";
        auto typeId = "band" + juce::String(i) + "_type";
        layout.add(std::make_unique<juce::AudioParameterFloat>(gainId, "Band " + juce::String(i+1) + " Gain",
            juce::NormalisableRange<float>(-18.f, 18.f, 0.1f), 0.f));
        layout.add(std::make_unique<juce::AudioParameterFloat>(freqId, "Band " + juce::String(i+1) + " Freq",
            juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.35f), (float)EQProcessor::defaultFreqs[i]));
        // Shelf bands (first/last) default to Butterworth-flat Q 0.71
        float defaultQ = (i == 0 || i == EQProcessor::NumBands - 1) ? 0.71f : 1.4f;
        layout.add(std::make_unique<juce::AudioParameterFloat>(qId, "Band " + juce::String(i+1) + " Q",
            juce::NormalisableRange<float>(0.1f, 10.f, 0.05f), defaultQ));
        layout.add(std::make_unique<juce::AudioParameterBool>(enId, "Band " + juce::String(i+1) + " Enabled", true));
        int defaultType = 0; // Bell
        if (i == 0) defaultType = 1; // Low Shelf on band 1
        else if (i == EQProcessor::NumBands - 1) defaultType = 2; // High Shelf on last band
        layout.add(std::make_unique<juce::AudioParameterChoice>(typeId, "Band " + juce::String(i+1) + " Type",
            typeChoices, defaultType));
    }
    return layout;
}

void SmartEQAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (sampleRate < 8000) sampleRate = 48000;
    if (samplesPerBlock <= 0) samplesPerBlock = 512;
    eq.prepare(sampleRate, samplesPerBlock);
    spectrumAnalyzer.prepare(sampleRate, samplesPerBlock);
    intelligentAnalyzer.reset();
#ifndef SMARTEQ_FREE_VERSION
    songAnalyzer.prepare(sampleRate);
    for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i) songCurrentOffsets[i] = 0.0f;
    roomCalibrator.prepare(sampleRate, samplesPerBlock);
#endif
    // Sincronizza parametri -> DSP con guard
    for (int i=0;i<EQProcessor::NumBands;++i)
    {
        auto* g = apvts.getRawParameterValue("band" + juce::String(i) + "_gain");
        auto* f = apvts.getRawParameterValue("band" + juce::String(i) + "_freq");
        auto* q = apvts.getRawParameterValue("band" + juce::String(i) + "_q");
        auto* e = apvts.getRawParameterValue("band" + juce::String(i) + "_enabled");
        auto* t = apvts.getRawParameterValue("band" + juce::String(i) + "_type");
        if (!g || !f || !q || !e) continue;
        eq.getBand(i).gainDB = g->load();
        eq.getBand(i).freq = juce::jlimit(20.0, 20000.0, (double)f->load());
        eq.getBand(i).Q = juce::jlimit(0.1, 10.0, (double)q->load());
        eq.getBand(i).enabled = e->load() > 0.5f;
        if (t) eq.getBand(i).type = (Biquad::Type) juce::jlimit(0, (int)Biquad::NumTypes - 1, (int) t->load());
        eq.getBand(i).updateCoefficients(sampleRate);
    }
    dryWet.reset(sampleRate, 0.02);
    dryWet.setCurrentAndTargetValue(1.0f);
}

void SmartEQAudioProcessor::releaseResources() {}

bool SmartEQAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Bitwig interroga layout strani (mono, stereo, sidechain) - accetta tutto ragionevole
    // Consenti mono->stereo, stereo->stereo, anche disabled sidechain
    auto mainIn = layouts.getChannelSet(true, 0);
    auto mainOut = layouts.getChannelSet(false, 0);
    // Se main disabilitato, rifiuta ma consenti sidechain extra
    if (mainIn.isDisabled() && mainOut.isDisabled()) return false;
    // Accetta fino a stereo, rifiuta oltre 2 canali per non complicare DSP
    if (mainIn.size() > 2 || mainOut.size() > 2) return false;
    // Consenti mismatched (mono in -> stereo out) per Bitwig
    return true;
}

void SmartEQAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Bitwig può chiamare con buffer vuoto durante scan - non crashare
    if (numSamples == 0) return;
    // Pulisci canali output extra
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Valida sampleRate (Bitwig può chiamare processBlock prima di prepareToPlay)
    double sr = getSampleRate();
    if (sr < 8000) sr = 48000;

#ifndef SMARTEQ_FREE_VERSION
    // Full-edition demo: 45 minutes of audio per session, then mute.
    // Timer only - no license keys in the public build.
    demoSecondsUsed.store(demoSecondsUsed.load() + (double) numSamples / sr);
    if (demoSecondsUsed.load() >= kDemoLimitSeconds)
    {
        buffer.clear();
        return;
    }
#endif

    // --- Room calibrator SE-9 style: se in Playing genera pink e cattura ---
#ifndef SMARTEQ_FREE_VERSION
    {
        auto rcState = roomCalibrator.getState();
        if (rcState == RoomCalibrator::State::Playing || rcState == RoomCalibrator::State::Analyzing)
        {
            roomCalibrator.processBlock(buffer, true);
            return;
        }
    }
#endif

    // Aggiorna parametri live con null-check (incl. tipo filtro)
    for (int i=0;i<EQProcessor::NumBands;++i)
    {
        auto* pg = apvts.getRawParameterValue("band" + juce::String(i) + "_gain");
        auto* pf = apvts.getRawParameterValue("band" + juce::String(i) + "_freq");
        auto* pq = apvts.getRawParameterValue("band" + juce::String(i) + "_q");
        auto* pe = apvts.getRawParameterValue("band" + juce::String(i) + "_enabled");
        auto* pt = apvts.getRawParameterValue("band" + juce::String(i) + "_type");
        if (!pg || !pf || !pq || !pe) continue;
        float g = pg->load();
        float f = pf->load();
        float q = pq->load();
        bool en = pe->load() > 0.5f;
        int ty = pt ? (int) pt->load() : (int) eq.getBand(i).type;
        auto& band = eq.getBand(i);
        if (std::abs(band.gainDB - g) > 0.0001 || std::abs(band.freq - f) > 0.01 || std::abs(band.Q - q) > 0.0001 || band.enabled != en || (int) band.type != ty)
        {
            band.gainDB = g; band.freq = juce::jlimit(20.0, 20000.0, (double)f); band.Q = juce::jlimit(0.1, 10.0, (double)q); band.enabled = en;
            band.type = (Biquad::Type) juce::jlimit(0, (int)Biquad::NumTypes - 1, ty);
            band.updateCoefficients(sr);
        }
    }

    auto* pinGain = apvts.getRawParameterValue("inputGain");
    auto* poutGain = apvts.getRawParameterValue("outputGain");
    auto* pbypass = apvts.getRawParameterValue("bypass");
    if (!pinGain || !poutGain || !pbypass) return;
    float inGainLin = juce::Decibels::decibelsToGain(pinGain->load());
    float outGainLin = juce::Decibels::decibelsToGain(poutGain->load());
    bool isBypass = pbypass->load() > 0.5f;

    // Input gain - check finiti
    if (!std::isfinite(inGainLin)) inGainLin = 1.0f;
    if (!std::isfinite(outGainLin)) outGainLin = 1.0f;
    buffer.applyGain(inGainLin);

    // Feed analyzers (Full version only; Free has no analyzer)
#ifndef SMARTEQ_FREE_VERSION
    try {
        if (buffer.getNumChannels() > 0 && numSamples > 0)
        {
            spectrumAnalyzer.pushBuffer(buffer);
            intelligentAnalyzer.pushAudioBlock(buffer.getReadPointer(0), numSamples, buffer.getNumChannels(), sr);
        }
    } catch (...) {}
#endif
#ifndef SMARTEQ_FREE_VERSION
    try { updateSongMap(buffer, sr); } catch (...) {}
#endif

    if (!isBypass)
    {
        // Denormal check già fatto, ma proteggiamoci da NaN
        try { eq.processBlock(buffer); } catch (...) { buffer.clear(); }
    }

    buffer.applyGain(outGainLin);

    // Safety clip + sanitizza NaN/Inf (Bitwig sandbox uccide plugin su NaN)
    for (int ch=0; ch<buffer.getNumChannels(); ++ch)
    {
        float* d = buffer.getWritePointer(ch);
        for (int i=0;i<numSamples;++i)
        {
            float s = d[i];
            if (!std::isfinite(s)) s = 0.0f;
            d[i] = juce::jlimit(-1.2f, 1.2f, s);
        }
    }
}

void SmartEQAudioProcessor::processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midi)
{
    // Bitwig può chiamare double - converti a float, processa, riconverti
    juce::AudioBuffer<float> floatBuf(buffer.getNumChannels(), buffer.getNumSamples());
    for(int ch=0; ch<buffer.getNumChannels(); ++ch)
        for(int i=0;i<buffer.getNumSamples(); ++i) floatBuf.setSample(ch,i, (float)buffer.getSample(ch,i));
    processBlock(floatBuf, midi);
    for(int ch=0; ch<buffer.getNumChannels(); ++ch)
        for(int i=0;i<buffer.getNumSamples(); ++i) buffer.setSample(ch,i, (double)floatBuf.getSample(ch,i));
}

#ifndef SMARTEQ_FREE_VERSION
void SmartEQAudioProcessor::startRoomCalibration()
{
    int micIdx = 0;
    if (auto* p = apvts.getRawParameterValue("micProfile")) micIdx = (int) p->load();
    roomCalibrator.setMicProfile(micIdx);
    roomCalibrator.startCalibration(micIdx);
}

void SmartEQAudioProcessor::abortRoomCalibration()
{
    roomCalibrator.abort();
}

void SmartEQAudioProcessor::applyRoomCalibration(float strength)
{
    roomCalibrator.applyToEQ(eq, strength);
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        float newGain = (float) eq.getBand(i).gainDB;
        if (auto* p = apvts.getParameter("band" + juce::String(i) + "_gain"))
            p->setValueNotifyingHost(p->convertTo0to1(newGain));
    }
}

void SmartEQAudioProcessor::clearSongMap()
{
    songAnalyzer.reset();
    for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i) songCurrentOffsets[i] = 0.0f;
}

bool SmartEQAudioProcessor::isSongFollowing() const
{
    auto* p = apvts.getRawParameterValue("songFollow");
    return p && p->load() > 0.5f;
}

void SmartEQAudioProcessor::updateSongMap(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto* pLearn = apvts.getRawParameterValue("songLearn");
    auto* pFollow = apvts.getRawParameterValue("songFollow");
    auto* pGlide = apvts.getRawParameterValue("songGlide");
    if (! pLearn || ! pFollow) return;
    bool learn = pLearn->load() > 0.5f;
    bool follow = pFollow->load() > 0.5f;
    float glideMs = pGlide ? pGlide->load() : 400.0f;

    songAnalyzer.setLearning(learn);

    // Host transport: ppq + bpm + playing (JUCE 7 PositionInfo)
    double ppq = -1.0, bpm = 0.0;
    bool playing = false, ppqValid = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto o = pos->getPpqPosition()) { ppq = *o; ppqValid = true; }
            if (auto o = pos->getBpm()) bpm = *o;
            playing = pos->getIsPlaying();
            if (! ppqValid)
            {
                auto t = pos->getTimeInSeconds();
                if (t && bpm > 0) { ppq = (*t) * bpm / 60.0; ppqValid = true; }
            }
        }
    }

    // Falling edge del transport: consolida l'ultima battuta (altrimenti si perde)
    if (songWasPlaying && ! playing)
        songAnalyzer.flush();
    songWasPlaying = playing;

    const int numSamples = buffer.getNumSamples();
    if (learn && buffer.getNumChannels() > 0 && numSamples > 0)
        songAnalyzer.pushAudioBlock(buffer.getReadPointer(0), numSamples,
                                    ppq, ppqValid, playing, bpm, sampleRate);

    if (follow && songAnalyzer.hasData())
    {
        float target[SongAnalyzer::MaxSongBands] = { 0 };
        bool haveTarget = ppqValid
            ? songAnalyzer.getGainsForPpq(ppq, target)
            : songAnalyzer.getGainsForBar(0, target);
        // Senza target (mappa tutta silente) scivola a zero: mai offset congelati
        {
            // One-pole glide verso il target (evita salti tra battute diverse)
            float alpha = 1.0f;
            if (glideMs > 1.0f && sampleRate > 0 && numSamples > 0)
                alpha = 1.0f - std::exp(-2.2f * (float) numSamples / ((glideMs * 0.001f) * (float) sampleRate));
            alpha = juce::jlimit(0.0f, 1.0f, alpha);
            if (! haveTarget)
                for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i) target[i] = 0.0f;

            int n = juce::jmin(EQProcessor::NumBands, SongAnalyzer::MaxSongBands);
            for (int i = 0; i < n; ++i)
                songCurrentOffsets[i] += alpha * (juce::jlimit(-9.0f, 9.0f, target[i]) - songCurrentOffsets[i]);

            // Applica come offset sopra i gain utente (letti live dai parametri)
            for (int i = 0; i < n; ++i)
            {
                auto* pg = apvts.getRawParameterValue("band" + juce::String(i) + "_gain");
                if (! pg) continue;
                float eff = juce::jlimit(-18.0f, 18.0f, pg->load() + songCurrentOffsets[i]);
                auto& band = eq.getBand(i);
                if (std::abs(band.gainDB - eff) > 0.0001)
                {
                    band.gainDB = eff;
                    band.updateCoefficients(sampleRate);
                }
            }
        }
    }
    else
    {
        // Follow off: riporta gli offset a zero (il sync parametri ripristina i gain utente)
        for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i) songCurrentOffsets[i] = 0.0f;
    }
}
#endif

void SmartEQAudioProcessor::triggerAnalysis()
{
    auto result = intelligentAnalyzer.analyze();
    {
        juce::ScopedLock sl(analysisLock);
        lastAnalysisResult = result;
    }
    // Auto-fix applica direttamente su apvts (thread sicuro via UI)
    // Notificheremo l'editor tramite callback
}

void SmartEQAudioProcessor::applyAutoFix()
{
    juce::ScopedLock sl(analysisLock);
    float strength = apvts.getRawParameterValue("autoFixStrength")->load();
    intelligentAnalyzer.applyAutoFix(eq, lastAnalysisResult, strength);
    // Propaga a parametri
    for (int i=0;i<EQProcessor::NumBands;++i)
    {
        float newGain = (float)eq.getBand(i).gainDB;
        float newQ = (float)eq.getBand(i).Q;
        if (auto* p = apvts.getParameter("band" + juce::String(i) + "_gain"))
            p->setValueNotifyingHost(p->convertTo0to1(newGain));
        if (auto* p = apvts.getParameter("band" + juce::String(i) + "_q"))
            p->setValueNotifyingHost(p->convertTo0to1(newQ));
    }
}

juce::AudioProcessorEditor* SmartEQAudioProcessor::createEditor() { return new SmartEQAudioProcessorEditor(*this); }

void SmartEQAudioProcessor::setLastPreset(const juce::String& name, const juce::String& category)
{
    apvts.state.setProperty("lastPresetName", name, nullptr);
    apvts.state.setProperty("lastPresetCategory", category, nullptr);
}
juce::String SmartEQAudioProcessor::getLastPresetName() const
{
    return apvts.state.getProperty("lastPresetName", "").toString();
}
juce::String SmartEQAudioProcessor::getLastPresetCategory() const
{
    return apvts.state.getProperty("lastPresetCategory", "").toString();
}

void SmartEQAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
#ifndef SMARTEQ_FREE_VERSION
    state.appendChild(songAnalyzer.toValueTree(), nullptr);
#endif
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

#ifndef SMARTEQ_FREE_VERSION
bool SmartEQAudioProcessor::isDemoExpired() const
{
    return demoSecondsUsed.load() >= kDemoLimitSeconds;
}

double SmartEQAudioProcessor::getDemoSecondsRemaining() const
{
    return juce::jmax(0.0, kDemoLimitSeconds - demoSecondsUsed.load());
}
#endif

void SmartEQAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml) apvts.replaceState(juce::ValueTree::fromXml(*xml));
#ifndef SMARTEQ_FREE_VERSION
    auto songChild = apvts.state.getChildWithName("SongMap");
    if (songChild.isValid())
        songAnalyzer.restoreFromValueTree(songChild);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SmartEQAudioProcessor(); }
