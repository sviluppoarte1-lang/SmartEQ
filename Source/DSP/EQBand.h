#pragma once
#include <JuceHeader.h>
#include <cmath>

// Biquad RBJ Cookbook - all professional filter types
// Slopes: 12dB/oct = 1 biquad, 24dB/oct = 2 cascaded biquads
class Biquad
{
public:
    enum Type
    {
        Peak = 0,       // Parametric bell (gain in dB)
        LowShelf,       // Low shelf (gain in dB)
        HighShelf,      // High shelf (gain in dB)
        LowPass12,      // Low-pass 12 dB/oct (Q = resonance)
        LowPass24,      // Low-pass 24 dB/oct (2 stages)
        HighPass12,     // High-pass 12 dB/oct
        HighPass24,     // High-pass 24 dB/oct (2 stages)
        BandPass,       // Band-pass (constant skirt gain)
        Notch,          // Notch (band reject)
        AllPass,        // All-pass (phase only, for phase correction)
        NumTypes
    };

    static const char* getTypeName(Type t)
    {
        switch (t)
        {
            case Peak:      return "Bell";
            case LowShelf:  return "Low Shelf";
            case HighShelf: return "High Shelf";
            case LowPass12: return "Low Pass 12";
            case LowPass24: return "Low Pass 24";
            case HighPass12:return "High Pass 12";
            case HighPass24:return "High Pass 24";
            case BandPass:  return "Band Pass";
            case Notch:     return "Notch";
            case AllPass:   return "All Pass";
            default:        return "?";
        }
    }

    static juce::StringArray getTypeNames()
    {
        return { "Bell", "Low Shelf", "High Shelf",
                 "Low Pass 12", "Low Pass 24",
                 "High Pass 12", "High Pass 24",
                 "Band Pass", "Notch", "All Pass" };
    }

    // True if this type uses the Gain parameter (bell/shelves only)
    static bool usesGain(Type t)
    {
        return t == Peak || t == LowShelf || t == HighShelf;
    }

    // True if this type needs 2 cascaded biquads (24 dB/oct)
    static bool is24dB(Type t) { return t == LowPass24 || t == HighPass24; }

    Biquad() { reset(); }

    void reset()
    {
        // Clear delay lines ONLY - coefficients are owned by updateCoefficients/
        // setUnity. (Resetting coeffs here used to silently flatten the EQ.)
        s1 = s2 = 0.0f;
    }

    void setCoefficients(Type type, double sampleRate, double freq, double Q, double gainDB)
    {
        // Resolve 24dB variants to their 12dB prototype for single-stage calc.
        // The caller cascades two stages for the 24dB versions.
        Type proto = type;
        if (type == LowPass24)  proto = LowPass12;
        if (type == HighPass24) proto = HighPass12;

        const double A = std::pow(10.0, gainDB / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * Q);
        const double sqrtA = std::sqrt(A);

        double b0_, b1_, b2_, a0_, a1_, a2_;

        switch (proto)
        {
            case Peak:
            {
                b0_ = 1 + alpha * A;
                b1_ = -2 * cosw0;
                b2_ = 1 - alpha * A;
                a0_ = 1 + alpha / A;
                a1_ = -2 * cosw0;
                a2_ = 1 - alpha / A;
                break;
            }
            case LowShelf:
            {
                // Proper RBJ shelf slope: the Q knob doubles as shelf slope S.
                // S = 1 is Butterworth-flat; higher S rings. Clamped so the
                // default Q (0.71 on shelf bands) stays flat and musical.
                const double S = juce::jlimit(0.1, 3.0, Q);
                const double alphaS = sinw0 / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
                const double twoSqrtA_alpha = 2 * sqrtA * alphaS;
                b0_ = A * ((A + 1) - (A - 1) * cosw0 + twoSqrtA_alpha);
                b1_ = 2 * A * ((A - 1) - (A + 1) * cosw0);
                b2_ = A * ((A + 1) - (A - 1) * cosw0 - twoSqrtA_alpha);
                a0_ = (A + 1) + (A - 1) * cosw0 + twoSqrtA_alpha;
                a1_ = -2 * ((A - 1) + (A + 1) * cosw0);
                a2_ = (A + 1) + (A - 1) * cosw0 - twoSqrtA_alpha;
                break;
            }
            case HighShelf:
            {
                const double S = juce::jlimit(0.1, 3.0, Q);
                const double alphaS = sinw0 / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
                const double twoSqrtA_alpha = 2 * sqrtA * alphaS;
                b0_ = A * ((A + 1) + (A - 1) * cosw0 + twoSqrtA_alpha);
                b1_ = -2 * A * ((A - 1) + (A + 1) * cosw0);
                b2_ = A * ((A + 1) + (A - 1) * cosw0 - twoSqrtA_alpha);
                a0_ = (A + 1) - (A - 1) * cosw0 + twoSqrtA_alpha;
                a1_ = 2 * ((A - 1) - (A + 1) * cosw0);
                a2_ = (A + 1) - (A - 1) * cosw0 - twoSqrtA_alpha;
                break;
            }
            case LowPass12:
            case LowPass24:
            {
                b0_ = (1 - cosw0) / 2;
                b1_ = 1 - cosw0;
                b2_ = (1 - cosw0) / 2;
                a0_ = 1 + alpha;
                a1_ = -2 * cosw0;
                a2_ = 1 - alpha;
                break;
            }
            case HighPass12:
            case HighPass24:
            {
                b0_ = (1 + cosw0) / 2;
                b1_ = -(1 + cosw0);
                b2_ = (1 + cosw0) / 2;
                a0_ = 1 + alpha;
                a1_ = -2 * cosw0;
                a2_ = 1 - alpha;
                break;
            }
            case BandPass:
            {
                b0_ = alpha;
                b1_ = 0;
                b2_ = -alpha;
                a0_ = 1 + alpha;
                a1_ = -2 * cosw0;
                a2_ = 1 - alpha;
                break;
            }
            case Notch:
            {
                b0_ = 1;
                b1_ = -2 * cosw0;
                b2_ = 1;
                a0_ = 1 + alpha;
                a1_ = -2 * cosw0;
                a2_ = 1 - alpha;
                break;
            }
            case AllPass:
            {
                b0_ = 1 - alpha;
                b1_ = -2 * cosw0;
                b2_ = 1 + alpha;
                a0_ = 1 + alpha;
                a1_ = -2 * cosw0;
                a2_ = 1 - alpha;
                break;
            }
            default:
            {
                b0_ = 1; b1_ = 0; b2_ = 0;
                a0_ = 1; a1_ = 0; a2_ = 0;
                break;
            }
        }

        b0 = float(b0_ / a0_);
        b1 = float(b1_ / a0_);
        b2 = float(b2_ / a0_);
        a1 = float(a1_ / a0_);
        a2 = float(a2_ / a0_);
    }

    // Transposed Direct Form II - stable, denormal-safe
    inline float processSampleTDF2(float x) noexcept
    {
        float out = b0 * x + s1;
        s1 = b1 * x - a1 * out + s2;
        s2 = b2 * x - a2 * out;
        if (std::abs(s1) < 1e-20f) s1 = 0;
        if (std::abs(s2) < 1e-20f) s2 = 0;
        return out;
    }

    float getMagnitudeForFrequency(double freq, double sampleRate) const
    {
        double w = 2 * juce::MathConstants<double>::pi * freq / sampleRate;
        std::complex<double> ejw = std::exp(std::complex<double>(0, -w));
        std::complex<double> ej2w = std::exp(std::complex<double>(0, -2*w));
        std::complex<double> num = (double)b0 + (double)b1 * ejw + (double)b2 * ej2w;
        std::complex<double> den = 1.0 + (double)a1 * ejw + (double)a2 * ej2w;
        auto mag = std::abs(num / den);
        return float(mag);
    }

private:
    float b0=1, b1=0, b2=0, a1=0, a2=0;
    float s1=0, s2=0;
};

// Single EQ band with up to 2 cascaded stages (for 24 dB/oct slopes)
struct EQBand
{
    Biquad::Type type = Biquad::Type::Peak;
    double freq = 1000.0;
    double Q = 1.0;
    double gainDB = 0.0;
    bool enabled = true;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedGain;

    Biquad filterL, filterR;
    Biquad filterL2, filterR2; // second stage for LP24/HP24

    bool usesSecondStage() const { return Biquad::is24dB(type); }

    // Unity bypass coefficients (flat)
    static void setUnity(Biquad& f, double sampleRate)
    {
        f.setCoefficients(Biquad::Peak, sampleRate, 1000, 0.707, 0.0);
    }

    void prepare(double sampleRate)
    {
        smoothedGain.reset(sampleRate, 0.05);
        smoothedGain.setCurrentAndTargetValue((float)gainDB);
        updateCoefficients(sampleRate);
        filterL.reset(); filterR.reset();
        filterL2.reset(); filterR2.reset();
    }

    bool isUnity() const
    {
        if (!enabled) return true;
        // Gain-based filters at 0 dB are flat
        if (Biquad::usesGain(type) && juce::approximatelyEqual(gainDB, 0.0))
            return true;
        // AllPass is never unity in phase, but ~flat in magnitude; keep processing
        if (type == Biquad::AllPass) return false;
        return false;
    }

    void updateCoefficients(double sampleRate)
    {
        if (isUnity())
        {
            setUnity(filterL, sampleRate);
            setUnity(filterR, sampleRate);
            setUnity(filterL2, sampleRate);
            setUnity(filterR2, sampleRate);
            return;
        }
        double f = juce::jlimit(10.0, sampleRate * 0.48, freq);
        double q = juce::jlimit(0.1, 18.0, Q);
        // For cut filters gain is ignored - force 0 dB into prototype
        double g = Biquad::usesGain(type) ? gainDB : 0.0;
        filterL.setCoefficients(type, sampleRate, f, q, g);
        filterR.setCoefficients(type, sampleRate, f, q, g);
        if (usesSecondStage())
        {
            // Second stage: same freq, Butterworth Q for proper 24dB Linkwitz-Riley-ish slope
            filterL2.setCoefficients(type, sampleRate, f, 0.707, g);
            filterR2.setCoefficients(type, sampleRate, f, 0.707, g);
        }
        else
        {
            setUnity(filterL2, sampleRate);
            setUnity(filterR2, sampleRate);
        }
    }

    inline float processSampleL(float x) noexcept
    {
        x = filterL.processSampleTDF2(x);
        if (usesSecondStage()) x = filterL2.processSampleTDF2(x);
        return x;
    }
    inline float processSampleR(float x) noexcept
    {
        x = filterR.processSampleTDF2(x);
        if (usesSecondStage()) x = filterR2.processSampleTDF2(x);
        return x;
    }

    inline void processBlock(float* left, float* right, int numSamples) noexcept
    {
        if (isUnity()) return;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = processSampleL(left[i]);
            right[i] = processSampleR(right[i]);
        }
    }

    float getMagnitude(double f, double sr) const
    {
        if (isUnity()) return 1.0f;
        float m = filterL.getMagnitudeForFrequency(f, sr);
        if (usesSecondStage()) m *= filterL2.getMagnitudeForFrequency(f, sr);
        return m;
    }
};
