// SmartEQ spectral-tilt verification: the analyzer must brighten dark
// material, warm thin material, and leave balanced material alone.
// Links against the real plugin static lib (uses the true DSP + thresholds).
#include "DSP/IntelligentAnalyzer.h"
#include <cstdio>
#include <cmath>
#include <functional>

static int fails = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double sr = 48000.0;
static unsigned rngState = 999;
static float white() { rngState = rngState * 1664525u + 1013904223u; return ((rngState >> 16) / 32768.0f - 1.0f); }

struct Res { float tilt; int nIssues; bool hasDark; bool hasBright; float lowGain; float highGain; };

static Res runCase(const char* tag, std::function<float(double)> fn)
{
    IntelligentAnalyzer a;
    const int N = (int)(4.0 * sr);
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = fn(i / sr);
    for (int i = 0; i < N; i += 1024)
        a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
    auto r = a.analyze();
    Res res { r.tiltDB, r.issues.size(), false, false, 0, 0 };
    for (auto& iss : r.issues)
    {
        auto rs = iss.reason.toLowerCase();
        if (rs.contains("dark") || rs.contains("brightening")) res.hasDark = true;
        if (rs.contains("bright/thin") || rs.contains("warming") || rs.contains("softening")) res.hasBright = true;
    }
    // representative low/high band gains (16-band Full layout indices)
    res.lowGain = r.suggestedGains[5];   // 250 Hz
    res.highGain = r.suggestedGains[13]; // 10 kHz
    printf("[%-9s] tilt=%+.1fdB issues=%d dark=%d bright=%d g250=%+.2f g10k=%+.2f\n",
           tag, res.tilt, res.nIssues, (int)res.hasDark, (int)res.hasBright, res.lowGain, res.highGain);
    for (int i = 0; i < std::min(4, r.issues.size()); ++i)
        printf("    - %s\n", r.issues[i].reason.toRawUTF8());
    return res;
}

static float vossPink()
{
    // Voss-McCartney pink noise: dense spectrum, equal energy per octave
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

static std::vector<float> makeSignal(std::function<float(double)> fn, double seconds = 4.0)
{
    int N = (int)(seconds * sr);
    std::vector<float> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = fn(i / sr);
    // RMS-normalize: tilt must measure BALANCE, never level
    double ms = 0;
    for (float s : buf) ms += s * s;
    float g = (float)(0.2 / std::sqrt(ms / N + 1e-12));
    for (float& s : buf) s *= g;
    return buf;
}

int main()
{
    // Dark: brown-ish (-6dB/oct) + master-like low weight
    auto darkBuf = makeSignal([](double t) {
        static float brown = 0;
        float w = white();
        brown = 0.985f * brown + 0.15f * w;
        return brown * 2.2f + 0.25f*std::sin(2*3.14159*65*t);
    });
    // Bright/thin: highpassed wash + forward highs, starved lows
    auto brightBuf = makeSignal([](double t) {
        static float lp1 = 0, lp2 = 0;
        float w = white();
        lp1 += 0.23f * (w - lp1); lp2 += 0.23f * (w - lp2);
        float hp = w - (lp1 + lp2) * 0.5f;
        return hp * 1.4f + 0.20f*std::sin(2*3.14159*3200*t) + 0.15f*std::sin(2*3.14159*6800*t);
    });
    // Balanced: pink reference (equal energy per octave)
    auto balancedBuf = makeSignal([](double) { return vossPink(); });

    auto runBuf = [](const char* tag, const std::vector<float>& buf) {
        IntelligentAnalyzer a;
        const int N = (int)buf.size();
        for (int i = 0; i < N; i += 1024)
            a.pushAudioBlock(buf.data() + i, std::min(1024, N - i), 1, sr);
        auto r = a.analyze();
        Res res { r.tiltDB, r.issues.size(), false, false, 0, 0 };
        for (auto& iss : r.issues)
        {
            auto rs = iss.reason.toLowerCase();
            if (rs.contains("dark") || rs.contains("brightening")) res.hasDark = true;
            if (rs.contains("bright/thin") || rs.contains("warming") || rs.contains("softening")) res.hasBright = true;
        }
        res.lowGain = r.suggestedGains[5];
        res.highGain = r.suggestedGains[13];
        printf("[%-9s] tilt=%+.1fdB issues=%d dark=%d bright=%d g250=%+.2f g10k=%+.2f\n",
               tag, res.tilt, res.nIssues, (int)res.hasDark, (int)res.hasBright, res.lowGain, res.highGain);
        for (int i = 0; i < std::min(4, r.issues.size()); ++i)
            printf("    - %s\n", r.issues[i].reason.toRawUTF8());
        return res;
    };

    auto dark = runBuf("dark", darkBuf);
    auto bright = runBuf("bright", brightBuf);
    auto balanced = runBuf("balanced", balancedBuf);

    CHECK(dark.tilt > 20.0f, "dark master reads tilt > 20 (%.1f)", dark.tilt);
    CHECK(dark.hasDark && !dark.hasBright, "dark master gets brightening issues");
    CHECK(dark.lowGain < -0.3f && dark.highGain > 0.3f, "dark correction direction (low %.2f high %.2f)",
          dark.lowGain, dark.highGain);

    CHECK(bright.tilt < 4.0f, "thin master reads tilt < 4 (%.1f)", bright.tilt);
    CHECK(bright.hasBright && !bright.hasDark, "thin master gets warming issues");
    CHECK(bright.lowGain > 0.3f && bright.highGain <= -0.3f, "bright correction direction (low %.2f high %.2f)",
          bright.lowGain, bright.highGain);

    CHECK(!balanced.hasDark && !balanced.hasBright, "balanced material untouched (tilt %.1f)",
          balanced.tilt);

    // Correction stays gentle: tilt issues never propose more than +-1.6 dB
    // (band totals may be larger - other detectors stack on the same bands)
    {
        IntelligentAnalyzer a;
        const int N = (int)(4.0 * sr);
        for (int i = 0; i < N; i += 1024)
        {
            float blk[1024];
            for (int k = 0; k < 1024; ++k)
            {
                double t = (i + k) / sr;
                blk[k] = 0.6f*std::sin(2*3.14159*70*t) + 0.4f*std::sin(2*3.14159*140*t);
            }
            a.pushAudioBlock(blk, 1024, 1, sr);
        }
        auto r = a.analyze();
        bool gentle = true;
        for (auto& iss : r.issues)
        {
            auto rs = iss.reason.toLowerCase();
            if (rs.contains("dark") || rs.contains("brightening") || rs.contains("warming") || rs.contains("softening"))
                if (std::abs(iss.suggestedGainDB) > 1.6f) gentle = false;
        }
        CHECK(gentle, "extreme sub material: tilt correction capped gentle");
        // ...and every band total stays inside the +-9 dB safety clamp
        bool clamped = true;
        for (int i = 0; i < 16; ++i) if (std::abs(r.suggestedGains[i]) > 9.0f) clamped = false;
        CHECK(clamped, "all band totals inside +-9 dB clamp");
    }

    printf(fails == 0 ? "TILT TESTS PASSED\n" : "FAILURES: %d\n", fails);
    return fails;
}
