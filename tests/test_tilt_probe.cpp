// Probe: measure spectral tilt of a real mastered file in 4 sections.
// Usage: test_tilt_probe "/path/to/file.wav"
#include "DSP/IntelligentAnalyzer.h"
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: test_tilt_probe file.wav\n"); return 2; }
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> reader(
        fmt.createReaderFor(new juce::FileInputStream(juce::File(argv[1])), true));
    if (reader == nullptr) { printf("cannot open\n"); return 1; }
    double sr = reader->sampleRate;
    long long total = reader->lengthInSamples;
    printf("sr=%.0f len=%.1fs ch=%d\n", sr, total / sr, (int)reader->numChannels);

    for (int sec = 0; sec < 4; ++sec)
    {
        long long start = total * sec / 4;
        int want = (int)std::min<long long>(60 * (long long)sr, total - start);
        juce::AudioBuffer<float> buf((int)reader->numChannels, want);
        reader->read(&buf, 0, want, (int)start, true, true);
        IntelligentAnalyzer a;
        const int chs = buf.getNumChannels();
        for (int i = 0; i < want; i += 1024)
        {
            int n = std::min(1024, want - i);
            // mix to mono for the analyzer push
            static thread_local std::vector<float> mono(1024);
            for (int k = 0; k < n; ++k)
            {
                float m = buf.getSample(0, i + k);
                for (int c = 1; c < chs; ++c) m += buf.getSample(c, i + k);
                mono[k] = m / chs;
            }
            a.pushAudioBlock(mono.data(), n, 1, sr);
        }
        auto r = a.analyze();
        printf("[sec %d t=%.0f-%.0fs] tilt=%+.1fdB rms=%.1f issues=%d\n", sec,
               start / sr, (start + want) / sr, r.tiltDB, r.rmsDB, r.issues.size());
        for (int i = 0; i < std::min(5, r.issues.size()); ++i)
            printf("    - %s\n", r.issues[i].reason.toRawUTF8());
    }
    return 0;
}
