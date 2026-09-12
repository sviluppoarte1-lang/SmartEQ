#pragma once
#include "EQBand.h"

class EQProcessor
{
public:
#ifdef SMARTEQ_FREE_VERSION
    static constexpr int NumBands = 8;   // Free edition: 8 bands, no analyzer
#else
    static constexpr int NumBands = 16;  // Full edition: 16 bands + analyzer
#endif

    EQProcessor();
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void processBlock(juce::AudioBuffer<float>& buffer);

    // Accesso bande
    EQBand& getBand(int idx) { jassert(idx >=0 && idx < NumBands); return bands[idx]; }
    const EQBand& getBand(int idx) const { jassert(idx >=0 && idx < NumBands); return bands[idx]; }

    void setBandFreq(int idx, double freq);
    void setBandGain(int idx, double gainDB);
    void setBandQ(int idx, double Q);
    void setBandType(int idx, Biquad::Type type);
    void setBandEnabled(int idx, bool en);

    // Curva di risposta per UI (magnitudine in dB per freq log)
    float getMagnitudeForFrequency(double freq) const;
    void getCurve(float* magnitudesDB, const double* freqs, int numPoints) const;

    // Preset helpers
    void resetAllBands();
    void applyGains(const float gainsDB[NumBands]);

    double getSampleRate() const { return sampleRate; }

    // Default ISO frequencies: 25..20k (full) or 63..10k musical spread (free 8-band)
#ifdef SMARTEQ_FREE_VERSION
    static constexpr double defaultFreqs[NumBands] = {
        63, 160, 400, 1000, 2500, 4000, 6300, 10000
    };
#else
    static constexpr double defaultFreqs[NumBands] = {
        25, 40, 63, 100, 160, 250, 400, 630,
        1000, 1600, 2500, 4000, 6300, 10000, 16000, 20000
    };
#endif

private:
    void updateBand(int idx);

    EQBand bands[NumBands];
    double sampleRate = 48000.0;
};
