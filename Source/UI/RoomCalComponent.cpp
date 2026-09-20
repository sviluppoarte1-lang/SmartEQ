#ifndef SMARTEQ_FREE_VERSION
#include "RoomCalComponent.h"
#include "../PluginProcessor.h"
#include "../DSP/MicProfile.h"

RoomCalComponent::RoomCalComponent(SmartEQAudioProcessor& p)
    : processor(p)
{
    // Mic selector
    auto profiles = MicProfile::getAllProfiles(); // for tooltip count
    micBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1a1a2e));
    micBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe0e0e0));
    addAndMakeVisible(micBox);
    micLabel.setText("MIC:", juce::dontSendNotification);
    micLabel.setJustificationType(juce::Justification::centredRight);
    micLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0cc));
    micLabel.setFont(juce::Font(11.0f).boldened());
    addAndMakeVisible(micLabel);

    // Fill mic box with categories grouped
    auto all = MicProfile::getAllProfiles();
    micBox.clear();
    juce::String lastCat;
    int id = 1;
    for (auto& m : all)
    {
        if (m.category != lastCat)
        {
            // Add separator via disabled item? JUCE doesn't have groups, use prefix
            lastCat = m.category;
        }
        micBox.addItem(m.name + "  [" + m.category + "]", id++);
    }
    micBox.setSelectedId(1, juce::dontSendNotification);
    micBox.setTooltip("Seleziona il microfono collegato - la curva di compensazione verrà applicata automaticamente");

    micAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, "micProfile", micBox);

    // CAL button - teal SE-9 style
    calButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff00b894));
    calButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    calButton.setTooltip("Collega il microfono, posizionalo nel punto di ascolto, premi ROOM CAL. Verrà riprodotto rumore rosa per 6 secondi - non parlare durante la misura!");
    calButton.onClick = [this]()
    {
        auto st = processor.getRoomCalibrator().getState();
        if (st == RoomCalibrator::State::Playing || st == RoomCalibrator::State::Analyzing)
            return;
        processor.startRoomCalibration();
    };
    addAndMakeVisible(calButton);

    abortButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd63031));
    abortButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    abortButton.onClick = [this]() { processor.abortRoomCalibration(); };
    addChildComponent(abortButton);

    applyButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff0984e3));
    applyButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    applyButton.setTooltip("Applica la correzione con fader motorizzati (animazione SE-9)");
    applyButton.onClick = [this]()
    {
        // Salva target per animazione
        auto res = processor.getRoomCalibrator().getResult();
        if (!res.valid) return;
        for (int i = 0; i < EQProcessor::NumBands; ++i)
        {
            if (auto* p = processor.apvts.getRawParameterValue("band" + juce::String(i) + "_gain"))
                animStartGains[i] = p->load();
            animTargetGains[i] = res.gainsDB[i];
        }
        startMotorizedAnimation();
    };
    addChildComponent(applyButton);

    statusLabel.setJustificationType(juce::Justification::centredLeft);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0cc));
    statusLabel.setFont(juce::Font(11.0f));
    statusLabel.setText("Seleziona microfono e premi ROOM CAL", juce::dontSendNotification);
    addAndMakeVisible(statusLabel);

    progressBar.setColour(juce::ProgressBar::backgroundColourId, juce::Colour(0xff1a1a2e));
    progressBar.setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff00b894));
    addAndMakeVisible(progressBar);
}

void RoomCalComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff12121a));
    g.fillRoundedRectangle(b, 4);
    g.setColour(juce::Colour(0xff2a2a3e));
    g.drawRoundedRectangle(b, 4, 1.0f);
}

void RoomCalComponent::resized()
{
    auto area = getLocalBounds().reduced(6, 4);
    auto topRow = area.removeFromTop(26);
    micLabel.setBounds(topRow.removeFromLeft(36));
    micBox.setBounds(topRow.removeFromLeft(210));
    topRow.removeFromLeft(6);
    calButton.setBounds(topRow.removeFromLeft(92));
    topRow.removeFromLeft(4);
    abortButton.setBounds(topRow.removeFromLeft(64));
    applyButton.setBounds(topRow.removeFromLeft(68));

    area.removeFromTop(4);
    auto bottomRow = area;
    progressBar.setBounds(bottomRow.removeFromLeft(110));
    bottomRow.removeFromLeft(8);
    statusLabel.setBounds(bottomRow);
}

void RoomCalComponent::refresh()
{
    auto& cal = processor.getRoomCalibrator();
    auto st = cal.getState();
    progressVal = cal.getProgress();
    progressBar.setVisible(st == RoomCalibrator::State::Playing || st == RoomCalibrator::State::Analyzing);

    bool isPlaying = (st == RoomCalibrator::State::Playing || st == RoomCalibrator::State::Analyzing);
    calButton.setVisible(!isPlaying && st != RoomCalibrator::State::Complete);
    abortButton.setVisible(isPlaying);
    applyButton.setVisible(st == RoomCalibrator::State::Complete && cal.hasValidResult() && !animating);

    juce::String txt = cal.getStatusText();
    if (st == RoomCalibrator::State::Playing)
    {
        double rem = cal.getSecondsRemaining();
        txt += "  " + juce::String(rem, 1) + "s";
        calButton.setEnabled(false);
    }
    else
    {
        calButton.setEnabled(true);
    }
    if (animating)
        txt = "Motori in movimento... " + juce::String((int)(100.0f * animStep / animSteps)) + "%";

    statusLabel.setText(txt, juce::dontSendNotification);

    // Colore stato
    if (st == RoomCalibrator::State::Error)
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffff6b6b));
    else if (st == RoomCalibrator::State::Complete)
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff00b894));
    else
        statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b0cc));

    repaint();
}

void RoomCalComponent::startMotorizedAnimation()
{
    animating = true;
    animStep = 0;
}

void RoomCalComponent::updateAnimation()
{
    if (!animating) return;
    animStep++;
    float t = (float)animStep / (float)animSteps;
    // Ease out cubic
    t = 1.0f - std::pow(1.0f - t, 3.0f);
    for (int i = 0; i < EQProcessor::NumBands; ++i)
    {
        float cur = animStartGains[i] + (animTargetGains[i] - animStartGains[i]) * t;
        if (auto* p = processor.apvts.getParameter("band" + juce::String(i) + "_gain"))
            p->setValueNotifyingHost(p->convertTo0to1(cur));
    }
    if (animStep >= animSteps)
    {
        animating = false;
        // Final precise apply
        processor.applyRoomCalibration(1.0f);
    }
}

#endif
