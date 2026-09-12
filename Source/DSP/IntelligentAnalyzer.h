#pragma once
#include <JuceHeader.h>
#include "EQProcessor.h"

// Analizzatore intelligente - rileva eccessi, fastidi, carenze e propone fix
class IntelligentAnalyzer
{
public:
    struct AnalysisResult
    {
        struct BandIssue
        {
            int bandIdx = -1;
            double freq = 1000;
            float detectedLevelDB = 0;   // livello rilevato vs media
            float suggestedGainDB = 0;   // correzione proposta
            float suggestedQ = 1.4f;
            juce::String reason;         // es. "Risonanza fastidiosa a 3.2kHz"
            enum Severity { Info, Warning, Critical } severity = Info;
            bool isExcess = true; // true = tagliare, false = boost carenza
        };

        juce::Array<BandIssue> issues;
        float overallScore = 100; // 0-100 qualità
        juce::String summary; // testo riassuntivo
        float suggestedGains[EQProcessor::NumBands] = {0};

        // Statistiche spettro
        float rmsDB = -60;
        float crestFactor = 0;
        float spectralBalance = 0; // -1 scuro, +1 brillante
        float tiltDB = 0; // bilanciamento globale: energia low (200Hz) meno high (3.5-7.5k)
        float muddiness = 0; // 0-1
        float harshness = 0; // 0-1
        float sibilance = 0;
    };

    IntelligentAnalyzer();

    // Chiamato dal thread audio per accumulare campioni (lock-free FIFO semplificata)
    void pushAudioBlock(const float* data, int numSamples, int numChannels, double sampleRate);

    // Analisi offline su buffer accumulato (chiamata da UI thread quando premi ANALIZZA)
    AnalysisResult analyze();

    // Variante riusabile per song-map: analizza uno spettro dB gia' pronto (size = getSpectrumSize()).
    // crestDB: crest factor del materiale (dB, <0 = sconosciuto). Con materiale
    // transiente (batteria) i tagli stretti vengono ridotti: inseguono il wash,
    // non i colpi - senza crest non sapremmo distinguerli dallo spettro medio.
    AnalysisResult analyzeSpectrumDB(const float* spectrumDB, float crestDB = -1.0f);

    // Applica fix automatico a EQProcessor (interpolazione smooth)
    void applyAutoFix(EQProcessor& eq, const AnalysisResult& result, float strength = 1.0f);

    // Configurazione analisi
    void setAnalysisDuration(double seconds) { analysisDurationSec = seconds; }
    void setSampleRate(double sr) { if (sr >= 8000) sampleRate = sr; }
    void reset();

    bool isReady() const { return accumulatedSamples > minSamplesForAnalysis; }
    float getProgress() const; // 0-1

    // Per Spectrum analyzer: fornisce FFT già calcolata
    const float* getAveragedSpectrumDB() const { return averagedSpectrumDB; }
    int getSpectrumSize() const { return spectrumSize; }
    double getBandFreq(int idx) const;

private:
    static constexpr int fftOrder = 12; // 4096
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int spectrumSize = fftSize / 2;
    static constexpr int NumBands = EQProcessor::NumBands;

    // Buffer circolare per analisi
    static constexpr int maxAccumulated = 48000 * 8; // 8 sec @48k
    juce::AudioBuffer<float> circBuffer;
    int writePos = 0;
    int accumulatedSamples = 0;
    int minSamplesForAnalysis = 48000 * 2; // almeno 2 sec
    double analysisDurationSec = 4.0;
    double sampleRate = 48000;

    float averagedSpectrumDB[spectrumSize] = { -100 };
    float peakSpectrumDB[spectrumSize] = { -100 };

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    // Helpers
    void computeSpectrum(const float* monoData, int numSamples);
    float freqToBin(double freq) const;
    double binToFreq(int bin) const;
    float getBandEnergy(double centerFreq, double bandwidthOct = 0.5) const;

    // Detection algorithms
    void detectSpectralTilt(AnalysisResult& r); // global dark/bright balance (runs first)
    int nearestBandIdx(double freq) const;      // closest EQ band (any NumBands)
    void detectResonances(AnalysisResult& r);
    void detectMuddiness(AnalysisResult& r);
    void detectHarshness(AnalysisResult& r);
    void detectSibilance(AnalysisResult& r);
    void detectLowDeficiency(AnalysisResult& r);
    void detectHarshLowMids(AnalysisResult& r);
    void mapIssuesToBands(AnalysisResult& r);
    AnalysisResult analyzeCore(); // richiede `lock` gia' acquisito; spettro in averagedSpectrumDB
    // Scala dei tagli su materiale transiente (1 = pieno, 0.35 = wash di batteria)
    float transientCutScale() const;
    float crestForGuard = -1.0f; // crest noto a analyze()/analyzeSpectrumDB(), -1 = ignoto

    juce::CriticalSection lock;
    float tmpFFT[fftSize * 2] = {0};
};
