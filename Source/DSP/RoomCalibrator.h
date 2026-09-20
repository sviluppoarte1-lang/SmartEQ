#pragma once
#include <JuceHeader.h>
#include "MicProfile.h"
#include "PinkNoiseGenerator.h"
#include "EQProcessor.h"

// Calibratore ambiente stile Sansui SE-9
// Genera rumore rosa, registra via ingresso, calcola correzione EQ

class RoomCalibrator
{
public:
    enum class State { Idle, Playing, Analyzing, Complete, Error };

    RoomCalibrator();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Avvia calibrazione: serve micProfileIdx
    void startCalibration(int micProfileIdx);
    void abort();

    // Chiamato da processBlock: genera pink su output e cattura input
    // Se standalone: input = mic, output = speaker
    // Se VST su DAW: input = track input (utente deve routare mic nella traccia)
    void processBlock(juce::AudioBuffer<float>& buffer, bool isPlayingPink);

    // Stato per UI
    State getState() const { return state.load(); }
    float getProgress() const { return progress.load(); } // 0..1
    juce::String getStatusText() const;
    double getSecondsRemaining() const;

    // Risultato: gains per banda (-12..+12)
    struct Result
    {
        float gainsDB[EQProcessor::NumBands] = {0};
        float overallCorrection = 0; // dB medi
        juce::String summary;
        bool valid = false;
        // Curva misurata per display
        std::vector<std::pair<double,float>> measuredCurve; // freq, dB
        std::vector<std::pair<double,float>> correctionCurve;
    };

    Result getResult() const { juce::ScopedLock lk(resultLock); return result; }
    bool hasValidResult() const { juce::ScopedLock lk(resultLock); return result.valid; }

    // Applica risultato a EQProcessor con animazione gestita dall'editor
    void applyToEQ(EQProcessor& eq, float strength = 1.0f) const;

    void setMicProfile(int idx) { micProfileIdx = juce::jlimit(0, (int)MicProfile::getAllProfiles().size()-1, idx); }
    int getMicProfile() const { return micProfileIdx; }

    // Parametri calibrazione
    double pinkDurationSec = 6.0;  // come SE-9 ~5-7 sec
    float pinkGain = 0.35f;
    double silenceThresholdDB = -50.0;

private:
    void doAnalyze();

    std::atomic<State> state { State::Idle };
    std::atomic<float> progress { 0.0f };

    double sampleRate = 48000.0;
    int maxBlock = 512;

    int micProfileIdx = 0;

    // Pink generator
    PinkNoiseGenerator pinkGen;
    int pinkSamplesTotal = 0;
    int pinkSamplesPlayed = 0;

    // Capture buffer (mono sum ingresso)
    std::vector<float> captureBuf;
    int capturePos = 0;
    int captureCapacity = 0;

    // Analysis
    mutable juce::CriticalSection resultLock;
    Result result;
    juce::String errorMsg;

    juce::CriticalSection lock;
};
