// End-to-end: vero SmartEQ su drums.wav con stub transport a 126bpm.
// Fase 1: LEARN su tutto il brano. Fase 2: FOLLOW. Scarica l'output.
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <vector>

struct StubPlayHead : juce::AudioPlayHead
{
    juce::AudioPlayHead::PositionInfo pos;
    double sampleRate = 44100.0, bpm = 126.0;
    juce::int64 samples = 0;
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return pos; }
    void reset()
    {
        samples = 0;
        pos.setIsPlaying(true);
        pos.setBpm(bpm);
        pos.setPpqPosition(0.0);
        pos.setTimeInSeconds(0.0);
    }
    void advance(int n)
    {
        samples += n;
        double t = samples / sampleRate;
        pos.setTimeInSeconds(t);
        pos.setPpqPosition(t * bpm / 60.0);
    }
};

static void setParam(SmartEQAudioProcessor& p, const char* id, float v)
{
    if (auto* par = p.apvts.getParameter(id))
        par->setValueNotifyingHost(par->convertTo0to1(v));
}

int main()
{
    FILE* f = fopen("/tmp/opencode/drums_mono.f32", "rb");
    fseek(f, 0, SEEK_END);
    size_t n = (size_t) ftell(f) / sizeof(float);
    fseek(f, 0, SEEK_SET);
    std::vector<float> audio(n);
    [[maybe_unused]] size_t nr = fread(audio.data(), sizeof(float), n, f);
    fclose(f);
    printf("samples=%zu\n", n);

    SmartEQAudioProcessor proc;
    proc.prepareToPlay(44100, 512);
    StubPlayHead ph;
    proc.setPlayHead(&ph);
    juce::MidiBuffer midi;

    auto runPass = [&](bool learn, const char* tag, std::vector<float>& out) {
        ph.reset();
        setParam(proc, "songLearn", learn ? 1.0f : 0.0f);
        setParam(proc, "songFollow", learn ? 0.0f : 1.0f);
        setParam(proc, "songGlide", 400.0f);
        out.assign(n, 0);
        juce::AudioBuffer<float> buf(2, 512);
        for (size_t pos = 0; pos + 512 <= n; pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample(ch, i, audio[pos + i]);
            proc.processBlock(buf, midi);
            for (int i = 0; i < 512; ++i) out[pos + i] = buf.getSample(0, i);
            ph.advance(512);
        }
        printf("%s: map=%s\n", tag, proc.getSongAnalyzer().getStatusText().toRawUTF8());
    };

    std::vector<float> learned, followed;
    runPass(true, "LEARN", learned);
    runPass(false, "FOLLOW", followed);

    FILE* o = fopen("/tmp/opencode/chain_eq.f32", "wb");
    fwrite(followed.data(), sizeof(float), n, o);
    fclose(o);

    auto sec = [&](const std::vector<float>& v, int s0, int s1) {
        double ss = 0; float pk = 0;
        size_t a = (size_t) s0 * 44100, b = juce::jmin(n, (size_t) s1 * 44100);
        for (size_t i = a; i < b; ++i) { ss += (double) v[i] * v[i]; pk = juce::jmax(pk, std::abs(v[i])); }
        double rms = 20 * log10(sqrt(ss / (b - a)) + 1e-12);
        return std::make_pair(rms, 20 * log10(pk + 1e-12));
    };
    printf("sez in->out (rms/peak dB):\n");
    for (auto [a, b] : { std::make_pair(20, 35), std::make_pair(150, 165), std::make_pair(240, 255) })
    {
        auto [ri, pi] = sec(audio, a, b);
        auto [ro, po] = sec(followed, a, b);
        printf("  %d-%ds: rms %+.1f -> %+.1f (d=%+.1f) peak %+.1f -> %+.1f (d=%+.1f)\n",
               a, b, ri, ro, ro - ri, pi, po, po - pi);
    }
    return 0;
}
