#pragma once
#include <JuceHeader.h>

#ifndef SMARTEQ_FREE_VERSION
#include "../DSP/MicProfile.h"
#include "../DSP/RoomCalibrator.h"
#include "../DSP/EQProcessor.h"
#include "../PluginProcessor.h"

// Pannello compatto per calibrazione ambiente SE-9 style
class RoomCalComponent : public juce::Component
{
public:
    RoomCalComponent(SmartEQAudioProcessor& p);
    ~RoomCalComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Chiamato dal timer dell'editor per aggiornare progress/status
    void refresh();

    // Animazione fader motorizzati
    void startMotorizedAnimation();
    bool isAnimating() const { return animating; }
    void updateAnimation();

private:
    SmartEQAudioProcessor& processor;

    juce::ComboBox micBox;
    juce::Label micLabel;
    juce::TextButton calButton { "ROOM CAL" };
    juce::TextButton applyButton { "APPLY" };
    juce::TextButton abortButton { "ABORT" };
    juce::Label statusLabel;
    juce::ProgressBar progressBar { progressVal };

    double progressVal = 0.0;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> micAttach;

    // Animazione motorizzata
    bool animating = false;
    int animStep = 0;
    static constexpr int animSteps = 36; // ~0.6s a 60Hz
    float animStartGains[EQProcessor::NumBands] = {0};
    float animTargetGains[EQProcessor::NumBands] = {0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoomCalComponent)
};

#endif
