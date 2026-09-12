#include "IntelligentAnalyzer.h"
#include <vector>
#include <algorithm>

IntelligentAnalyzer::IntelligentAnalyzer()
{
    circBuffer.setSize(1, maxAccumulated);
    circBuffer.clear();
}

void IntelligentAnalyzer::reset()
{
    const juce::ScopedLock sl(lock);
    circBuffer.clear();
    writePos = 0;
    accumulatedSamples = 0;
    crestForGuard = -1.0f;
    for (int i=0;i<spectrumSize;++i) { averagedSpectrumDB[i] = -100; peakSpectrumDB[i] = -100; }
}

void IntelligentAnalyzer::pushAudioBlock(const float* data, int numSamples, int numChannels, double sr)
{
    // Mix to mono semplificato, lock-free con tryEnter
    if (!lock.tryEnter()) return;
    sampleRate = sr;
    // Scrivi in circolare
    for (int i=0;i<numSamples;++i)
    {
        float mono = 0;
        if (numChannels == 1) mono = data[i];
        else
        {
            // data è interleaved? no, pointer singolo canale passato come mono già?
            // Per semplicità assumiamo mono avg se stereo: usiamo primo canale
            mono = data[i];
        }
        circBuffer.setSample(0, writePos, mono);
        writePos = (writePos + 1) % maxAccumulated;
        if (accumulatedSamples < maxAccumulated) accumulatedSamples++;
    }
    lock.exit();
}

float IntelligentAnalyzer::getProgress() const
{
    return juce::jlimit(0.f, 1.f, (float)accumulatedSamples / (float)(sampleRate * analysisDurationSec));
}

double IntelligentAnalyzer::getBandFreq(int idx) const
{
    static constexpr double freqs[NumBands] = {
#ifdef SMARTEQ_FREE_VERSION
        63,160,400,1000,2500,4000,6300,10000
#else
        25,40,63,100,160,250,400,630,1000,1600,2500,4000,6300,10000,16000,20000
#endif
    };
    return freqs[juce::jlimit(0, NumBands-1, idx)];
}

void IntelligentAnalyzer::computeSpectrum(const float* monoData, int numSamples)
{
    // Media spettrale con overlapping 75%
    const int hop = fftSize / 4;
    int numFrames = 0;
    for (int i=0;i<spectrumSize;++i) { averagedSpectrumDB[i] = -120; peakSpectrumDB[i] = -120; }
    float acc[spectrumSize] = {0};

    int frames = 0;
    for (int start=0; start + fftSize < numSamples; start += hop)
    {
        for (int i=0;i<fftSize;++i) tmpFFT[i] = monoData[start + i];
        window.multiplyWithWindowingTable(tmpFFT, fftSize);
        // FFT
        fft.performFrequencyOnlyForwardTransform(tmpFFT);
        // tmpFFT ora contiene magnitudine 0..spectrumSize-1
        for (int b=0;b<spectrumSize;++b)
        {
            float mag = tmpFFT[b];
            float db = juce::Decibels::gainToDecibels(mag + 1e-9f) + 60; // offset per visual
            // compensa finestra Hann
            acc[b] += db;
            peakSpectrumDB[b] = juce::jmax(peakSpectrumDB[b], db);
        }
        frames++;
    }
    if (frames > 0)
        for (int b=0;b<spectrumSize;++b) averagedSpectrumDB[b] = acc[b] / frames;
    else
        for (int b=0;b<spectrumSize;++b) averagedSpectrumDB[b] = -100;
}

float IntelligentAnalyzer::freqToBin(double freq) const { return float(freq * fftSize / sampleRate); }
double IntelligentAnalyzer::binToFreq(int bin) const { return bin * sampleRate / fftSize; }

float IntelligentAnalyzer::getBandEnergy(double centerFreq, double bandwidthOct) const
{
    double fLow = centerFreq * std::pow(2.0, -bandwidthOct/2);
    double fHigh = centerFreq * std::pow(2.0, bandwidthOct/2);
    int binLow = juce::jlimit(0, spectrumSize-1, (int)freqToBin(fLow));
    int binHigh = juce::jlimit(0, spectrumSize-1, (int)freqToBin(fHigh));
    float sum=0; int cnt=0;
    for (int b=binLow;b<=binHigh;++b) { sum += averagedSpectrumDB[b]; cnt++; }
    return cnt>0? sum/cnt : -100;
}

IntelligentAnalyzer::AnalysisResult IntelligentAnalyzer::analyze()
{
    AnalysisResult result;
    for (int i=0;i<NumBands;++i) result.suggestedGains[i]=0;

    juce::ScopedLock sl(lock);
    if (accumulatedSamples < minSamplesForAnalysis)
    {
        result.summary = "Not enough audio: need at least 2 seconds of audio. Play something and try again.";
        return result;
    }

    // Estrai buffer lineare consecutivo
    int N = juce::jlimit(0, maxAccumulated, (int)(sampleRate * analysisDurationSec));
    N = juce::jmin(N, accumulatedSamples);
    // allinea hop
    N = (N / 1024) * 1024;
    juce::AudioBuffer<float> linear(1, N);
    int startPos = (writePos - N + maxAccumulated) % maxAccumulated;
    for (int i=0;i<N;++i)
    {
        int idx = (startPos + i) % maxAccumulated;
        linear.setSample(0,i, circBuffer.getSample(0, idx));
    }

    // Calcolo RMS e Crest
    float rms = linear.getRMSLevel(0,0,N);
    result.rmsDB = juce::Decibels::gainToDecibels(rms + 1e-9f);
    float peak = linear.getMagnitude(0,0,N);
    result.crestFactor = juce::Decibels::gainToDecibels(peak/(rms+1e-9f));

    computeSpectrum(linear.getReadPointer(0), N);

    // Core spettrale condiviso con song-map (lock `sl` gia' acquisito qui)
    crestForGuard = juce::Decibels::gainToDecibels(peak / (rms + 1e-9f));
    result = analyzeCore();
    result.rmsDB = juce::Decibels::gainToDecibels(rms + 1e-9f);
    result.crestFactor = juce::Decibels::gainToDecibels(peak/(rms+1e-9f));

    return result;
}

IntelligentAnalyzer::AnalysisResult IntelligentAnalyzer::analyzeSpectrumDB(const float* spectrumDB, float crestDB)
{
    juce::ScopedLock sl(lock);
    if (spectrumDB != averagedSpectrumDB)
        std::memcpy(averagedSpectrumDB, spectrumDB, sizeof(float) * spectrumSize);
    for (int i = 0; i < spectrumSize; ++i) peakSpectrumDB[i] = averagedSpectrumDB[i];
    crestForGuard = crestDB;
    return analyzeCore();
}

float IntelligentAnalyzer::transientCutScale() const
{
    // Tagli stretti (risonanze, mud, harsh, sibilanti, nasalita') suonano bene
    // su mix densi ma scavano i colpi su materiale transiente: lo spettro medio
    // di una batteria e' dominato dal wash, i transienti no. Scala continua:
    // crest <= 14 dB -> correzione piena; crest >= 22 dB -> 35%.
    if (crestForGuard < 14.0f) return 1.0f;
    if (crestForGuard >= 22.0f) return 0.35f;
    return 1.0f - (crestForGuard - 14.0f) * (0.65f / 8.0f);
}

IntelligentAnalyzer::AnalysisResult IntelligentAnalyzer::analyzeCore()
{
    // Spectral core shared by snapshot ANALYZE and full-song mapping.
    // Caller must hold `lock` and have averagedSpectrumDB (+ sampleRate) ready.
    // RMS/crest stay unset here (time-domain, set by analyze()).
    AnalysisResult result;
    for (int i=0;i<NumBands;++i) result.suggestedGains[i]=0;

    // Bilancio spettrale: confronto energia low vs high
    float lowEnergy = getBandEnergy(150, 1.5);   // 60-300
    float midEnergy = getBandEnergy(1000, 1.0);  // 700-1400
    float highEnergy = getBandEnergy(8000, 1.2); // 4k-12k
    result.spectralBalance = (highEnergy - lowEnergy) * 0.05f; // normalizza
    result.spectralBalance = juce::jlimit(-1.f, 1.f, result.spectralBalance);
    (void)midEnergy;

    // Esegui detectors (tilt globale per primo: inquadra il resto)
    detectSpectralTilt(result);
    detectResonances(result);
    detectMuddiness(result);
    detectHarshness(result);
    detectSibilance(result);
    detectLowDeficiency(result);
    detectHarshLowMids(result);

    mapIssuesToBands(result);

    // Score: parti da 100, sottrai per ogni issue pesata
    float score = 100;
    for (auto &iss : result.issues)
    {
        float penalty = 0;
        if (iss.severity == AnalysisResult::BandIssue::Critical) penalty = 12;
        else if (iss.severity == AnalysisResult::BandIssue::Warning) penalty = 7;
        else penalty = 3;
        penalty *= (std::abs(iss.suggestedGainDB) / 3.0f);
        score -= penalty;
    }
    result.overallScore = juce::jlimit(0.f, 100.f, score);

    // Summary
    if (result.issues.isEmpty())
        result.summary = "Balanced spectrum! No issues detected. Score: " + juce::String((int)result.overallScore) + "/100";
    else
    {
        juce::String s = "Found " + juce::String(result.issues.size()) + " issues. Score: " + juce::String((int)result.overallScore) + "/100. ";
        // top 3
        for (int i=0;i<juce::jmin(3, result.issues.size()); ++i)
            s += "\n- " + result.issues[i].reason;
        result.summary = s;
    }

    // Clamp gains
    for (int i=0;i<NumBands;++i) result.suggestedGains[i] = juce::jlimit(-9.f, 9.f, result.suggestedGains[i]);

    return result;
}

// --- Detectors ---

int IntelligentAnalyzer::nearestBandIdx(double freq) const
{
    int best = 0;
    double bestDist = 1e9;
    for (int b = 0; b < NumBands; ++b)
    {
        double d = std::abs(std::log2(getBandFreq(b) / freq));
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

void IntelligentAnalyzer::detectSpectralTilt(AnalysisResult& r)
{
    // Global dark/bright balance: if the whole spectrum leans dark
    // (like a muddy master) we gently brighten it, and vice versa.
    // Compares low-mid energy (~200 Hz) against brilliance (3.5-7.5 kHz)
    // through the same dB-averaged bands as every other detector.
    // Thresholds calibrated empirically (see tests/test_analyzer_tilt.cpp
    // and probe runs on a real mastered track):
    //   pink reference ....... tilt ~+15.6 (untouched)
    //   brown/dark synth ..... tilt ~+26.5 (corrected)
    //   real dark master ..... tilt ~+35   (corrected)
    //   thin/bright synth .... tilt ~-19.4 (corrected mirrored)
    // Only clear leans are touched; correction = 30% of excess, capped small.
    const float lowAvg = getBandEnergy(200, 1.2);
    const float highAvg = 0.5f * (getBandEnergy(3500, 0.8f) + getBandEnergy(7500, 0.8f));
    const float tilt = lowAvg - highAvg;
    r.tiltDB = tilt;

    constexpr float kTiltDark = 20.0f;   // above: overall dark
    constexpr float kTiltBright = 4.0f;  // below: overall bright/thin
    constexpr float kMaxCorr = 2.5f;     // cap per correction (gentle)

    const bool dark = tilt > kTiltDark;
    const bool bright = tilt < kTiltBright;
    if (!dark && !bright) return;

    const float excess = dark ? (tilt - kTiltDark) : (kTiltBright - tilt);
    const float c = juce::jlimit(0.5f, kMaxCorr, excess * 0.3f);

    if (dark)
    {
        // Ease off the lows broadly, lift the brilliance
        r.suggestedGains[nearestBandIdx(90)] -= c * 0.3f;
        r.suggestedGains[nearestBandIdx(160)] -= c * 0.5f;
        r.suggestedGains[nearestBandIdx(250)] -= c * 0.4f;
        r.suggestedGains[nearestBandIdx(6500)] += c * 0.5f;
        r.suggestedGains[nearestBandIdx(10000)] += c * 0.6f;
        r.suggestedGains[nearestBandIdx(16000)] += c * 0.3f;

        typename AnalysisResult::BandIssue cut;
        cut.freq = 160; cut.bandIdx = nearestBandIdx(160);
        cut.detectedLevelDB = tilt;
        cut.suggestedGainDB = -c * 0.5f;
        cut.suggestedQ = 0.7f;
        cut.isExcess = true;
        cut.severity = AnalysisResult::BandIssue::Info;
        cut.reason = "Overall dark balance (lows +" + juce::String(tilt, 1)
                   + " dB over highs) - gentle low-mid ease-off";
        r.issues.add(cut);

        typename AnalysisResult::BandIssue lift;
        lift.freq = 8000; lift.bandIdx = nearestBandIdx(8000);
        lift.detectedLevelDB = -tilt;
        lift.suggestedGainDB = c * 0.6f;
        lift.suggestedQ = 0.7f;
        lift.isExcess = false;
        lift.severity = AnalysisResult::BandIssue::Info;
        lift.reason = "Brilliance lacking (highs " + juce::String(tilt, 1)
                    + " dB under lows) - gentle brightening";
        r.issues.add(lift);
    }
    else
    {
        // Mirror image: warm the lows back, soften the top
        r.suggestedGains[nearestBandIdx(90)] += c * 0.3f;
        r.suggestedGains[nearestBandIdx(160)] += c * 0.5f;
        r.suggestedGains[nearestBandIdx(250)] += c * 0.4f;
        r.suggestedGains[nearestBandIdx(6500)] -= c * 0.5f;
        r.suggestedGains[nearestBandIdx(10000)] -= c * 0.6f;
        r.suggestedGains[nearestBandIdx(16000)] -= c * 0.3f;

        typename AnalysisResult::BandIssue warm;
        warm.freq = 160; warm.bandIdx = nearestBandIdx(160);
        warm.detectedLevelDB = -tilt;
        warm.suggestedGainDB = c * 0.5f;
        warm.suggestedQ = 0.7f;
        warm.isExcess = false;
        warm.severity = AnalysisResult::BandIssue::Info;
        warm.reason = "Overall bright/thin balance (highs +" + juce::String(-tilt, 1)
                    + " dB over lows) - gentle warming";
        r.issues.add(warm);

        typename AnalysisResult::BandIssue soften;
        soften.freq = 8000; soften.bandIdx = nearestBandIdx(8000);
        soften.detectedLevelDB = tilt;
        soften.suggestedGainDB = -c * 0.6f;
        soften.suggestedQ = 0.7f;
        soften.isExcess = true;
        soften.severity = AnalysisResult::BandIssue::Info;
        soften.reason = "Top end forward (highs +" + juce::String(-tilt, 1)
                      + " dB over lows) - gentle softening";
        r.issues.add(soften);
    }
}

void IntelligentAnalyzer::detectResonances(AnalysisResult& r)
{
    // Risonanze strette: picco > mediana locale + threshold.
    // Veto armonico: i picchi che appartengono a una famiglia armonica
    // (>= 1 parente a rapporto intero) sono quasi sempre parziali musicali
    // (corde, ottoni, fondamentali di basso), non difetti. Per loro l'asticella
    // sale a +14 dB; solo un urlo (>+18 dB) viene segnalato comunque.
    // I picchi solitari (modi stanza, ring microfonici, asprezze metalliche)
    // restano alla soglia normale: errare dal lato del "non tagliare la musica"
    // e' voluto, perche' l'auto-fix applica i tagli da solo.
    struct Cand { double freq; float diff; int bin; };
    std::vector<Cand> cands;
    for (int bin=5; bin<spectrumSize-5; ++bin)
    {
        float center = averagedSpectrumDB[bin];
        // mediana locale 11 bins
        float neigh[11];
        for (int k=-5;k<=5;++k) neigh[k+5]=averagedSpectrumDB[bin+k];
        std::sort(neigh, neigh+11);
        float median = neigh[5];
        float diff = center - median;
        if (diff > 8.0f) // risonanza forte
        {
            double freq = binToFreq(bin);
            if (freq < 80 || freq > 18000) continue;
            // Evita duplicati dello stesso picco: il lobo principale Hann
            // e' largo 8 bin, quindi entro +-4 bin e' lo stesso picco fisico.
            // (Un dedupe in % fonderebbe invece picchi distinti, es. 1992 e 2090 Hz.)
            bool close=false;
            for (auto &c : cands) if (std::abs(c.bin - bin) <= 4) { close=true; break; }
            if (close) continue;
            cands.push_back({ freq, diff, bin });
            if ((int)cands.size() >= 24) break;
        }
    }

    constexpr float kHarmonicBarDB = 14.0f; // famiglie armoniche: solo se marcate
    constexpr float kAlwaysFlagDB = 18.0f;  // un urlo e' un problema, punto
    const double binHz = sampleRate / (double)fftSize;
    auto harmonicallyRelated = [&](double a, double b) -> bool
    {
        // b e' un multiplo intero di a? (entrambe le direzioni testate dal chiamante)
        if (a < 20.0 || b < 20.0 || b <= a) return false;
        const double ratio = b / a;
        const int m = (int)std::round(ratio);
        if (m < 2 || m > 16) return false;
        // Tolleranza: errore di quantizzazione bin (generoso in basso) con tetto
        // assoluto, cosi' in alto non si raggruppa a caso (es. 2713 vs 3x880).
        const double tol = juce::jmin(juce::jmax(0.04 * b, 1.5 * binHz), 3.0 * binHz);
        return std::abs(b - m * a) <= tol;
    };
    // Prominenza tonale a un bin: picco netto cercato per il veto subarmonico
    auto peakProminence = [&](int bin) -> float
    {
        if (bin < 6 || bin > spectrumSize - 7) return -100.0f;
        float pk = averagedSpectrumDB[bin];
        for (int k = -1; k <= 1; ++k) pk = juce::jmax(pk, averagedSpectrumDB[bin + k]);
        float neigh[11];
        for (int k = -5; k <= 5; ++k) neigh[k + 5] = averagedSpectrumDB[bin + k];
        std::sort(neigh, neigh + 11);
        return pk - neigh[5];
    };

    struct Commit { double freq; float diff; };
    std::vector<Commit> commits;
    for (size_t i = 0; i < cands.size(); ++i)
    {
        int relatives = 0;
        for (size_t j = 0; j < cands.size(); ++j)
        {
            if (i == j) continue;
            if (harmonicallyRelated(cands[i].freq, cands[j].freq)
             || harmonicallyRelated(cands[j].freq, cands[i].freq)) { ++relatives; break; }
        }
        // Livello 2: cerca un TONO FORTE al sottoarmonico (m = 2..6): se il picco
        // e' l'armonica di una fondamentale reale (es. parziale 13-esimo debole
        // con fondamentale forte non rilevata), e' musica. Solo m piccoli: oltre,
        // il reticolo e' cosi' fitto che ogni coincidenza sarebbe casuale.
        bool subharmonicTone = false;
        if (relatives == 0)
        {
            for (int m = 2; m <= 6; ++m)
            {
                const double f0 = cands[i].freq / m;
                if (f0 < 60.0) continue;
                const int b0 = (int)std::round(f0 * fftSize / sampleRate);
                if (peakProminence(b0) > 10.0f) { subharmonicTone = true; break; }
            }
        }
        const float diff = cands[i].diff;
        const bool harmonicFamily = (relatives > 0) || subharmonicTone;
        if (diff >= kAlwaysFlagDB) { commits.push_back({ cands[i].freq, diff }); }
        else if (harmonicFamily) { if (diff > kHarmonicBarDB) commits.push_back({ cands[i].freq, diff }); }
        else commits.push_back({ cands[i].freq, diff });
    }

    // I piu' forti prima (il summary mostra i primi 3), max 8 fix
    std::sort(commits.begin(), commits.end(),
              [](const Commit& a, const Commit& b) { return a.diff > b.diff; });
    const float cutScale = transientCutScale();
    int committed = 0;
    for (auto &cm : commits)
    {
        if (committed >= 8) break;
        typename AnalysisResult::BandIssue iss;
        iss.freq = cm.freq;
        iss.detectedLevelDB = cm.diff;
        iss.suggestedGainDB = -juce::jlimit(1.5f, 6.f, cm.diff * 0.6f) * cutScale;
        iss.suggestedQ = juce::jlimit(2.f, 8.f, 4.f + cm.diff*0.2f);
        iss.isExcess = true;
        iss.severity = cm.diff > 12 ? AnalysisResult::BandIssue::Critical : AnalysisResult::BandIssue::Warning;
        iss.reason = "Narrow resonance at " + juce::String((int)cm.freq) + " Hz (+" + juce::String(cm.diff,1) + " dB) - cut with narrow Q";
        iss.bandIdx = nearestBandIdx(cm.freq);
        r.issues.add(iss);
        r.suggestedGains[iss.bandIdx] += iss.suggestedGainDB * 0.7f; // attenua sovrapposizione
        ++committed;
    }
}

void IntelligentAnalyzer::detectMuddiness(AnalysisResult& r)
{
    // Fangosità 180-350 Hz accumulo
    float mud = getBandEnergy(250, 0.8); // ~190-330
    float lowMidRef = getBandEnergy(600, 0.7);
    float diff = mud - lowMidRef;
    r.muddiness = juce::jlimit(0.f,1.f, (diff - 3) * 0.15f);
    if (diff > 4.5f)
    {
        typename AnalysisResult::BandIssue iss;
        iss.freq = 250;
        iss.bandIdx = nearestBandIdx(250);
        iss.detectedLevelDB = diff;
        // Su kit isolati il "mud" e' corpo di rullante/tom, non accumulo che
        // maschera altro: con crest alto riduciamo (transientCutScale).
        iss.suggestedGainDB = -juce::jlimit(1.f, 5.f, (diff-2)*0.8f) * transientCutScale();
        iss.suggestedQ = 1.2f;
        iss.isExcess = true;
        iss.severity = diff > 8 ? AnalysisResult::BandIssue::Critical : AnalysisResult::BandIssue::Warning;
        iss.reason = "Muddiness / boomy at 200-350 Hz (+" + juce::String(diff,1) + " dB) - clean low-mids";
        r.issues.add(iss);
        r.suggestedGains[iss.bandIdx] += iss.suggestedGainDB;
        // anche 400 Hz leggero
        r.suggestedGains[nearestBandIdx(400)] += iss.suggestedGainDB * 0.4f;
    }
}

void IntelligentAnalyzer::detectHarshness(AnalysisResult& r)
{
    // Harsh 2-4kHz
    float harsh = getBandEnergy(3000, 0.7);
    float ref = (getBandEnergy(1000,0.6)+getBandEnergy(6000,0.6))*0.5f;
    float diff = harsh - ref;
    r.harshness = juce::jlimit(0.f,1.f, (diff-2)*0.2f);
    if (diff > 3.5f)
    {
        typename AnalysisResult::BandIssue iss;
        iss.freq = 3000;
        double target = diff > 6 ? 3200 : 2500;
        int idx = nearestBandIdx(target); // 2.5k o 4k secondo layout bande
        iss.bandIdx = idx;
        iss.freq = target;
        iss.detectedLevelDB = diff;
        iss.suggestedGainDB = -juce::jlimit(1.f, 4.5f, (diff-1)*0.7f) * transientCutScale();
        iss.suggestedQ = 1.4f;
        iss.isExcess = true;
        iss.severity = diff > 7 ? AnalysisResult::BandIssue::Critical : AnalysisResult::BandIssue::Warning;
        iss.reason = "Harshness at 2-4 kHz (+" + juce::String(diff,1) + " dB) - causes listening fatigue";
        r.issues.add(iss);
        r.suggestedGains[idx] += iss.suggestedGainDB;
    }
}

void IntelligentAnalyzer::detectSibilance(AnalysisResult& r)
{
    float sib = getBandEnergy(7500, 0.6);
    float ref = getBandEnergy(3000, 0.6);
    float diff = sib - ref;
    r.sibilance = juce::jlimit(0.f,1.f, (diff-1)*0.18f);
    // Sibilanza solo se picco netto 5-9k
    // Cerca picco in 5-9k
    int bLow = (int)freqToBin(5000), bHigh=(int)freqToBin(9000);
    float peakDb=-200; int peakBin=bLow;
    for(int b=bLow;b<=bHigh && b<spectrumSize;++b) if(averagedSpectrumDB[b]>peakDb){peakDb=averagedSpectrumDB[b]; peakBin=b;}
    float medianHigh = getBandEnergy(6000, 1.0);
    float peakDiff = peakDb - medianHigh;
    if (peakDiff > 6 && diff > 0)
    {
        typename AnalysisResult::BandIssue iss;
        iss.freq = binToFreq(peakBin);
        // mappa a 6.3k o 10k
        iss.bandIdx = iss.freq < 8000 ? nearestBandIdx(6300) : nearestBandIdx(10000);
        iss.detectedLevelDB = peakDiff;
        // De-esser vero vs wash di piatti: una sibilante vocale decade sopra i
        // 9 kHz, un crash no. Se l'aria (13k) e' quasi forte quanto il picco,
        // e' wash a banda larga - non una "ess" da togliere.
        float airAbove = getBandEnergy(13000, 0.5);
        float washScale = (sib - airAbove < 6.0f) ? 0.3f : 1.0f;
        iss.suggestedGainDB = -juce::jlimit(1.f, 5.f, peakDiff*0.5f) * washScale * transientCutScale();
        iss.suggestedQ = 2.5f;
        iss.isExcess = true;
        iss.severity = AnalysisResult::BandIssue::Warning;
        iss.reason = "Sibilance / harsh ess at " + juce::String((int)iss.freq) + " Hz - gentle de-esser";
        r.issues.add(iss);
        r.suggestedGains[iss.bandIdx] += iss.suggestedGainDB;
    }
}

void IntelligentAnalyzer::detectLowDeficiency(AnalysisResult& r)
{
    // Bassi deboli se <80Hz molto sotto 150Hz
    float sub = getBandEnergy(40, 0.8);
    float low = getBandEnergy(120, 0.6);
    float diff = low - sub; // positivo = sub carente
    if (diff > 8)
    {
        // Solo se non già fangoso
        if (r.muddiness < 0.4)
        {
            typename AnalysisResult::BandIssue iss;
            iss.freq = 40;
            iss.bandIdx = nearestBandIdx(40);
            iss.detectedLevelDB = -diff;
            iss.suggestedGainDB = juce::jlimit(1.f, 4.f, (diff-6)*0.4f);
            iss.suggestedQ = 0.9f;
            iss.isExcess = false;
            iss.severity = AnalysisResult::BandIssue::Info;
            iss.reason = "Weak deep bass (sub lacking " + juce::String(diff,1) + " dB) - gentle low-shelf boost";
            r.issues.add(iss);
            r.suggestedGains[nearestBandIdx(25)] += iss.suggestedGainDB * 0.6f;
            r.suggestedGains[nearestBandIdx(40)] += iss.suggestedGainDB;
        }
    }
    // Aria / high carente
    float air = getBandEnergy(15000, 0.5);
    float pres = getBandEnergy(4000, 0.6);
    float airDiff = pres - air;
    if (airDiff > 10)
    {
        typename AnalysisResult::BandIssue iss;
        iss.freq = 16000;
        iss.bandIdx = nearestBandIdx(16000);
        iss.detectedLevelDB = -airDiff;
        iss.suggestedGainDB = juce::jlimit(0.8f, 3.f, (airDiff-8)*0.25f);
        iss.suggestedQ = 0.8f;
        iss.isExcess = false;
        iss.severity = AnalysisResult::BandIssue::Info;
        iss.reason = "Lack of air / brilliance above 12kHz - gentle high-shelf";
        r.issues.add(iss);
        r.suggestedGains[nearestBandIdx(20000)] += iss.suggestedGainDB;
        r.suggestedGains[nearestBandIdx(16000)] += iss.suggestedGainDB*0.5f;
    }
}

void IntelligentAnalyzer::detectHarshLowMids(AnalysisResult& r)
{
    // Nasalità 800-1.2k
    float nasal = getBandEnergy(1000, 0.5);
    float ref = (getBandEnergy(500,0.5)+getBandEnergy(2000,0.5))*0.5f;
    float diff = nasal - ref;
    if (diff > 4)
    {
        typename AnalysisResult::BandIssue iss;
        iss.freq = 1000;
        iss.bandIdx = nearestBandIdx(1000);
        iss.detectedLevelDB = diff;
        iss.suggestedGainDB = -juce::jlimit(1.f, 3.5f, (diff-2)*0.6f) * transientCutScale();
        iss.suggestedQ = 1.8f;
        iss.isExcess = true;
        iss.severity = AnalysisResult::BandIssue::Warning;
        iss.reason = "Nasality / honk at 900-1200 Hz (+" + juce::String(diff,1) + " dB)";
        r.issues.add(iss);
        r.suggestedGains[8] += iss.suggestedGainDB;
    }
}

void IntelligentAnalyzer::mapIssuesToBands(AnalysisResult& r)
{
    // I detector scrivono indipendenti sulla stessa banda (es. sibilante +
    // harshness + risonanze sui piatti): la somma saturava il clamp ±9 dB e il
    // FOLLOW pompava. Budget per banda: oltre ±4 dB si scala proporzionalmente
    // (direzione invariata, entita' contenuta) - anche sulle issue, cosi' Q e
    // display restano coerenti con i gain applicati.
    constexpr float kMaxBandTotal = 4.0f;
    for (int i = 0; i < NumBands; ++i)
    {
        float total = r.suggestedGains[i];
        if (std::abs(total) <= kMaxBandTotal) continue;
        float f = kMaxBandTotal / std::abs(total);
        r.suggestedGains[i] = total * f;
        for (auto& iss : r.issues)
            if (iss.bandIdx == i)
                iss.suggestedGainDB *= f;
    }
}

void IntelligentAnalyzer::applyAutoFix(EQProcessor& eq, const AnalysisResult& result, float strength)
{
    strength = juce::jlimit(0.f,1.f,strength);
    for (int i=0;i<NumBands;++i)
    {
        float cur = (float)eq.getBand(i).gainDB;
        float target = cur + result.suggestedGains[i] * strength;
        // Se già non-zero, blend più conservativo
        if (std::abs(cur) > 0.5f) target = cur * 0.7f + target * 0.3f;
        eq.setBandGain(i, juce::jlimit(-12.0, 12.0, (double)target));
        // Adatta Q se issue indica Q stretto
        for (auto &iss : result.issues) if (iss.bandIdx==i && std::abs(iss.suggestedGainDB)>1.5f)
            eq.setBandQ(i, iss.suggestedQ);
    }
}
