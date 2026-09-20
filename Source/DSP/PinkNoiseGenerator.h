#pragma once
#include <JuceHeader.h>

// Generatore rumore rosa - filtro Paul Kellet 1-pole (approssimazione -3dB/oct)
class PinkNoiseGenerator
{
public:
    PinkNoiseGenerator() { reset(); }

    void reset()
    {
        b0=b1=b2=b3=b4=b5=b6=0.0;
        rng = juce::Random(0x1234567);
    }

    void prepare(double /*sr*/) { reset(); }

    inline float nextSample()
    {
        float white = rng.nextFloat() * 2.0f - 1.0f;

        // Paul Kellet pink filter
        b0 = 0.99886f * b0 + white * 0.0555179f;
        b1 = 0.99332f * b1 + white * 0.0750759f;
        b2 = 0.96900f * b2 + white * 0.1538520f;
        b3 = 0.86650f * b3 + white * 0.3104856f;
        b4 = 0.55000f * b4 + white * 0.5329522f;
        b5 = -0.7616f * b5 - white * 0.0168980f;

        float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.11f;
        b6 = white * 0.115926f;

        // Hard clip safety
        return juce::jlimit(-0.95f, 0.95f, pink);
    }

    void fillBuffer(float* buf, int numSamples, float gain = 0.4f)
    {
        for (int i=0;i<numSamples;++i) buf[i] = nextSample() * gain;
    }

private:
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    juce::Random rng;
};
