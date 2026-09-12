#pragma once
#include <JuceHeader.h>
#include "../DSP/EQProcessor.h"

struct EQPreset
{
    juce::String name;
    juce::String category; // Vocals, Drums, Guitar, Mastering, etc.
    juce::String description;
    float gains[EQProcessor::NumBands] = {0};
    float Qs[EQProcessor::NumBands] = {0};
    // Se Q =0 usa default

    juce::String toString() const;
};

class PresetManager
{
public:
    PresetManager();

    int getNumPresets() const { return presets.size(); }
    const EQPreset& getPreset(int idx) const { return presets.getReference(idx); }
    juce::StringArray getPresetNames() const;
    juce::StringArray getCategories() const;
    juce::Array<EQPreset> getPresetsForCategory(const juce::String& cat) const;

    int findPresetByName(const juce::String& name) const;
    void applyPreset(EQProcessor& eq, int idx) const;
    void applyPreset(EQProcessor& eq, const EQPreset& p) const;

    // Preset factory
    static EQPreset createPreset(const juce::String& name, const juce::String& cat,
                                 const juce::String& desc,
                                 std::initializer_list<float> gains,
                                 std::initializer_list<float> Qs = {});

private:
    juce::Array<EQPreset> presets;
    void buildPresets();
};
