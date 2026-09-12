#pragma once
#include <JuceHeader.h>

// Analizzatore di spettro professionale real-time per UI
class SpectrumAnalyzer
{
public:
    SpectrumAnalyzer();
    ~SpectrumAnalyzer() = default;

    void prepare(double sampleRate, int blockSize);
    void pushBuffer(const juce::AudioBuffer<float>& buffer);
    void getSpectrum(float* outDB, int numPoints); // interpolato log

    // Configurazione
    void setFFTSize(int order); // 10=1024 ... 12=4096
    float getPeakDBAtFreq(double freq) const;
    bool isReady() const { return fifoReady; }

    // Listener per UI
    std::function<void()> onSpectrumUpdated;

private:
    static constexpr int maxFFTOrder = 13; //8192
    static constexpr int maxFFTSize = 1 << maxFFTOrder;

    int fftOrder = 11; //2048 default bilanciato
    int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    juce::AudioBuffer<float> fifoBuffer;
    int fifoIndex = 0;
    bool fifoReady = false;

    float fftData[maxFFTSize * 2] = {0};
    float spectrumDB[maxFFTSize/2] = {-100};
    float smoothedDB[maxFFTSize/2] = {-100};

    double sampleRate = 48000;
    juce::CriticalSection lock;

    void processFFT();

    // decay per visual gradevole
    float decayRate = 0.6f; // dB per frame
};
