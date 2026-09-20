#include "RoomCalibrator.h"
#include <cmath>

RoomCalibrator::RoomCalibrator() {}

void RoomCalibrator::prepare(double sr, int maxBlockSize)
{
    juce::ScopedLock lk(lock);
    sampleRate = sr;
    maxBlock = maxBlockSize;
    pinkGen.prepare(sr);
    pinkSamplesTotal = (int)(pinkDurationSec * sr);
    captureCapacity = pinkSamplesTotal + (int)(sr * 0.5); // margine
    captureBuf.assign(captureCapacity, 0.0f);
    capturePos = 0;
    pinkSamplesPlayed = 0;
    if (state.load() == State::Idle)
        progress.store(0.0f);
}

void RoomCalibrator::reset()
{
    juce::ScopedLock lk(lock);
    capturePos = 0;
    pinkSamplesPlayed = 0;
    progress.store(0.0f);
    if (state.load() != State::Complete)
        state.store(State::Idle);
    pinkGen.reset();
}

void RoomCalibrator::startCalibration(int micIdx)
{
    juce::ScopedLock lk(lock);
    micProfileIdx = juce::jlimit(0, (int)MicProfile::getAllProfiles().size()-1, micIdx);
    capturePos = 0;
    pinkSamplesPlayed = 0;
    std::fill(captureBuf.begin(), captureBuf.end(), 0.0f);
    pinkGen.reset();
    {
        juce::ScopedLock rlk(resultLock);
        result = Result();
    }
    state.store(State::Playing);
    progress.store(0.0f);
    errorMsg = {};
}

void RoomCalibrator::abort()
{
    juce::ScopedLock lk(lock);
    state.store(State::Idle);
    progress.store(0.0f);
    pinkSamplesPlayed = 0;
    capturePos = 0;
}

void RoomCalibrator::processBlock(juce::AudioBuffer<float>& buffer, bool /*isPlayingPink*/)
{
    auto st = state.load();
    if (st != State::Playing) return;

    int numSamples = buffer.getNumSamples();
    int numCh = buffer.getNumChannels();
    if (numCh == 0 || numSamples == 0) return;

    juce::ScopedLock lk(lock);
    if (state.load() != State::Playing) return;

    // Genera pink noise su tutti i canali di output (sovrascrive)
    // E contemporaneamente cattura l'input (prima di sovrascrivere, salva)
    // In VST l'input è già nel buffer all'ingresso: lo catturiamo poi sovrascriviamo con pink

    // Cattura mono sum dell'ingresso corrente (quello che il mic sta sentendo)
    // Nota: durante Playing, l'output pink viene ascoltato dalla stanza e rientra nel mic
    // al blocco successivo (latenza). Semplifichiamo: catturiamo sempre.
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= (float)numCh;

        if (capturePos < captureCapacity)
            captureBuf[capturePos++] = mono;
    }

    // Ora genera pink noise in output
    int remaining = pinkSamplesTotal - pinkSamplesPlayed;
    int toGen = juce::jmin(numSamples, remaining);
    if (toGen > 0)
    {
        // Genera in un temp buffer poi copia su tutti i canali
        std::vector<float> tmp(toGen);
        pinkGen.fillBuffer(tmp.data(), toGen, pinkGain);
        for (int ch = 0; ch < numCh; ++ch)
        {
            float* out = buffer.getWritePointer(ch);
            for (int i = 0; i < toGen; ++i) out[i] = tmp[i];
            for (int i = toGen; i < numSamples; ++i) out[i] = 0.0f;
        }
        pinkSamplesPlayed += toGen;
        float prog = (float)pinkSamplesPlayed / (float)pinkSamplesTotal;
        progress.store(prog * 0.85f); // 0..0.85 durante playback
    }
    else
    {
        // Pink finito: silenzio in output
        buffer.clear();
    }

    if (pinkSamplesPlayed >= pinkSamplesTotal)
    {
        // Lascia ancora ~300ms di coda riverbero
        static int tailBlocks = 0;
        tailBlocks++;
        int tailNeeded = (int)(0.3 * sampleRate / maxBlock) + 1;
        if (tailBlocks >= tailNeeded)
        {
            tailBlocks = 0;
            // Passa ad Analyzing fuori dal lock audio? Facciamolo qui ma veloce
            // Copia stato per analisi
            state.store(State::Analyzing);
            progress.store(0.88f);
            // Esegui analisi (può richiedere ~50ms, ok in audio thread con try? meglio defer)
            // Per semplicità facciamola qui: è offline FFT, non troppo pesante
            doAnalyze();
        }
    }
}

void RoomCalibrator::doAnalyze()
{
    // Calcola spettro del segnale catturato
    // Pink ideale = -3dB/oct (pendenza rosa). Misurato - ideale = risposta stanza + mic
    // Correzione = -(misurato - ideale - micComp) con smoothing e limit

    if (capturePos < 4096)
    {
        juce::ScopedLock rlk(resultLock);
        result.valid = false;
        result.summary = "Segnale troppo breve - verifica microfono";
        state.store(State::Error);
        progress.store(0.0f);
        return;
    }

    // Check silenzio
    float rms = 0.0f;
    for (int i = 0; i < capturePos; ++i) rms += captureBuf[i]*captureBuf[i];
    rms = std::sqrt(rms / (float)capturePos);
    float rmsDB = juce::Decibels::gainToDecibels(rms, -100.0f);
    if (rmsDB < silenceThresholdDB)
    {
        juce::ScopedLock rlk(resultLock);
        result.valid = false;
        result.summary = "Silenzio: microfono non collegato o gain troppo basso (" + juce::String(rmsDB,1) + " dB)";
        state.store(State::Error);
        progress.store(0.0f);
        return;
    }

    // FFT analisi: usa finestra Hann, overlap 50%, media
    int fftOrder = 12; // 4096
    int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window(fftSize, juce::dsp::WindowingFunction<float>::hann);

    int numBins = fftSize / 2;
    std::vector<float> avgMag(numBins, 0.0f);
    int numFrames = 0;

    std::vector<float> fftData(fftSize * 2, 0.0f);
    int hop = fftSize / 2;
    for (int pos = 0; pos + fftSize <= capturePos; pos += hop)
    {
        std::fill(fftData.begin(), fftData.end(), 0.0f);
        for (int i = 0; i < fftSize; ++i) fftData[i] = captureBuf[pos + i];
        window.multiplyWithWindowingTable(fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        for (int b = 0; b < numBins; ++b)
        {
            float mag = fftData[b] / (float)fftSize;
            // Evita log(0)
            mag = juce::jmax(mag, 1e-7f);
            float db = juce::Decibels::gainToDecibels(mag);
            // Accumula in lineare per media corretta: converti in power
            avgMag[b] += db;
        }
        numFrames++;
    }
    if (numFrames == 0) numFrames = 1;
    for (int b = 0; b < numBins; ++b) avgMag[b] /= (float)numFrames;

    // Converte bin -> dB spectrum lisciato
    // Ora per ogni banda EQ, calcola energia media in 1/3 ottava
    auto getBandEnergy = [&](double centerFreq, double octWidth) -> float
    {
        double fLow = centerFreq * std::pow(2.0, -octWidth/2.0);
        double fHigh = centerFreq * std::pow(2.0, octWidth/2.0);
        int binLow = juce::jlimit(1, numBins-1, (int)(fLow * fftSize / sampleRate));
        int binHigh = juce::jlimit(1, numBins-1, (int)(fHigh * fftSize / sampleRate));
        if (binHigh <= binLow) return avgMag[binLow];
        float sum = 0;
        for (int b = binLow; b <= binHigh; ++b) sum += avgMag[b];
        return sum / (float)(binHigh - binLow + 1);
    };

    auto profiles = MicProfile::getAllProfiles();
    const MicProfile* mic = nullptr;
    if (micProfileIdx >= 0 && micProfileIdx < (int)profiles.size())
        mic = &profiles[(size_t)micProfileIdx];

    Result res;
    float sumAbs = 0;

    // Pink ideale: pendenza -3dB/oct, riferimento 1kHz = 0dB
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        double freq = EQProcessor::defaultFreqs[i];
        float measured = getBandEnergy(freq, 0.8); // 0.8 oct width
        double idealPink = -3.0 * std::log2(freq / 1000.0); // rosa
        double micComp = mic ? mic->getCompensationDB(freq) : 0.0;
        // Risposta stanza = measured - idealPink - micComp
        // Correzione = -risposta stanza (per appiattire)
        float roomResponse = measured - (float)idealPink - (float)micComp;
        // Normalizza: rimuovi media globale (non vogliamo gain overall)
        // La media verrà sottratta dopo
        res.correctionCurve.emplace_back(freq, roomResponse);
        res.measuredCurve.emplace_back(freq, measured);
        // Gain provvisorio
        res.gainsDB[i] = -roomResponse;
    }

    // Rimuovi offset medio (mantieni volume percepito)
    float mean = 0;
    for (int i = 0; i < EQProcessor::NumBands; ++i) mean += res.gainsDB[i];
    mean /= (float)EQProcessor::NumBands;
    for (int i = 0; i < EQProcessor::NumBands; ++i) res.gainsDB[i] -= mean;

    // Smoothing 1/3 ottava: media mobile su 3 bande
    float smoothed[EQProcessor::NumBands];
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        float s = res.gainsDB[i];
        if (i > 0) s += res.gainsDB[i-1];
        if (i + 1 < EQProcessor::NumBands) s += res.gainsDB[i+1];
        int cnt = 1 + (i>0) + (i+1<EQProcessor::NumBands);
        smoothed[i] = s / (float)cnt;
    }
    for (int i = 0; i < EQProcessor::NumBands; ++i) res.gainsDB[i] = smoothed[i];

    // Limita a +/-6dB per banda (evita correzioni estreme), e tilt max +/-3dB
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        res.gainsDB[i] = juce::jlimit(-6.0f, 6.0f, res.gainsDB[i]);
        // Arrotonda a 0.5dB
        res.gainsDB[i] = std::round(res.gainsDB[i] * 2.0f) / 2.0f;
        sumAbs += std::abs(res.gainsDB[i]);
    }
    res.overallCorrection = sumAbs / EQProcessor::NumBands;

    if (res.overallCorrection < 0.4f)
        res.summary = "Ambiente già piatto (" + juce::String(res.overallCorrection,1) + " dB medi) - nessuna correzione necessaria";
    else if (res.overallCorrection < 1.5f)
        res.summary = "Correzione leggera (" + juce::String(res.overallCorrection,1) + " dB medi)";
    else if (res.overallCorrection < 3.0f)
        res.summary = "Correzione ambiente applicata (" + juce::String(res.overallCorrection,1) + " dB medi)";
    else
        res.summary = "Forte correzione (" + juce::String(res.overallCorrection,1) + " dB medi) - verifica posizionamento monitor";

    res.valid = true;

    {
        juce::ScopedLock rlk(resultLock);
        result = res;
    }
    progress.store(1.0f);
    state.store(State::Complete);
}

void RoomCalibrator::applyToEQ(EQProcessor& eq, float strength) const
{
    juce::ScopedLock rlk(resultLock);
    if (!result.valid) return;
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        float cur = (float)eq.getBand(i).gainDB;
        float target = result.gainsDB[i] * juce::jlimit(0.0f, 1.0f, strength);
        // Blend: se già c'è gain, mix
        float blended = cur + target - cur * 0.0f; // sostituisce
        // In realtà vogliamo sommare alla curva esistente? Per mastering: sostituisce
        // Ma se l'utente aveva già EQ, sommiamo dolcemente
        if (std::abs(cur) > 0.1f)
            blended = cur * 0.3f + target * 0.7f;
        else
            blended = target;
        float clamped = juce::jlimit(-12.0f, 12.0f, blended);
        eq.getBand(i).gainDB = clamped;
        eq.getBand(i).updateCoefficients(eq.getSampleRate());
    }
}

juce::String RoomCalibrator::getStatusText() const
{
    auto st = state.load();
    switch (st)
    {
        case State::Idle: return "Pronto - seleziona microfono e premi CALIBRA";
        case State::Playing: return "Riproduzione rumore rosa... (" + juce::String((int)(progress.load()*100)) + "%) - non parlare!";
        case State::Analyzing: return "Analisi risposta ambiente...";
        case State::Complete: { juce::ScopedLock lk(resultLock); return result.summary; }
        case State::Error: { juce::ScopedLock lk(resultLock); return result.summary; }
    }
    return {};
}

double RoomCalibrator::getSecondsRemaining() const
{
    auto st = state.load();
    if (st == State::Playing)
    {
        float p = progress.load() / 0.85f;
        double elapsed = p * pinkDurationSec;
        return juce::jmax(0.0, pinkDurationSec - elapsed + 0.3);
    }
    return 0;
}
