#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/SpectrumComponent.h"
#include "UI/ModernLookAndFeel.h"

class SmartEQAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    SmartEQAudioProcessorEditor(SmartEQAudioProcessor&);
    ~SmartEQAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    SmartEQAudioProcessor& processor;

    // Shared modern styling (declared first: outlives all components)
    ModernLookAndFeel modernLF;

    // UI Components
    std::unique_ptr<SpectrumComponent> spectrum;

    juce::TextButton analyzeButton { "ANALYZE & FIX" };
    juce::TextButton bypassButton { "BYPASS" };
    juce::ComboBox presetBox;
    juce::ComboBox categoryBox;
    juce::Label presetLabel, categoryLabel;
    juce::Slider inputGainSlider, outputGainSlider, strengthSlider;
    juce::Label inputLabel, outputLabel, strengthLabel;
    juce::TextButton resetButton { "RESET" };
    juce::Label statusLabel;
    juce::Label titleLabel;
    juce::Label analyzerStatus;

#ifndef SMARTEQ_FREE_VERSION
    // Song-map controls (Full version only)
    juce::TextButton songLearnButton { "SONG LEARN" };
    juce::TextButton songFollowButton { "FOLLOW" };
    juce::TextButton songClearButton { "CLEAR" };

    // Demo bar (Full only, public build): countdown + buy link, no key field.
    // Full-edition purchase happens outside the plugin - no secrets here.
    juce::Label demoLabel;
    juce::TextButton buyButton { "BUY FULL VERSION - 19.99 EUR" };

    struct ExpiredOverlay;
    std::unique_ptr<ExpiredOverlay> expiredOverlay;
#endif

    // 16 bands with full professional filter-type selector
    struct BandStrip : public juce::Component
    {
        juce::Slider gain { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
        juce::Slider freq { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Slider q { juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow };
        juce::ComboBox type;
        juce::Label name, gainL, freqL, qL, typeL;
        juce::ToggleButton enabled { "ON" };
        int idx=0;
        BandStrip(int i);
        void resized() override;
    };
    juce::OwnedArray<BandStrip> bandStrips;
    juce::Viewport bandViewport;
    juce::Component bandContainer;

    juce::AudioProcessorValueTreeState::SliderAttachment* inputAttach = nullptr;
    juce::AudioProcessorValueTreeState::SliderAttachment* outputAttach = nullptr;
    juce::AudioProcessorValueTreeState::SliderAttachment* strengthAttach = nullptr;
    juce::AudioProcessorValueTreeState::ButtonAttachment* bypassAttach = nullptr;
#ifndef SMARTEQ_FREE_VERSION
    juce::AudioProcessorValueTreeState::ButtonAttachment* songLearnAttach = nullptr;
    juce::AudioProcessorValueTreeState::ButtonAttachment* songFollowAttach = nullptr;
#endif
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> bandGainAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> bandFreqAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> bandQAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bandEnabledAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bandTypeAttachments;

    void updatePresetBox();
    void applyPresetFromBox();
    bool isInitializing = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmartEQAudioProcessorEditor)
};
