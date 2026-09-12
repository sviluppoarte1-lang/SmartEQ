// Resonance harmonic-veto verification: musical harmonic stacks must NOT
// earn narrow auto-cuts, inharmonic rings/screams MUST be flagged.
// Dense spectra only (sparse tones break dB-average statistics).
#include "DSP/IntelligentAnalyzer.h"
#include <cstdio>
#include <cmath>
#include <functional>

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double sr = 48000.0;
static unsigned rngState = 4242;
static float white() { rngState = rngState * 1664525u + 1013904223u; return ((rngState >> 16) / 32768.0f - 1.0f); }

struct Res { int nRes; bool near220, near440, near660, near880, near2500, near2090; float diff2500, diff2090; int sev2090; };

static Res runCase(const char* tag, std::function<float(double)> fn)
{
    IntelligentAnalyzer a;
    const int N = (int)(4.0 * sr);
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = fn(i / sr);
    double ms = 0;
    for (float s : buf) ms += s * s;
    float g = (float)(0.2 / std::sqrt(ms / N + 1e-12));
    for (float& s : buf) s *= g; // common level: veto must judge HARMONY, not level
    for (int i = 0; i < N; i += 1024)
        a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
    auto r = a.analyze();
    Res res { 0, false,false,false,false,false,false, 0, 0, -1 };
    printf("[%s] %d issues:\n", tag, r.issues.size());
    for (auto& iss : r.issues)
    {
        auto rs = iss.reason.toLowerCase();
        if (!rs.contains("narrow resonance")) continue;
        ++res.nRes;
        double f = iss.freq;
        printf("    res %6.0fHz %+.1fdB sev%d gain%+.2f\n", f, iss.detectedLevelDB,
               (int)iss.severity, iss.suggestedGainDB);
        auto near = [&](double t) { return std::abs(f - t) / t < 0.025; };
        if (near(220)) res.near220 = true;
        if (near(440)) res.near440 = true;
        if (near(660)) res.near660 = true;
        if (near(880)) res.near880 = true;
        if (near(2500)) { res.near2500 = true; res.diff2500 = iss.detectedLevelDB; }
        if (near(2090)) { res.near2090 = true; res.diff2090 = iss.detectedLevelDB; res.sev2090 = (int)iss.severity; }
    }
    return res;
}

static float vossPink()
{
    static unsigned counters[16] = {0};
    static float vals[16] = {0};
    static unsigned cnt = 0;
    ++cnt;
    for (int i = 0; i < 16; ++i)
        if ((cnt & ((1u << (i + 1)) - 1)) == 0) vals[i] = white();
    float sum = 0;
    for (int i = 0; i < 16; ++i) sum += vals[i];
    return sum / 8.0f;
}

int main()
{
    // Cello-like complex: f0=110 + 15 harmonics (1/n) + pink wash.
    // A dense, truly harmonic signal: partials in the +8..14 zone MUST be
    // vetoed (they are music), while an inharmonic intruder still flags.
    auto cello = [&](double t, float hotFreq, float hotAmp) {
        float v = vossPink() * 0.35f;
        for (int n = 1; n <= 15; ++n)
            v += (0.22f / n) * std::sin(2*3.14159*110*n*t);
        if (hotAmp > 0) v += hotAmp * std::sin(2*3.14159*hotFreq*t);
        return v;
    };
    auto musical = runCase("cello", [&](double t) { return cello(t, 0, 0); });
    // Inharmonic intruder 2917 Hz: >50 Hz from every 110-multiple and wash peak
    auto ring = runCase("ring", [&](double t) { return cello(t, 2917, 0.30f); });

    // Middle partials (440..990) sitting in the veto zone must earn NO cuts
    bool midsClean = true;
    {
        IntelligentAnalyzer a;
        const int N = (int)(4.0 * sr);
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = cello(i / sr, 0, 0);
        for (int i = 0; i < N; i += 1024)
            a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
        auto r = a.analyze();
        printf("[cello-mids] veto-zone check:\n");
        for (auto& iss : r.issues)
        {
            if (!iss.reason.toLowerCase().contains("narrow resonance")) continue;
            double f = iss.freq;
            bool isMidPartial = false;
            for (int m = 4; m <= 9; ++m)
                if (std::abs(f - 110*m) / (110*m) < 0.025) isMidPartial = true;
            printf("    res %6.0fHz %+.1fdB %s\n", f, iss.detectedLevelDB,
                   isMidPartial ? "(MID partial)" : "");
            if (isMidPartial && iss.detectedLevelDB < 14.0f) midsClean = false;
        }
    }
    CHECK(midsClean, "harmonic mid-partials in +8..14 zone earn no cuts");

    // ring intruder near 2917 must be flagged
    {
        bool found = false; float d = 0;
        IntelligentAnalyzer a;
        const int N = (int)(4.0 * sr);
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = cello(i / sr, 2917, 0.30f);
        for (int i = 0; i < N; i += 1024)
            a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
        auto r = a.analyze();
        for (auto& iss : r.issues)
        {
            if (!iss.reason.toLowerCase().contains("narrow resonance")) continue;
            if (std::abs(iss.freq - 2917) / 2917 < 0.04) { found = true; d = iss.detectedLevelDB; }
        }
        CHECK(found && d > 8.0f, "inharmonic intruder flagged (diff %.1f)", d);
    }

    // Tier-2 subharmonic veto: weak 13th partial (1418 Hz) over a STRONG
    // 110 Hz fundamental must read as music, not a defect - while the SAME
    // peak alone (no harmonic context) must still flag. The contrast proves
    // the veto is contextual, not a threshold trick.
    auto run1418 = [&](bool withFundamental) {
        bool cut1418 = false; float diff1418 = 0;
        IntelligentAnalyzer a;
        const int N = (int)(4.0 * sr);
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
        {
            double t = i / sr;
            buf[i] = vossPink() * 0.30f
                   + (withFundamental ? 0.22f*std::sin(2*3.14159*110*t) : 0.0f)
                   + 0.022f*std::sin(2*3.14159*1418*t);
        }
        double ms = 0;
        for (float s : buf) ms += s * s;
        float g = (float)(0.2 / std::sqrt(ms / N + 1e-12));
        for (float& s : buf) s *= g;
        for (int i = 0; i < N; i += 1024)
            a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
        auto r = a.analyze();
        for (auto& iss : r.issues)
        {
            if (!iss.reason.toLowerCase().contains("narrow resonance")) continue;
            if (std::abs(iss.freq - 1418) / 1418 < 0.025)
            {
                cut1418 = true;
                diff1418 = iss.detectedLevelDB;
                printf("    subharm-check res %6.0fHz %+.1fdB (fundamental %s)\n",
                       iss.freq, iss.detectedLevelDB, withFundamental ? "ON" : "OFF");
            }
        }
        return std::make_pair(cut1418, diff1418);
    };
    auto solo = run1418(false);
    auto withHarm = run1418(true);
    CHECK(solo.first && solo.second > 8.0f && solo.second < 18.0f,
          "1418 alone flags in veto zone (diff %.1f)", solo.second);
    CHECK(!withHarm.first, "same 1418 over strong 110 fundamental vetoed");

    // Tier-2 isolation: 62 Hz sub-bass can NEVER be a candidate (<80 Hz rule),
    // so tier-1 cannot see it. Its 6th harmonic (372 Hz) in the veto zone must
    // still be vetoed via subharmonic prominence - while 372 alone flags.
    auto run372 = [&](bool withSub) {
        bool cut372 = false; float diff372 = 0;
        IntelligentAnalyzer a;
        const int N = (int)(4.0 * sr);
        std::vector<float> buf(N);
        for (int i = 0; i < N; ++i)
        {
            double t = i / sr;
            buf[i] = vossPink() * 0.30f
                   + (withSub ? 0.30f*std::sin(2*3.14159*62*t) : 0.0f)
                   + 0.032f*std::sin(2*3.14159*372*t);
        }
        double ms = 0;
        for (float s : buf) ms += s * s;
        float g = (float)(0.2 / std::sqrt(ms / N + 1e-12));
        for (float& s : buf) s *= g;
        for (int i = 0; i < N; i += 1024)
            a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
        auto r = a.analyze();
        for (auto& iss : r.issues)
        {
            if (!iss.reason.toLowerCase().contains("narrow resonance")) continue;
            if (std::abs(iss.freq - 372) / 372 < 0.03)
            {
                cut372 = true;
                diff372 = iss.detectedLevelDB;
                printf("    tier2-check res %6.0fHz %+.1fdB (sub %s)\n",
                       iss.freq, iss.detectedLevelDB, withSub ? "ON" : "OFF");
            }
        }
        return std::make_pair(cut372, diff372);
    };
    auto solo372 = run372(false);
    auto withSub = run372(true);
    CHECK(solo372.first && solo372.second > 8.0f && solo372.second < 18.0f,
          "372 alone flags in veto zone (diff %.1f)", solo372.second);
    CHECK(!withSub.first, "same 372 over strong 62 Hz sub vetoed (tier-2)");

    printf(fails == 0 ? "VETO TESTS PASSED\n" : "FAILURES: %d\n", fails);
    return fails;
}
