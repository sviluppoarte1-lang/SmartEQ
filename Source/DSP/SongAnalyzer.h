#pragma once
#include <JuceHeader.h>
#include "IntelligentAnalyzer.h"
#include "EQProcessor.h"

// SongAnalyzer: impara le variazioni timbriche del brano, battuta per battuta.
//
// - In LEARN (transport in play + ppq valida) accumula l'audio in ingresso e,
//   a ogni cambio di battuta, calcola lo spettro medio e lo converte in
//   correzioni EQ tramite IntelligentAnalyzer::analyzeSpectrumDB().
// - In FOLLOW il processor interroga getGainsForPpq() e applica le correzioni
//   con glide, cosi' l'EQ segue il brano in tempo reale.
// - La mappa e' persistita nello stato del plugin (get/setStateInformation).
class SongAnalyzer
{
public:
    static constexpr int MaxSongBands = 16;
    static constexpr int MaxBars = 1024;

    struct BarEntry
    {
        int bar = -1;
        float gains[MaxSongBands] = { 0 };
        float rmsDB = -60.0f;
        bool silent = true; // sotto -60 dB: non cuociamo spazzatura dal rumore
    };

    SongAnalyzer();

    void prepare(double sampleRate, int beatsPerBar = 4);
    void reset();
    void flush(); // consolida la battuta corrente (chiamare su stop del transport)
    void setBeatsPerBar(int bpb);
    void setLearning(bool shouldLearn) { learning.store(shouldLearn); }
    bool isLearning() const { return learning.load(); }

    // mono = canale 0 post-input-gain, pre-EQ. Solo audio thread.
    void pushAudioBlock(const float* monoData, int numSamples,
                        double ppqPosition, bool ppqValid, bool isPlaying,
                        double bpm, double sampleRate);

    // Ritorna false se la mappa e' vuota. outGains deve contenere MaxSongBands float.
    bool getGainsForPpq(double ppqPosition, float* outGains) const;
    bool getGainsForBar(int bar, float* outGains) const;
    int getNumBars() const;
    bool hasData() const;
    int getCurrentBar() const { return currentBar; }
    juce::String getStatusText() const;

    // Copia una mappa statica (es. snapshot ANALYZE) su tutte le battute apprese
    // o su una singola battuta se la mappa e' vuota.
    void importStaticMap(const float* gainsDB, int numGains);

    juce::ValueTree toValueTree() const;
    void restoreFromValueTree(const juce::ValueTree& v);

private:
    int barFromPpq(double ppq) const;
    void finalizeCurrentBar();
    void computeBarSpectrum(float* outSpectrumDB) const;
    int nearestMusicalBar(int bar) const; // richiede `lock`; -1 se solo silenzio

    mutable juce::CriticalSection lock;
    IntelligentAnalyzer helper;

    double sampleRate = 48000.0;
    int beatsPerBar = 4;
    std::atomic<bool> learning { false };

    std::vector<BarEntry> bars;          // indicizzato per numero di battuta (gap riempiti)
    int currentBar = -1;
    std::vector<float> barSamples;       // accumulo battuta corrente (mono)
    double barSumSquares = 0.0;
    float barPeak = 0.0f;
    juce::int64 barSampleCount = 0;

    juce::dsp::FFT fft { 12 }; // 4096, stesso spettro di IntelligentAnalyzer
    juce::dsp::WindowingFunction<float> window { (size_t) 4096, juce::dsp::WindowingFunction<float>::hann };
    mutable float tmpFFT[4096 * 2] = { 0 };

    static constexpr int fftSize = 4096;
    static constexpr int spectrumSize = 2048;
};
