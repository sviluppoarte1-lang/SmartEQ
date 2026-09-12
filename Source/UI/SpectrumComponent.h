#pragma once
#include <JuceHeader.h>
#include "../DSP/EQProcessor.h"
#include "../DSP/SpectrumAnalyzer.h"
#include "../DSP/IntelligentAnalyzer.h"

class SpectrumComponent : public juce::Component, public juce::Timer
{
public:
    SpectrumComponent(SpectrumAnalyzer& sa, EQProcessor& eq, IntelligentAnalyzer& ia);
    void paint(juce::Graphics& g) override;
    void resized() override { }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent&) override { draggingBand = -1; }

    std::function<void(int bandIdx, float newGain, float newFreq, float newQ)> onBandChanged;

    void setShowIntelligentOverlay(bool s) { showIntelligent = s; repaint(); }
    void setAnalysisResult(const IntelligentAnalyzer::AnalysisResult* r) { analysisResult = r; repaint(); }
    // Free edition: curve + nodes only, no live spectrum (no analyzer there)
    void setShowSpectrum(bool s) { showSpectrum = s; repaint(); }

private:
    void timerCallback() override { repaint(); }

    SpectrumAnalyzer& spectrumAnalyzer;
    EQProcessor& eqProcessor;
    IntelligentAnalyzer& intelligentAnalyzer;

    static constexpr int spectrumPoints = 512;
    float spectrumDB[spectrumPoints] = { -100 };
    float eqCurveDB[spectrumPoints] = { 0 };
    double freqs[spectrumPoints] = {0};

    int draggingBand = -1;
    float dragStartGain = 0;
    double dragStartFreq = 0;

    bool showIntelligent = false;
    bool showSpectrum = true;
    const IntelligentAnalyzer::AnalysisResult* analysisResult = nullptr;

    juce::Point<float> freqGainToPixel(double freq, float gainDB, juce::Rectangle<float> bounds) const;
    void pixelToFreqGain(juce::Point<float> p, juce::Rectangle<float> bounds, double& freq, float& gainDB) const;
    int findClosestBand(juce::Point<float> pos, juce::Rectangle<float> bounds) const;

    // Grid helpers
    void drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds);
};
