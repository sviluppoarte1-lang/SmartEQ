#include "SpectrumComponent.h"

SpectrumComponent::SpectrumComponent(SpectrumAnalyzer& sa, EQProcessor& eq, IntelligentAnalyzer& ia)
    : spectrumAnalyzer(sa), eqProcessor(eq), intelligentAnalyzer(ia)
{
    for (int i=0;i<spectrumPoints;++i)
    {
        double t = (double)i / (spectrumPoints-1);
        freqs[i] = 20.0 * std::pow(1000.0, t);
    }
    startTimerHz(30);
}

void SpectrumComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff0d0d14));

    // Aggiorna dati
    if (spectrumAnalyzer.isReady())
        spectrumAnalyzer.getSpectrum(spectrumDB, spectrumPoints);
    else
        for (int i=0;i<spectrumPoints;++i) spectrumDB[i] = -80 + std::sin(i*0.1)*3;

    eqProcessor.getCurve(eqCurveDB, freqs, spectrumPoints);

    // Grid
    drawGrid(g, bounds);

    // --- Spectrum fill ---
    juce::Path specPath, specFill;
    bool first=true;
    for (int i=0;i<spectrumPoints;++i)
    {
        float db = juce::jlimit(-60.f, 12.f, spectrumDB[i]);
        auto pt = freqGainToPixel(freqs[i], db, bounds);
        // leggera offset per visual: mappa -60..12 -> -18..18
        pt.y = juce::jmap(db, -60.f, 12.f, bounds.getBottom() - 20, bounds.getY() + 20);
        if (first) { specPath.startNewSubPath(pt); specFill.startNewSubPath(pt.x, bounds.getBottom()); specFill.lineTo(pt); first=false; }
        else { specPath.lineTo(pt); specFill.lineTo(pt); }
    }
    specFill.lineTo(freqGainToPixel(freqs[spectrumPoints-1], -60, bounds).x, bounds.getBottom());
    specFill.closeSubPath();

    // gradient fill spectrum
    juce::ColourGradient grad(juce::Colour(0x4000ffcc), bounds.getCentreX(), bounds.getY(),
                              juce::Colour(0x0500ffcc), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillPath(specFill);
    g.setColour(juce::Colour(0xff00ffcc).withAlpha(0.9f));
    g.strokePath(specPath, juce::PathStrokeType(1.5f));

    // --- Intelligent overlay (aree problematiche) ---
    if (showIntelligent && analysisResult)
    {
        for (auto &iss : analysisResult->issues)
        {
            auto pt = freqGainToPixel(iss.freq, 0, bounds);
            juce::Colour c = iss.severity == 2 ? juce::Colours::red : iss.severity ==1 ? juce::Colours::orange : juce::Colours::yellow;
            g.setColour(c.withAlpha(0.25f));
            float w = 30 * (iss.suggestedQ > 2 ? 1 : 2);
            g.fillEllipse(pt.x - w/2, bounds.getY(), w, bounds.getHeight());
            g.setColour(c.withAlpha(0.9f));
            g.drawEllipse(pt.x - w/2, bounds.getY(), w, bounds.getHeight(), 1.0f);
            // label
            g.setFont(10.f);
            g.drawText(juce::String((int)iss.freq) + "Hz", (int)(pt.x - 30), (int)bounds.getY()+2, 60, 12, juce::Justification::centred);
        }
    }

    // --- EQ Curve ---
    juce::Path eqPath;
    first=true;
    for (int i=0;i<spectrumPoints;++i)
    {
        float db = juce::jlimit(-18.f, 18.f, eqCurveDB[i]);
        auto pt = freqGainToPixel(freqs[i], db, bounds);
        if (first) { eqPath.startNewSubPath(pt); first=false; }
        else eqPath.lineTo(pt);
    }
    g.setColour(juce::Colours::white);
    g.strokePath(eqPath, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved));

    // Glow
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.strokePath(eqPath, juce::PathStrokeType(6.f, juce::PathStrokeType::curved));

    // --- Bands dots (full-width, color-coded by filter type) ---
    for (int i=0;i<EQProcessor::NumBands;++i)
    {
        const auto &b = eqProcessor.getBand(i);
        // Cut filters ignore gain: pin dot to 0 dB line so curve position is honest
        const float dotGain = Biquad::usesGain(b.type) ? (float)b.gainDB : 0.0f;
        auto pt = freqGainToPixel(b.freq, dotGain, bounds);
        bool isDragging = (i==draggingBand);
        float r = isDragging ? 9 : 6;
        juce::Colour col;
        if (!b.enabled) col = juce::Colours::grey;
        else if (isDragging) col = juce::Colour(0xffffcc00);
        else if (b.type == Biquad::LowPass12 || b.type == Biquad::LowPass24
              || b.type == Biquad::HighPass12 || b.type == Biquad::HighPass24) col = juce::Colour(0xffff7043); // orange-red for cuts
        else if (b.type == Biquad::LowShelf || b.type == Biquad::HighShelf) col = juce::Colour(0xff7bff7b);   // green for shelves
        else if (b.type == Biquad::Notch || b.type == Biquad::BandPass) col = juce::Colour(0xffc07bff);       // purple for BP/Notch
        else col = juce::Colour(0xff00d4ff); // cyan for Bell
        if (Biquad::usesGain(b.type) && std::abs(b.gainDB) < 0.1) col = col.withAlpha(0.6f);
        g.setColour(col);
        g.fillEllipse(pt.x - r, pt.y - r, r*2, r*2);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawEllipse(pt.x - r, pt.y - r, r*2, r*2, 1.2f);
        // index
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(10.f).withStyle(juce::Font::bold));
        g.drawText(juce::String(i+1), (int)(pt.x - r), (int)(pt.y - r), (int)(r*2), (int)(r*2), juce::Justification::centred);
    }

    // Bordo
    g.setColour(juce::Colour(0xff1e1e2a));
    g.drawRect(bounds, 1.5f);
}

void SpectrumComponent::drawGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour(0xff1e1e2a));
    // Horizontal dB lines
    for (int db=-12; db<=12; db+=6)
    {
        auto pt = freqGainToPixel(1000, (float)db, bounds);
        float y = pt.y;
        g.setColour(db==0 ? juce::Colour(0xff2a2a40) : juce::Colour(0xff1e1e2a));
        g.drawHorizontalLine((int)y, bounds.getX(), bounds.getRight());
        g.setColour(juce::Colour(0xff6a6a80));
        g.setFont(9.f);
        g.drawText(juce::String(db) + " dB", (int)bounds.getX()+4, (int)y-8, 40, 10, juce::Justification::left);
    }
    // Vertical freq lines
    const double freqMarks[] = {20,50,100,200,500,1000,2000,5000,10000,20000};
    for (auto f: freqMarks)
    {
        auto pt = freqGainToPixel(f, 0, bounds);
        g.setColour(juce::Colour(0xff1e1e2a));
        g.drawVerticalLine((int)pt.x, bounds.getY(), bounds.getBottom());
        g.setColour(juce::Colour(0xff6a6a80));
        g.setFont(9.f);
        juce::String label = f>=1000 ? juce::String(f/1000,1)+"k" : juce::String((int)f);
        g.drawText(label, (int)pt.x -20, (int)bounds.getBottom()-14, 40, 12, juce::Justification::centred);
    }
}

juce::Point<float> SpectrumComponent::freqGainToPixel(double freq, float gainDB, juce::Rectangle<float> bounds) const
{
    // Full-width log mapping: 20 Hz -> left edge, 20000 Hz -> right edge
    // log10(20000/20) = log10(1000) = 3
    const double f = juce::jlimit(20.0, 20000.0, freq);
    const double logF = std::log10(f / 20.0) / 3.0; // 0..1 across full width
    const float x = (float)(bounds.getX() + logF * bounds.getWidth());
    const float y = juce::jmap(gainDB, -18.f, 18.f, bounds.getBottom() - 18, bounds.getY() + 18);
    return {x,y};
}

void SpectrumComponent::pixelToFreqGain(juce::Point<float> p, juce::Rectangle<float> bounds, double& freq, float& gainDB) const
{
    double t = (p.x - bounds.getX()) / bounds.getWidth();
    t = juce::jlimit(0.0, 1.0, t);
    // Inverse of above: 20 * 10^(t*3)
    freq = 20.0 * std::pow(10.0, t * 3.0);
    gainDB = juce::jmap(p.y, bounds.getY()+18, bounds.getBottom()-18, 18.f, -18.f);
}

int SpectrumComponent::findClosestBand(juce::Point<float> pos, juce::Rectangle<float> bounds) const
{
    int best=-1; float bestDist=1e9;
    for (int i=0;i<EQProcessor::NumBands;++i)
    {
        const auto &b = eqProcessor.getBand(i);
        auto pt = freqGainToPixel(b.freq, (float)b.gainDB, bounds);
        float d = pt.getDistanceFrom(pos);
        if (d < bestDist) { bestDist=d; best=i; }
    }
    return bestDist < 28 ? best : -1;
}

void SpectrumComponent::mouseDown(const juce::MouseEvent& e)
{
    auto bounds = getLocalBounds().toFloat();
    draggingBand = findClosestBand(e.position, bounds);
    if (draggingBand>=0)
    {
        dragStartGain = (float)eqProcessor.getBand(draggingBand).gainDB;
        dragStartFreq = eqProcessor.getBand(draggingBand).freq;
    }
}

void SpectrumComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (draggingBand<0) return;
    auto bounds = getLocalBounds().toFloat();
    double freq; float gain;
    pixelToFreqGain(e.position, bounds, freq, gain);
    gain = juce::jlimit(-18.f, 18.f, gain);
    const auto bandType = eqProcessor.getBand(draggingBand).type;
    const bool gainActive = Biquad::usesGain(bandType);
    bool shift = e.mods.isShiftDown();
    if (shift)
    {
        float deltaY = e.getDistanceFromDragStartY();
        float q = juce::jlimit(0.3f, 10.f, 1.4f + deltaY * -0.02f);
        const float keepGain = gainActive ? (float)eqProcessor.getBand(draggingBand).gainDB : 0.0f;
        if (onBandChanged) onBandChanged(draggingBand, keepGain, (float)freq, q);
    }
    else
    {
        freq = juce::jlimit(20.0, 20000.0, freq);
        // Cut / BP / Notch / AllPass ignore gain: only frequency moves vertically-locked
        const float outGain = gainActive ? gain : 0.0f;
        if (onBandChanged) onBandChanged(draggingBand, outGain, (float)freq, (float)eqProcessor.getBand(draggingBand).Q);
    }
    repaint();
}
