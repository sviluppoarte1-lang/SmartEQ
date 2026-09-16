#pragma once
#include <JuceHeader.h>
#include "DSP/EQProcessor.h"
#include "DSP/IntelligentAnalyzer.h"
#include "DSP/SongAnalyzer.h"
#include "DSP/SpectrumAnalyzer.h"
#include "Presets/PresetManager.h"
// Public build: no licensing - full version, always unlocked.

class SmartEQAudioProcessor : public juce::AudioProcessor
{
public:
    SmartEQAudioProcessor();
    ~SmartEQAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

#ifdef SMARTEQ_FREE_VERSION
    const juce::String getName() const override { return "SpectraCurve Free"; }
#else
    const juce::String getName() const override { return "SpectraCurve EQ"; }
#endif
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Accesso DSP per editor
    EQProcessor& getEQ() { return eq; }
    SpectrumAnalyzer& getSpectrumAnalyzer() { return spectrumAnalyzer; }
    IntelligentAnalyzer& getIntelligentAnalyzer() { return intelligentAnalyzer; }
    PresetManager& getPresetManager() { return presetManager; }

    // Parametri
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Per comunicazione UI
    std::atomic<bool> bypass { false };
    std::atomic<float> inputGain { 1.0f }; // linear
    std::atomic<float> outputGain { 1.0f };
    std::atomic<float> autoFixStrength { 1.0f };

    // Analysis result cache
    IntelligentAnalyzer::AnalysisResult lastAnalysisResult;
    juce::CriticalSection analysisLock;

    void triggerAnalysis();
    void applyAutoFix();

#ifndef SMARTEQ_FREE_VERSION
    // Full-song bar-by-bar map (Full only)
    SongAnalyzer& getSongAnalyzer() { return songAnalyzer; }
    void clearSongMap();
    bool isSongFollowing() const;
#endif

    // Preset persistence (stored in APVTS state)
    void setLastPreset(const juce::String& name, const juce::String& category);
    juce::String getLastPresetName() const;
    juce::String getLastPresetCategory() const;

    // Public build: no license keys here. Free edition fully working;
    // Full edition runs a 45-minute demo per session (timer only, no secrets).
#ifdef SMARTEQ_FREE_VERSION
    bool isDemoExpired() const { return false; }
    double getDemoSecondsRemaining() const { return -1.0; }
#else
    static constexpr double kDemoLimitSeconds = 45.0 * 60.0;
    bool isDemoExpired() const;
    double getDemoSecondsRemaining() const;
#endif

private:
    EQProcessor eq;
    SpectrumAnalyzer spectrumAnalyzer;
    IntelligentAnalyzer intelligentAnalyzer;
    PresetManager presetManager;

#ifndef SMARTEQ_FREE_VERSION
    SongAnalyzer songAnalyzer;
    float songCurrentOffsets[SongAnalyzer::MaxSongBands] = { 0 };
    bool songWasPlaying = false;
    void updateSongMap(juce::AudioBuffer<float>& buffer, double sampleRate);
#endif

#ifndef SMARTEQ_FREE_VERSION
    std::atomic<double> demoSecondsUsed { 0.0 }; // demo timer only, no secrets
#endif

    // Smoothing bypass
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryWet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmartEQAudioProcessor)
};
