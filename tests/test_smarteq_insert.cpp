// SmartEQ insert test: fresh instance must process at full level from sample 0.
// No fade-in / crescendo allowed - neither flat nor with hot low shelves.
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double rmsOf(juce::AudioBuffer<float>& b, int from, int n)
{
    double sum = 0;
    for (int i = from; i < from + n; ++i) { float v = b.getSample(0, i); sum += v * v; }
    return 10 * std::log10(sum / n + 1e-12);
}

static void fillSine(juce::AudioBuffer<float>& b, float amp, float freq, double sr)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(ch, i, amp * std::sin(2 * 3.14159265f * freq * i / (float)sr));
}

static void setGain(SmartEQAudioProcessor& p, int band, float db)
{
    if (auto* par = p.apvts.getParameter("band" + juce::String(band) + "_gain"))
        par->setValueNotifyingHost(par->convertTo0to1(db));
}

int main()
{
    juce::MidiBuffer midi;

    // ---- 1. Flat default: first block must equal settled block ----
    {
        SmartEQAudioProcessor proc;
        proc.prepareToPlay(48000, 512);
        juce::AudioBuffer<float> b1(2, 512), b2(2, 512);
        fillSine(b1, 0.5f, 440.f, 48000);
        fillSine(b2, 0.5f, 440.f, 48000);
        proc.processBlock(b1, midi); // insert moment
        proc.processBlock(b2, midi); // settled
        double r1 = rmsOf(b1, 0, 64), r2 = rmsOf(b2, 0, 64);
        printf("flat: first64 %.2fdB settled64 %.2fdB\n", r1, r2);
        CHECK(std::abs(r1 - r2) < 0.3, "flat insert: no ramp (diff %.2fdB)", std::abs(r1 - r2));
        CHECK(std::isfinite(b1.getSample(0, 0)), "flat insert: finite sample 0");
    }

    // ---- 2. Hot TRUE shelves (band 0 LowShelf 25Hz +9, band 15 HighShelf +6):
    // cosine start = full amplitude at sample 0 (worst case), same stream ----
    {
        SmartEQAudioProcessor proc;
        proc.prepareToPlay(48000, 512);
        setGain(proc, 0, 9.0f);   // 25 Hz low shelf +9 (true shelf band)
        setGain(proc, 15, 6.0f);  // 20k high shelf +6
        setGain(proc, 8, -6.0f);  // 1k bell -6
        const int N = 512 * 94; // ~1 second, multiple of block size, same stream
        juce::AudioBuffer<float> b(2, N);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < N; ++i)
                b.setSample(ch, i, 0.5f * std::cos(2 * 3.14159265f * 55.f * i / 48000.f));
        // process in 512-blocks like a DAW
        juce::AudioBuffer<float> blk(2, 512);
        for (int off = 0; off < N; off += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) blk.setSample(ch, i, b.getSample(ch, off + i));
            proc.processBlock(blk, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i) b.setSample(ch, off + i, blk.getSample(ch, i));
        }
        double rFirst = rmsOf(b, 0, 512);          // first block (insert moment)
        double rSettled = rmsOf(b, N - 4096, 4096); // settled tail
        printf("shelf: first512 %.2fdB settled %.2fdB\n", rFirst, rSettled);
        CHECK(std::abs(rFirst - rSettled) < 1.5, "shelf insert: no swell (diff %.2fdB)", std::abs(rFirst - rSettled));
    }

    // ---- 3. processBlock BEFORE prepareToPlay (host order): must stay finite/flat ----
    {
        SmartEQAudioProcessor proc; // no prepare!
        juce::AudioBuffer<float> b(2, 256);
        fillSine(b, 0.5f, 440.f, 48000);
        proc.processBlock(b, midi);
        bool bad = false;
        for (int i = 0; i < 256; ++i) if (!std::isfinite(b.getSample(0, i))) bad = true;
        double r = rmsOf(b, 0, 64);
        printf("no-prepare: first64 %.2fdB (expect ~-9.0)\n", r);
        CHECK(!bad && std::abs(r + 9.03) < 1.0, "pre-prepare block: unity passthrough, no ramp");
    }

    printf(fails == 0 ? "SMARTEQ INSERT TESTS PASSED\n" : "FAILURES: %d\n", fails);
    return fails;
}
