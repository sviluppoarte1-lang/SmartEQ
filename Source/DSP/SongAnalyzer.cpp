#include "SongAnalyzer.h"

SongAnalyzer::SongAnalyzer()
{
    bars.reserve(256);
    barSamples.reserve(96000);
    helper.setSampleRate(sampleRate);
}

void SongAnalyzer::prepare(double sr, int bpb)
{
    juce::ScopedLock sl(lock);
    if (sr >= 8000) sampleRate = sr;
    helper.setSampleRate(sampleRate);
    if (bpb >= 1 && bpb <= 12) beatsPerBar = bpb;
}

void SongAnalyzer::reset()
{
    juce::ScopedLock sl(lock);
    bars.clear();
    currentBar = -1;
    barSamples.clear();
    barSumSquares = 0.0;
    barPeak = 0.0f;
    barSampleCount = 0;
}

void SongAnalyzer::flush()
{
    juce::ScopedLock sl(lock);
    if (currentBar >= 0 && ! barSamples.empty())
    {
        finalizeCurrentBar();
        barSamples.clear();
        barSumSquares = 0.0;
        barPeak = 0.0f;
        barSampleCount = 0;
    }
}

void SongAnalyzer::setBeatsPerBar(int bpb)
{
    juce::ScopedLock sl(lock);
    if (bpb >= 1 && bpb <= 12) beatsPerBar = bpb;
}

int SongAnalyzer::barFromPpq(double ppq) const
{
    if (ppq < 0) return 0;
    int bpb = juce::jmax(1, beatsPerBar);
    return (int) std::floor(ppq / (double) bpb);
}

void SongAnalyzer::pushAudioBlock(const float* monoData, int numSamples,
                                  double ppqPosition, bool ppqValid, bool isPlaying,
                                  double bpm, double sr)
{
    if (monoData == nullptr || numSamples <= 0) return;
    if (! learning.load()) return;
    if (! isPlaying) return;

    juce::ScopedLock sl(lock);
    if (sr >= 8000 && std::abs(sr - sampleRate) > 1.0)
    {
        sampleRate = sr;
        helper.setSampleRate(sampleRate);
    }

    // Senza ppq valida ripiega sulla battuta corrente (o 0): impara comunque il timbro.
    int bar = currentBar;
    if (ppqValid && bpm > 0)
        bar = barFromPpq(ppqPosition);
    if (bar < 0) bar = 0;
    if (bar >= MaxBars) return; // mappa piena: ignora

    if (bar != currentBar)
    {
        if (currentBar >= 0)
            finalizeCurrentBar();
        currentBar = bar;
        barSamples.clear();
        barSumSquares = 0.0;
        barPeak = 0.0f;
        barSampleCount = 0;
    }

    // Accumula (cap ~8s per battuta per non esplodere di memoria)
    size_t room = (size_t) sampleRate * 8;
    size_t canTake = room > barSamples.size() ? room - barSamples.size() : 0;
    int take = (int) juce::jmin<size_t>((size_t) numSamples, canTake);
    for (int i = 0; i < take; ++i)
    {
        float s = monoData[i];
        if (! std::isfinite(s)) s = 0.0f;
        barSamples.push_back(s);
        barSumSquares += (double) s * (double) s;
        float a = std::abs(s);
        if (a > barPeak) barPeak = a;
    }
    barSampleCount += take;
}

void SongAnalyzer::computeBarSpectrum(float* outSpectrumDB) const
{
    const int N = (int) barSamples.size();
    double acc[2048] = { 0 };
    int frames = 0;

    for (int start = 0; start + fftSize <= N; start += fftSize)
    {
        std::memcpy(tmpFFT, barSamples.data() + start, sizeof(float) * (size_t) fftSize);
        window.multiplyWithWindowingTable(tmpFFT, (size_t) fftSize);
        std::memset(tmpFFT + fftSize, 0, sizeof(float) * (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform(tmpFFT);

        for (int b = 0; b < spectrumSize; ++b)
        {
            float mag = tmpFFT[b];
            float db = juce::Decibels::gainToDecibels(mag / (float) fftSize + 1e-9f);
            acc[b] += db;
        }
        ++frames;
    }

    if (frames == 0)
    {
        for (int b = 0; b < spectrumSize; ++b) outSpectrumDB[b] = -100.0f;
        return;
    }
    for (int b = 0; b < spectrumSize; ++b)
        outSpectrumDB[b] = (float) (acc[b] / (double) frames);
}

void SongAnalyzer::finalizeCurrentBar()
{
    // Richiede `lock` acquisito. Crea/aggiorna bars[currentBar].
    if (currentBar < 0 || currentBar >= MaxBars) return;

    static constexpr float kSilenceRmsDB = -60.0f;
    BarEntry entry;
    entry.bar = currentBar;

    double meanSq = barSampleCount > 0 ? barSumSquares / (double) barSampleCount : 0.0;
    float rmsDB = juce::Decibels::gainToDecibels((float) std::sqrt(meanSq) + 1e-9f);
    entry.rmsDB = rmsDB;

    if (rmsDB >= kSilenceRmsDB && (int) barSamples.size() >= fftSize)
    {
        entry.silent = false;
        float spectrum[spectrumSize];
        computeBarSpectrum(spectrum);
        // Crest della battuta: guida i tagli su materiale transiente
        float crestDB = juce::Decibels::gainToDecibels(barPeak / ((float) std::sqrt(meanSq) + 1e-9f));
        auto res = helper.analyzeSpectrumDB(spectrum, crestDB);
        int n = juce::jmin(EQProcessor::NumBands, MaxSongBands);
        for (int i = 0; i < n; ++i)
            entry.gains[i] = juce::jlimit(-4.0f, 4.0f, res.suggestedGains[i]); // budget ±4 dB/banda
        for (int i = n; i < MaxSongBands; ++i) entry.gains[i] = 0.0f;

        // Smoothing temporale: la fase dei colpi rispetto alla battuta fa
        // oscillare i tagli bar-by-bar (es. -3.2/-5.0 alternati) - amalgama
        // 70/30 con la precedente battuta musicale. Il glide del FOLLOW
        // gestisce comunque le transizioni vere di sezione.
        int prevMusical = -1;
        for (int b = currentBar - 1; b >= 0 && b < (int) bars.size(); --b)
            if (! bars[(size_t) b].silent) { prevMusical = b; break; }
        if (prevMusical >= 0)
            for (int i = 0; i < MaxSongBands; ++i)
                entry.gains[i] = 0.7f * entry.gains[i] + 0.3f * bars[(size_t) prevMusical].gains[i];
    }
    else
    {
        // Silenzio o frammento: non cuocere spazzatura dal rumore di fondo.
        // Resta silent a gain zero - i lookup la saltano.
        entry.silent = true;
        for (int i = 0; i < MaxSongBands; ++i) entry.gains[i] = 0.0f;
    }

    if ((int) bars.size() <= currentBar)
    {
        // Riempi eventuali gap (seek avanti) come silent
        while ((int) bars.size() < currentBar)
        {
            BarEntry gap;
            gap.bar = (int) bars.size();
            gap.silent = true;
            gap.rmsDB = -100.0f;
            bars.push_back(gap);
        }
        bars.push_back(entry);
    }
    else
    {
        // Re-learn della stessa battuta: media solo se entrambe musicali,
        // altrimenti vince la musicale (stabilita' senza avvelenare).
        BarEntry& prev = bars[(size_t) currentBar];
        if (! entry.silent && ! prev.silent)
        {
            for (int i = 0; i < MaxSongBands; ++i)
                prev.gains[i] = 0.5f * (prev.gains[i] + entry.gains[i]);
            prev.rmsDB = 0.5f * (prev.rmsDB + entry.rmsDB);
        }
        else if (! entry.silent)
        {
            prev = entry;
        }
        // else: resta la precedente (se era musicale) o silent
    }
}

int SongAnalyzer::nearestMusicalBar(int bar) const
{
    // Richiede `lock` acquisito.
    if (bars.empty()) return -1;
    int idx = juce::jlimit(0, (int) bars.size() - 1, bar);
    if (! bars[(size_t) idx].silent) return idx;
    for (int d = 1; d < (int) bars.size(); ++d)
    {
        if (idx - d >= 0 && ! bars[(size_t) (idx - d)].silent) return idx - d;
        if (idx + d < (int) bars.size() && ! bars[(size_t) (idx + d)].silent) return idx + d;
    }
    return -1;
}

bool SongAnalyzer::getGainsForBar(int bar, float* outGains) const
{
    if (outGains == nullptr) return false;
    juce::ScopedLock sl(lock);
    int idx = nearestMusicalBar(bar);
    if (idx < 0) return false;
    std::memcpy(outGains, bars[(size_t) idx].gains, sizeof(float) * (size_t) MaxSongBands);
    return true;
}

bool SongAnalyzer::getGainsForPpq(double ppqPosition, float* outGains) const
{
    if (outGains == nullptr) return false;
    juce::ScopedLock sl(lock);
    int idx = nearestMusicalBar(barFromPpq(ppqPosition));
    if (idx < 0) return false;
    std::memcpy(outGains, bars[(size_t) idx].gains, sizeof(float) * (size_t) MaxSongBands);
    return true;
}

int SongAnalyzer::getNumBars() const
{
    juce::ScopedLock sl(lock);
    return (int) bars.size();
}

bool SongAnalyzer::hasData() const
{
    juce::ScopedLock sl(lock);
    return ! bars.empty();
}

juce::String SongAnalyzer::getStatusText() const
{
    juce::ScopedLock sl(lock);
    if (bars.empty())
        return learning.load() ? "Song: learning... play the track" : "Song: no map - enable LEARN and play";
    int musical = 0;
    for (auto& b : bars) if (! b.silent) ++musical;
    juce::String s = "Song: " + juce::String((int) bars.size()) + " bars (" + juce::String(musical) + " musical)";
    if (currentBar >= 0) s += " | bar " + juce::String(currentBar + 1);
    if (learning.load()) s += " | learning";
    return s;
}

void SongAnalyzer::importStaticMap(const float* gainsDB, int numGains)
{
    if (gainsDB == nullptr || numGains <= 0) return;
    juce::ScopedLock sl(lock);
    int n = juce::jmin(numGains, MaxSongBands);
    if (bars.empty())
    {
        BarEntry e;
        e.bar = 0;
        e.silent = false;
        for (int i = 0; i < n; ++i) e.gains[i] = juce::jlimit(-9.0f, 9.0f, gainsDB[i]);
        bars.push_back(e);
        if (currentBar < 0) currentBar = 0;
    }
    else
    {
        for (auto& b : bars)
            for (int i = 0; i < n; ++i) b.gains[i] = juce::jlimit(-9.0f, 9.0f, gainsDB[i]);
    }
}

juce::ValueTree SongAnalyzer::toValueTree() const
{
    juce::ScopedLock sl(lock);
    juce::ValueTree v("SongMap");
    v.setProperty("beatsPerBar", beatsPerBar, nullptr);
    v.setProperty("numBars", (int) bars.size(), nullptr);

    juce::MemoryBlock mb;
    auto append = [&](const void* data, size_t sz) {
        mb.append(data, sz);
    };
    int nb = (int) bars.size();
    append(&nb, sizeof(nb));
    for (auto& b : bars)
    {
        append(&b.bar, sizeof(b.bar));
        append(b.gains, sizeof(b.gains));
        append(&b.rmsDB, sizeof(b.rmsDB));
        int silentFlag = b.silent ? 1 : 0;
        append(&silentFlag, sizeof(silentFlag));
    }
    v.setProperty("data", mb.toBase64Encoding(), nullptr);
    return v;
}

void SongAnalyzer::restoreFromValueTree(const juce::ValueTree& v)
{
    if (! v.isValid() || ! v.hasType("SongMap")) return;
    juce::ScopedLock sl(lock);
    bars.clear();

    int bpb = (int) v.getProperty("beatsPerBar", 4);
    if (bpb >= 1 && bpb <= 12) beatsPerBar = bpb;

    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding(v.getProperty("data", "").toString())) return;
    if (mb.getSize() < sizeof(int)) return;

    int pos = 0;
    auto read = [&](void* dst, size_t sz) -> bool {
        if (pos + (int) sz > (int) mb.getSize()) return false;
        std::memcpy(dst, (const char*) mb.getData() + pos, sz);
        pos += (int) sz;
        return true;
    };
    int nb = 0;
    if (! read(&nb, sizeof(nb))) return;
    nb = juce::jlimit(0, MaxBars, nb);
    // Compat: mappe salvate prima del flag silent (stride 72) vs nuove (76)
    int remaining = (int) mb.getSize() - pos;
    int strideNew = (int) (sizeof(int) + sizeof(float) * MaxSongBands + sizeof(float) + sizeof(int));
    bool hasSilentFlag = nb > 0 && remaining == nb * strideNew;
    for (int i = 0; i < nb; ++i)
    {
        BarEntry e;
        if (! read(&e.bar, sizeof(e.bar))) break;
        if (! read(e.gains, sizeof(e.gains))) break;
        if (! read(&e.rmsDB, sizeof(e.rmsDB))) break;
        if (hasSilentFlag)
        {
            int flag = 0;
            if (! read(&flag, sizeof(flag))) break;
            e.silent = flag != 0;
        }
        else
        {
            e.silent = false;
        }
        e.bar = juce::jlimit(0, MaxBars - 1, e.bar);
        for (int g = 0; g < MaxSongBands; ++g)
            // Budget corrente ±4 dB/banda: le mappe salvate col vecchio stacking
            // (±9 dB sui piatti) vengono sanate al caricamento, non riesumate.
            e.gains[g] = juce::jlimit(-4.0f, 4.0f, e.gains[g]);
        bars.push_back(e);
    }
    currentBar = bars.empty() ? -1 : 0;
    barSamples.clear();
    barSampleCount = 0;
    barSumSquares = 0.0;
    barPeak = 0.0f;
}
