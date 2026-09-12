#include "EQProcessor.h"

EQProcessor::EQProcessor()
{
    for (int i = 0; i < NumBands; ++i)
    {
        bands[i].freq = defaultFreqs[i];
        bands[i].gainDB = 0.0f;
        bands[i].enabled = true;

        // Tipi: prima banda LowShelf, ultima HighShelf, intermedie Peak.
        // Shelf bands default to Q 0.71 (Butterworth-flat shelf slope):
        // higher Q values add resonance on purpose.
        if (i == 0) { bands[i].type = Biquad::LowShelf; bands[i].Q = 0.71; }
        else if (i == NumBands-1) { bands[i].type = Biquad::HighShelf; bands[i].Q = 0.71; }
        else { bands[i].type = Biquad::Peak; bands[i].Q = 1.4; }
    }
}

void EQProcessor::prepare(double sr, int /*maxBlockSize*/)
{
    sampleRate = sr;
    for (int i = 0; i < NumBands; ++i)
    {
        bands[i].prepare(sr);
    }
}

void EQProcessor::reset()
{
    for (auto& b : bands) { b.filterL.reset(); b.filterR.reset(); }
}

void EQProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumChannels() == 0) return;

    // Processa banda per banda in serie (cascata)
    // Usiamo float* diretto per efficienza
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    if (numCh == 1)
    {
        float* ch0 = buffer.getWritePointer(0);
        for (int i = 0; i < NumBands; ++i)
        {
            if (bands[i].isUnity()) continue;
            for (int s = 0; s < numSamples; ++s)
                ch0[s] = bands[i].processSampleL(ch0[s]);
        }
    }
    else
    {
        float* left  = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        for (int i = 0; i < NumBands; ++i)
        {
            if (bands[i].isUnity()) continue;
            bands[i].processBlock(left, right, numSamples);
        }
        // Extra channels reuse left cascade
        for (int ch = 2; ch < numCh; ++ch)
        {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < NumBands; ++i)
            {
                if (bands[i].isUnity()) continue;
                for (int s = 0; s < numSamples; ++s)
                    data[s] = bands[i].processSampleL(data[s]);
            }
        }
    }
}

void EQProcessor::setBandFreq(int idx, double f) { bands[idx].freq = f; updateBand(idx); }
void EQProcessor::setBandGain(int idx, double g) { bands[idx].gainDB = g; updateBand(idx); }
void EQProcessor::setBandQ(int idx, double q)    { bands[idx].Q = q; updateBand(idx); }
void EQProcessor::setBandType(int idx, Biquad::Type t) { bands[idx].type = t; updateBand(idx); }
void EQProcessor::setBandEnabled(int idx, bool en) { bands[idx].enabled = en; updateBand(idx); }

void EQProcessor::updateBand(int idx)
{
    bands[idx].updateCoefficients(sampleRate);
}

float EQProcessor::getMagnitudeForFrequency(double freq) const
{
    float mag = 1.0f;
    for (int i = 0; i < NumBands; ++i)
        mag *= bands[i].getMagnitude(freq, sampleRate);
    return mag;
}

void EQProcessor::getCurve(float* magnitudesDB, const double* freqs, int numPoints) const
{
    for (int p = 0; p < numPoints; ++p)
    {
        float m = getMagnitudeForFrequency(freqs[p]);
        magnitudesDB[p] = juce::Decibels::gainToDecibels(m);
    }
}

void EQProcessor::resetAllBands()
{
    for (int i = 0; i < NumBands; ++i)
    {
        bands[i].gainDB = 0.0;
        bands[i].Q = (i == 0 || i == NumBands - 1) ? 0.71 : 1.4; // flat shelves
        bands[i].enabled = true;
        updateBand(i);
    }
}

void EQProcessor::applyGains(const float gainsDB[NumBands])
{
    for (int i = 0; i < NumBands; ++i)
    {
        bands[i].gainDB = juce::jlimit(-18.0f, 18.0f, gainsDB[i]);
        updateBand(i);
    }
}
