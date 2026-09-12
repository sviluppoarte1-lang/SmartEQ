// Song-follow smoke: restore map -> FOLLOW processes finite audio,
// state save/load preserves the map.
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void setParam(SmartEQAudioProcessor& p, const char* id, float v)
{
    if (auto* par = p.apvts.getParameter(id))
        par->setValueNotifyingHost(par->convertTo0to1(v));
}

int main()
{
    juce::MidiBuffer midi;

    // Build a 2-bar map offline
    SongAnalyzer sa;
    sa.prepare(48000.0, 4);
    sa.setLearning(true);
    std::vector<float> blk(512);
    for (int b = 0; b < 200; ++b) // ~2.1s dark @ppq ramp 0..4
    {
        for (int i = 0; i < 512; ++i)
            blk[(size_t) i] = 0.4f * std::sin(2 * 3.14159265f * 110 * (b * 512 + i) / 48000.0f);
        sa.pushAudioBlock(blk.data(), 512, b * 0.0213, true, true, 120.0, 48000.0);
    }
    for (int b = 0; b < 200; ++b) // ~2.1s bright, next bar
    {
        for (int i = 0; i < 512; ++i)
        {
            double t = (b * 512 + i) / 48000.0;
            blk[(size_t) i] = 0.3f * std::sin(2 * 3.14159265f * 8000 * t);
        }
        sa.pushAudioBlock(blk.data(), 512, 4.0 + b * 0.0213, true, true, 120.0, 48000.0);
    }
    sa.setLearning(false);
    CHECK(sa.getNumBars() >= 2, "offline map has %d bars", sa.getNumBars());

    SmartEQAudioProcessor proc;
    proc.prepareToPlay(48000, 512);
    proc.getSongAnalyzer().restoreFromValueTree(sa.toValueTree());
    setParam(proc, "songFollow", 1.0f);
    setParam(proc, "songGlide", 50.0f);

    juce::AudioBuffer<float> buf(2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buf.setSample(ch, i, 0.5f * std::sin(2 * 3.14159265f * 440 * i / 48000.0f));
    for (int k = 0; k < 20; ++k) proc.processBlock(buf, midi); // no playhead -> bar 0 fallback

    bool finite = true;
    for (int ch = 0; ch < 2 && finite; ++ch)
        for (int i = 0; i < 512; ++i)
            if (! std::isfinite(buf.getSample(ch, i))) finite = false;
    CHECK(finite, "follow output finite (no playhead, bar-0 fallback)");

    // State roundtrip preserves map
    juce::MemoryBlock mb;
    proc.getStateInformation(mb);
    SmartEQAudioProcessor proc2;
    proc2.prepareToPlay(48000, 512);
    proc2.setStateInformation(mb.getData(), (int) mb.getSize());
    CHECK(proc2.getSongAnalyzer().getNumBars() == sa.getNumBars(),
          "map survives preset save/load (%d bars)", proc2.getSongAnalyzer().getNumBars());

    printf(fails == 0 ? "SONGFOLLOW ALL OK\n" : "SONGFOLLOW %d FAILURES\n", fails);
    return fails;
}
