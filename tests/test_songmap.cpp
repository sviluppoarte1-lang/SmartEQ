// Song-map test: bar-by-bar learning, follow lookup, persistence roundtrip.
#include "DSP/SongAnalyzer.h"
#include "DSP/IntelligentAnalyzer.h"
#include <cstdio>
#include <cmath>
#include <vector>

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void genBar(std::vector<float>& out, int n, double sr, bool bright)
{
    out.resize((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        double t = i / sr;
        float s;
        if (bright)
            s = 0.3f * (float) std::sin(2 * 3.14159265 * 8000 * t)
              + 0.2f * (float) std::sin(2 * 3.14159265 * 12000 * t)
              + 0.05f * (float) std::sin(2 * 3.14159265 * 110 * t);
        else
            s = 0.5f * (float) std::sin(2 * 3.14159265 * 110 * t)
              + 0.25f * (float) std::sin(2 * 3.14159265 * 220 * t)
              + 0.02f * (float) std::sin(2 * 3.14159265 * 8000 * t);
        out[(size_t) i] = s;
    }
}

static void pushAsBlocks(SongAnalyzer& sa, const std::vector<float>& audio,
                         double ppqStart, double ppqPerBlock, double bpm, double sr)
{
    const int bs = 512;
    double ppq = ppqStart;
    for (size_t pos = 0; pos < audio.size(); pos += bs)
    {
        int n = (int) juce::jmin<size_t>(bs, audio.size() - pos);
        sa.pushAudioBlock(audio.data() + pos, n, ppq, true, true, bpm, sr);
        ppq += ppqPerBlock;
    }
}

int main()
{
    const double sr = 48000.0, bpm = 120.0;
    // 1 bar 4/4 @120bpm = 2s = 96000 samples; ppq per 512-block = 512/48000*120/60
    const double ppqPerBlock = 512.0 / sr * bpm / 60.0;

    SongAnalyzer sa;
    sa.prepare(sr, 4);
    sa.setLearning(true);

    std::vector<float> dark, bright;
    genBar(dark, 96000, sr, false);
    genBar(bright, 96000, sr, true);

    pushAsBlocks(sa, dark, 0.0, ppqPerBlock, bpm, sr);   // bar 0
    pushAsBlocks(sa, bright, 4.0, ppqPerBlock, bpm, sr); // bar 1 (finalizes bar 0)
    pushAsBlocks(sa, dark, 8.0, ppqPerBlock, bpm, sr);   // bar 2 (finalizes bar 1)
    sa.setLearning(false);

    CHECK(sa.getNumBars() >= 2, "learned >=2 bars (got %d)", sa.getNumBars());

    float g0[SongAnalyzer::MaxSongBands] = { 0 }, g1[SongAnalyzer::MaxSongBands] = { 0 };
    CHECK(sa.getGainsForBar(0, g0), "lookup bar 0");
    CHECK(sa.getGainsForBar(1, g1), "lookup bar 1");

    float maxDiff = 0;
    for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i)
        maxDiff = juce::jmax(maxDiff, std::abs(g0[i] - g1[i]));
    printf("bar0 vs bar1 maxDiff %.2fdB\n", maxDiff);
    CHECK(maxDiff > 0.5f, "dark vs bright bars differ (%.2fdB)", maxDiff);

    float gp[SongAnalyzer::MaxSongBands] = { 0 };
    CHECK(sa.getGainsForPpq(1.0, gp), "ppq lookup");
    bool sameAsBar0 = true;
    for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i)
        if (std::abs(gp[i] - g0[i]) > 1e-4f) sameAsBar0 = false;
    CHECK(sameAsBar0, "ppq 1.0 resolves to bar 0");

    // Persistence roundtrip
    auto vt = sa.toValueTree();
    SongAnalyzer sa2;
    sa2.prepare(sr, 4);
    sa2.restoreFromValueTree(vt);
    CHECK(sa2.getNumBars() == sa.getNumBars(), "roundtrip numBars %d", sa2.getNumBars());
    float r0[SongAnalyzer::MaxSongBands] = { 0 };
    sa2.getGainsForBar(0, r0);
    bool same = true;
    for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i)
        if (std::abs(r0[i] - g0[i]) > 1e-4f) same = false;
    CHECK(same, "roundtrip gains identical");

    // Static import on empty map
    SongAnalyzer sa3;
    sa3.prepare(sr, 4);
    float flat[SongAnalyzer::MaxSongBands] = { 0 };
    flat[3] = -3.0f;
    sa3.importStaticMap(flat, SongAnalyzer::MaxSongBands);
    CHECK(sa3.hasData(), "static import creates map");
    float q[SongAnalyzer::MaxSongBands] = { 0 };
    sa3.getGainsForBar(0, q);
    CHECK(std::abs(q[3] + 3.0f) < 1e-4f, "static import preserves gains");

    // No-learn guard: silence must not create bars
    SongAnalyzer sa4;
    sa4.prepare(sr, 4);
    sa4.setLearning(false);
    pushAsBlocks(sa4, dark, 0.0, ppqPerBlock, bpm, sr);
    CHECK(! sa4.hasData(), "no map when learn off");

    // ---- Transient guard: stesso spettro, crest noto vs ignoto ----
    {
        IntelligentAnalyzer ia;
        ia.setSampleRate(sr);
        const int specN = ia.getSpectrumSize();
        std::vector<float> spec((size_t) specN, -60.0f);
        // bump 175-330 Hz (+12): scatta il mud detector
        for (int b = 15; b <= 28 && b < specN; ++b) spec[(size_t) b] = -48.0f;
        auto rFull = ia.analyzeSpectrumDB(spec.data());        // crest ignoto
        auto rTran = ia.analyzeSpectrumDB(spec.data(), 24.0f); // batteria
        auto rDense = ia.analyzeSpectrumDB(spec.data(), 8.0f); // mix denso
        printf("mud g250: full=%.2f dense=%.2f transient=%.2f\n",
               rFull.suggestedGains[5], rDense.suggestedGains[5], rTran.suggestedGains[5]);
        CHECK(rFull.suggestedGains[5] < -3.0f, "mud triggers on 250Hz bump (%.2f)", rFull.suggestedGains[5]);
        CHECK(std::abs(rFull.suggestedGains[5]) <= 4.0f + 1e-4f, "per-band cap +-4dB (%.2f)",
              rFull.suggestedGains[5]);
        CHECK(std::abs(rDense.suggestedGains[5] - rFull.suggestedGains[5]) < 1e-4f,
              "dense mix keeps full correction");
        CHECK(std::abs(rTran.suggestedGains[5]) < std::abs(rFull.suggestedGains[5]) * 0.5f,
              "transient material gets gentler cut (%.2f vs %.2f)",
              rTran.suggestedGains[5], rFull.suggestedGains[5]);
        CHECK(rTran.suggestedGains[5] < -0.3f, "transient still corrected, not zero (%.2f)",
              rTran.suggestedGains[5]);
    }

    // ---- Silence skip: pausa tra due sezioni non avvelena la mappa ----
    {
        SongAnalyzer ss;
        ss.prepare(sr, 4);
        ss.setLearning(true);
        std::vector<float> loud(96000), silence(96000, 0.0f);
        for (int i = 0; i < 96000; ++i)
            loud[(size_t) i] = 0.5f * (float) std::sin(2 * 3.14159265 * 110 * i / sr);
        pushAsBlocks(ss, loud, 0.0, ppqPerBlock, bpm, sr);
        pushAsBlocks(ss, silence, 4.0, ppqPerBlock, bpm, sr);
        pushAsBlocks(ss, loud, 8.0, ppqPerBlock, bpm, sr);
        ss.setLearning(false);
        ss.flush();
        CHECK(ss.getNumBars() == 3, "3 bars indexed (got %d)", ss.getNumBars());
        float gb0[SongAnalyzer::MaxSongBands] = { 0 }, gb1[SongAnalyzer::MaxSongBands] = { 0 };
        CHECK(ss.getGainsForBar(0, gb0), "lookup bar 0");
        CHECK(ss.getGainsForBar(1, gb1), "lookup skips silent bar 1");
        bool same = true;
        for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i)
            if (std::abs(gb1[i] - gb0[i]) > 1e-4f) same = false;
        CHECK(same, "silent bar resolves to musical neighbor");
        CHECK(ss.getStatusText().contains("2 musical"), "status counts musical bars");
    }

    // ---- Persistence compat: blob vecchio formato (senza flag) ancora valido ----
    {
        juce::MemoryBlock mb;
        int nb = 1, bar = 0;
        float gains[SongAnalyzer::MaxSongBands] = { 0 };
        gains[3] = -3.0f;
        float rms = -20.0f;
        mb.append(&nb, sizeof(nb));
        mb.append(&bar, sizeof(bar));
        mb.append(gains, sizeof(gains));
        mb.append(&rms, sizeof(rms));
        juce::ValueTree v("SongMap");
        v.setProperty("beatsPerBar", 4, nullptr);
        v.setProperty("numBars", 1, nullptr);
        v.setProperty("data", mb.toBase64Encoding(), nullptr);
        SongAnalyzer sc;
        sc.prepare(sr, 4);
        sc.restoreFromValueTree(v);
        CHECK(sc.getNumBars() == 1, "old blob loads");
        float q[SongAnalyzer::MaxSongBands] = { 0 };
        CHECK(sc.getGainsForBar(0, q), "old blob lookup works (treated musical)");
        CHECK(std::abs(q[3] + 3.0f) < 1e-4f, "old blob gains intact");
    }

    // ---- Migrazione: mappa patologica pre-budget (±9) sanata al load ----
    {
        SongAnalyzer sm;
        sm.prepare(sr, 4);
        float hot[SongAnalyzer::MaxSongBands] = { 0 };
        hot[12] = -9.0f; hot[13] = -9.0f; hot[5] = -5.0f;
        sm.importStaticMap(hot, SongAnalyzer::MaxSongBands);
        auto vt = sm.toValueTree();
        // corrompi il blob a mano per simulare la vecchia mappa
        SongAnalyzer sm2;
        sm2.prepare(sr, 4);
        sm2.restoreFromValueTree(vt);
        // (le bake correnti non superano mai ±4: verifica il budget anche qui)
        float q[SongAnalyzer::MaxSongBands] = { 0 };
        sm2.getGainsForBar(0, q);
        bool inBudget = true;
        for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i)
            if (std::abs(q[i]) > 4.0f + 1e-4f) inBudget = false;
        CHECK(inBudget, "map budget enforced after load");

        // vecchio stacking scritto a mano: deve rientrare a ±4
        juce::MemoryBlock mb;
        int nb = 1, bar = 0;
        float old[SongAnalyzer::MaxSongBands] = { 0 };
        old[12] = -9.0f; old[13] = -8.0f;
        float rms = -25.0f;
        mb.append(&nb, sizeof(nb));
        mb.append(&bar, sizeof(bar));
        mb.append(old, sizeof(old));
        mb.append(&rms, sizeof(rms));
        juce::ValueTree v("SongMap");
        v.setProperty("data", mb.toBase64Encoding(), nullptr);
        SongAnalyzer sm3;
        sm3.prepare(sr, 4);
        sm3.restoreFromValueTree(v);
        float w[SongAnalyzer::MaxSongBands] = { 0 };
        sm3.getGainsForBar(0, w);
        CHECK(std::abs(w[12] + 4.0f) < 1e-4f && std::abs(w[13] + 4.0f) < 1e-4f,
              "stale +-9dB map healed to +-4 on load (%.2f %.2f)", w[12], w[13]);
    }

    printf(fails == 0 ? "SONGMAP ALL OK\n" : "SONGMAP %d FAILURES\n", fails);
    return fails;
}
