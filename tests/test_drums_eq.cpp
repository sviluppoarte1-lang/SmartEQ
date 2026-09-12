// Harness diagnostico: pilota il VERO SongAnalyzer + IntelligentAnalyzer
// con drums.wav (mono f32 raw, 44100 Hz) a 126bpm 4/4 e scarica la mappa.
#include "DSP/SongAnalyzer.h"
#include <cstdio>
#include <cmath>
#include <vector>

int main(int argc, char** argv)
{
    const double sr = 44100.0, bpm = 126.0;
    FILE* f = fopen("/tmp/opencode/drums_mono.f32", "rb");
    if (! f) { printf("no input\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t) ftell(f) / sizeof(float);
    fseek(f, 0, SEEK_SET);
    std::vector<float> audio(n);
    if (fread(audio.data(), sizeof(float), n, f) != n) { printf("short read\n"); return 1; }
    fclose(f);
    printf("samples=%zu dur=%.1fs\n", n, n / sr);

    SongAnalyzer sa;
    sa.prepare(sr, 4);
    sa.setLearning(true);
    const int bs = 512;
    for (size_t pos = 0; pos + bs <= n; pos += bs)
    {
        double t = (double) pos / sr;
        double ppq = t * bpm / 60.0;
        sa.pushAudioBlock(audio.data() + pos, bs, ppq, true, true, bpm, sr);
    }
    sa.setLearning(false);
    sa.flush();
    printf("bars=%d\n", sa.getNumBars());

    const char* out = argc > 1 ? argv[1] : "/tmp/opencode/eqmap.csv";
    FILE* o = fopen(out, "w");
    fprintf(o, "bar,rms");
    for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i) fprintf(o, ",g%d", i);
    fprintf(o, "\n");
    // rms per bar: riletta via getGainsForBar? rms non esposto -> ricalcola qui
    double barLen = 60.0 / bpm * 4.0;
    for (int b = 0; b < sa.getNumBars(); ++b)
    {
        float g[SongAnalyzer::MaxSongBands] = { 0 };
        sa.getGainsForBar(b, g);
        size_t s0 = (size_t) (b * barLen * sr), s1 = juce::jmin(n, (size_t) ((b + 1) * barLen * sr));
        double ss = 0; float pk = 0;
        for (size_t i = s0; i < s1; ++i) { ss += (double) audio[i] * audio[i]; pk = juce::jmax(pk, std::abs(audio[i])); }
        double rms = s1 > s0 ? 20 * log10(sqrt(ss / (s1 - s0)) + 1e-12) : -100;
        fprintf(o, "%d,%.1f", b, rms);
        for (int i = 0; i < SongAnalyzer::MaxSongBands; ++i) fprintf(o, ",%.2f", g[i]);
        fprintf(o, "\n");
    }
    fclose(o);
    printf("wrote %s\n", out);

    // Snapshot ANALYZE sulla sezione loud (150-154s) per confronto
    IntelligentAnalyzer ia;
    {
        size_t s0 = (size_t) (150 * sr);
        for (size_t pos = s0; pos < s0 + (size_t)(4 * sr); pos += 512)
            ia.pushAudioBlock(audio.data() + pos, 512, 1, sr);
    }
    auto res = ia.analyze();
    printf("--- ANALYZE 150-154s: score=%.0f tilt=%.1f crest=%.1f ---\n",
           res.overallScore, res.tiltDB, res.crestFactor);
    for (auto& iss : res.issues)
        printf("[%d] %.0fHz %+.1fdB Q%.1f %s\n", iss.bandIdx, iss.freq,
               iss.suggestedGainDB, iss.suggestedQ, iss.reason.toRawUTF8());
    printf("gains:");
    for (int i = 0; i < 16; ++i) printf(" %.2f", res.suggestedGains[i]);
    printf("\n");
    return 0;
}
