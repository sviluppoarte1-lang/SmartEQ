#include "SpectrumAnalyzer.h"

SpectrumAnalyzer::SpectrumAnalyzer()
{
    fifoBuffer.setSize(1, maxFFTSize);
    fifoBuffer.clear();
    for (int i=0;i<maxFFTSize/2;++i) { spectrumDB[i] = -100; smoothedDB[i]= -100; }
    window = std::make_unique<juce::dsp::WindowingFunction<float>>((size_t)fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
}

void SpectrumAnalyzer::prepare(double sr, int /*blockSize*/)
{
    sampleRate = sr;
    fifoIndex = 0;
    fifoReady = false;
    fifoBuffer.clear();
    if (!window)
        window = std::make_unique<juce::dsp::WindowingFunction<float>>((size_t)fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
}

void SpectrumAnalyzer::setFFTSize(int order)
{
    order = juce::jlimit(10, maxFFTOrder, order);
    if (order == fftOrder) return;
    fftOrder = order;
    fftSize = 1 << fftOrder;
    fft = juce::dsp::FFT(fftOrder);
    window = std::make_unique<juce::dsp::WindowingFunction<float>>((size_t)fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris);
    fifoReady = false;
}

void SpectrumAnalyzer::pushBuffer(const juce::AudioBuffer<float>& buffer)
{
    const float* data = buffer.getReadPointer(0);
    int numSamples = buffer.getNumSamples();
    // mono mix se stereo
    juce::ScopedLock sl(lock);
    for (int i=0;i<numSamples;++i)
    {
        float s = data[i];
        if (buffer.getNumChannels() > 1) s = (s + buffer.getReadPointer(1)[i]) * 0.5f;
        fifoBuffer.setSample(0, fifoIndex, s);
        fifoIndex++;
        if (fifoIndex >= fftSize)
        {
            fifoIndex = 0;
            fifoReady = true;
            processFFT();
        }
    }
}

void SpectrumAnalyzer::processFFT()
{
    // Copia windowed
    for (int i=0;i<fftSize;++i) fftData[i] = fifoBuffer.getSample(0,i);
    if (window) window->multiplyWithWindowingTable(fftData, (size_t)fftSize);
    // zero pad resto
    for (int i=fftSize;i<maxFFTSize*2;++i) fftData[i]=0;

    fft.performFrequencyOnlyForwardTransform(fftData);

    for (int i=0;i<fftSize/2;++i)
    {
        float mag = fftData[i];
        // normalizzazione per FFT size e finestra
        mag = mag / (fftSize * 0.5f);
        float db = juce::Decibels::gainToDecibels(mag + 1e-9f);
        db = juce::jlimit(-120.f, 24.f, db);
        // smoothing con decay/attack
        if (db > smoothedDB[i]) smoothedDB[i] = db; // attack immediato
        else smoothedDB[i] = smoothedDB[i] - decayRate;
        if (smoothedDB[i] < -120) smoothedDB[i] = -120;
        spectrumDB[i] = smoothedDB[i];
    }
    if (onSpectrumUpdated) onSpectrumUpdated();
}

void SpectrumAnalyzer::getSpectrum(float* outDB, int numPoints)
{
    juce::ScopedLock sl(lock);
    // Interpola log da 20Hz a 20kHz
    for (int p=0;p<numPoints;++p)
    {
        double t = (double)p / (numPoints-1);
        double freq = 20.0 * std::pow(1000.0, t); // 20 to 20000 log
        double bin = freq * fftSize / sampleRate;
        int binLow = (int)std::floor(bin);
        int binHigh = juce::jmin(fftSize/2 -1, binLow+1);
        float frac = float(bin - binLow);
        float dbLow = (binLow>=0 && binLow < fftSize/2) ? spectrumDB[binLow] : -100;
        float dbHigh= (binHigh>=0 && binHigh< fftSize/2) ? spectrumDB[binHigh]: -100;
        outDB[p] = dbLow * (1-frac) + dbHigh * frac;
    }
}

float SpectrumAnalyzer::getPeakDBAtFreq(double freq) const
{
    int bin = (int)(freq * fftSize / sampleRate);
    bin = juce::jlimit(0, fftSize/2 -1, bin);
    return spectrumDB[bin];
}
