#include "PresetManager.h"

EQPreset PresetManager::createPreset(const juce::String& name, const juce::String& cat,
                                     const juce::String& desc,
                                     std::initializer_list<float> gains,
                                     std::initializer_list<float> Qs)
{
    EQPreset p;
    p.name = name; p.category = cat; p.description = desc;
    int i=0;
    for (auto g : gains) if (i < EQProcessor::NumBands) p.gains[i++] = g;
    while (i < EQProcessor::NumBands) p.gains[i++] = 0;
    i=0;
    for (auto q : Qs) if (i < EQProcessor::NumBands) p.Qs[i++] = q;
    while (i < EQProcessor::NumBands) p.Qs[i++] = 0; // 0 = default
    return p;
}

PresetManager::PresetManager() { buildPresets(); }

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray s;
    for (auto &p: presets) s.add(p.name);
    return s;
}
juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray cats;
    for (auto &p: presets) if (!cats.contains(p.category)) cats.add(p.category);
    return cats;
}
juce::Array<EQPreset> PresetManager::getPresetsForCategory(const juce::String& cat) const
{
    juce::Array<EQPreset> r;
    for (auto &p: presets) if (p.category==cat) r.add(p);
    return r;
}
int PresetManager::findPresetByName(const juce::String& name) const
{
    for (int i=0;i<presets.size();++i) if (presets[i].name==name) return i;
    return -1;
}
void PresetManager::applyPreset(EQProcessor& eq, int idx) const
{
    if (idx<0||idx>=presets.size()) return;
    applyPreset(eq, presets[idx]);
}
void PresetManager::applyPreset(EQProcessor& eq, const EQPreset& p) const
{
    eq.applyGains(p.gains);
    for (int i=0;i<EQProcessor::NumBands;++i) if (p.Qs[i] > 0.1f) eq.setBandQ(i, p.Qs[i]);
}

void PresetManager::buildPresets()
{
    // Band order: 25,40,63,100,160,250,400,630,1k,1.6k,2.5k,4k,6.3k,10k,16k,20k

    // === VOCALS ===
    presets.add(createPreset("Transparent Lead Vocal", "Vocals",
        "Rumble cut, presence and air, gentle de-mud. For pop/rock vocals",
        { -2, -1, -1, 0, -1.5, -2, -1, 0, 0.5, 1, 1.5, 0.5, 0, 1, 1.2, 0.8 }));
    presets.add(createPreset("Female Vocal Air", "Vocals",
        "Emphasizes air and sweetness, reduces nasality",
        { -1.5, -1, -0.5, 0, -1, -1.5, -0.5, -1, -0.5, 0, 0.8, 0.5, 0.5, 1.5, 2, 1 }));
    presets.add(createPreset("Warm Male Vocal", "Vocals",
        "Warm body, 2.5k harsh cut, 1.6k presence",
        { -1, 0, 0.5, 0.5, 0.8, -1, -1.5, -0.5, 0.8, 1.2, -1.2, -0.5, 0, 0.5, 0.8, 0 }));
    presets.add(createPreset("Rap / Trap Vocal", "Vocals",
        "Aggressive presence, steep low-cut, brilliance",
        { -4, -3, -2, -1, -1, -1, -0.5, 0, 1, 1.5, 1.2, 1, 0.5, 1.8, 1.5, 1 }));
    presets.add(createPreset("Choir / Backing Vocals", "Vocals",
        "Softens and widens, cuts aggressive mids",
        { -1, -0.5, 0, 0, -0.5, -1, -0.5, 0, 0, -1, -1, 0, 0.5, 1, 1, 0.5 }));
    presets.add(createPreset("Podcast / Voice-Over", "Vocals",
        "Maximum intelligibility, rumble and sibilance cut",
        { -3, -2, -1, 0, 0, -0.5, -1, -0.5, 1, 1.5, 0.5, -1, -0.8, 0.5, 0.5, 0 }));

    // === DRUMS ===
    presets.add(createPreset("Kick Drum Punch", "Drums",
        "Boost 60Hz, attack 2.5k-4k, cut muddiness at 300Hz",
        { 1, 2.5, 3, 1, 0, -2.5, -2, -1, 0, 0.5, 2, 1.5, 0.5, 0, -0.5, 0 }));
    presets.add(createPreset("Snare Crack", "Drums",
        "160Hz body, 1.6k-2.5k crack, 10k air",
        { -1, -1, 0, 0.5, 2, 0.5, -1, -0.5, 0.5, 2, 2.5, 1, 0, 1.5, 1, 0 }));
    presets.add(createPreset("Hi-Hat / Cymbals Shine", "Drums",
        "Low cut, emphasize 8-12k shimmer",
        { -6, -6, -4, -2, -1, -0.5, 0, 0, 0, 0, 0, 0, 1.5, 2.5, 2, 1.2 }));
    presets.add(createPreset("Full Toms", "Drums",
        "Powerful low-mid, defined attack",
        { 0, 1, 1.5, 1, 1.5, 0, -1, 0, 0.5, 1, 1.2, 0.5, 0, 0.8, 0, 0 }));
    presets.add(createPreset("Drum Bus Glue", "Drums",
        "Compact bus, subtle smile, harsh control",
        { 0.5, 0.8, 0.5, 0, -1, -1, -0.5, 0, 0, 0, -1, -0.5, 0.5, 1, 0.8, 0.5 }));
    presets.add(createPreset("808 Bass Drum", "Drums",
        "Deep sub, mid cut, present click",
        { 2, 3.5, 2, 0.5, -1, -2, -1.5, -1, 0, 0, 1, 0.5, 0, 0.5, 0, 0 }));

    // === BASS ===
    presets.add(createPreset("Defined Electric Bass", "Bass",
        "Fundamental 63-100Hz, 800Hz definition, 250Hz mud cut",
        { 0, 1, 2, 2, 1, -2, -1.5, 0, 1, 0.5, 0, 0, 0, 0.5, 0, 0 }));
    presets.add(createPreset("Synth Bass Sub", "Bass",
        "Monolithic sub, extreme mid cleanup",
        { 1.5, 2.5, 1.5, 0, -1, -2, -2, -1, -0.5, 0, 0, 0, 0, 0.5, 0, 0 }));
    presets.add(createPreset("Jazz Double Bass", "Bass",
        "Natural warmth, wood resonance",
        { 0, 0.5, 1, 1, 0.8, 0, -0.5, 0, 0.5, 0.3, 0, -0.5, 0, 0.8, 0, 0 }));

    // === GUITARS ===
    presets.add(createPreset("Bright Acoustic Guitar", "Guitars",
        "Warm body without boominess, 10k brilliance",
        { -1, -0.5, 0, 0.5, 0, -1.5, -1, 0, 0.5, 0.8, 0.5, 0.5, 0.5, 1.8, 1.5, 0.8 }));
    presets.add(createPreset("Electric Guitar Crunch", "Guitars",
        "Rumble cut, subtle mid scoop, 2.5k presence",
        { -3, -2, -1, 0, 0, -0.5, -1, -1.5, 0, 1, 2, 1, 0, 0.8, 0.5, 0 }));
    presets.add(createPreset("Electric Guitar Clean", "Guitars",
        "Transparent, chime, harsh cut",
        { -1, -0.5, 0, 0, 0, -1, -0.5, 0, 0.5, 0.5, 0.5, -0.5, 0.5, 1.2, 1, 0.5 }));
    presets.add(createPreset("Lead Guitar Solo", "Guitars",
        "Pushes through the mix, mid sustain",
        { -1, 0, 0, 0, 0.5, 0, 0, 0.5, 1, 1.5, 1.5, 1, 0.5, 1, 0.5, 0 }));

    // === PIANO / KEYS ===
    presets.add(createPreset("Grand Piano", "Keys",
        "Classic balance, natural brilliance",
        { 0, 0.5, 0.5, 0, -0.5, -0.5, 0, 0, 0.5, 0.5, 0.3, 0.3, 0.5, 1, 0.8, 0.5 }));
    presets.add(createPreset("Bright Pop Piano", "Keys",
        "Modern, defined attack, high shimmer",
        { -0.5, 0, 0, 0, 0, -1, -0.5, 0, 0.8, 1, 1, 0.5, 0.8, 1.5, 1.2, 0.8 }));
    presets.add(createPreset("Rhodes / Wurlitzer Warm", "Keys",
        "Vintage warm, 1.6k bell",
        { 0, 0.5, 0.8, 0.5, 0.5, 0, -0.5, 0, 1, 1.5, 0.5, -0.5, 0, 0.5, 0, 0 }));
    presets.add(createPreset("Synth Pad Lush", "Keys",
        "Soft, lush, harsh cut",
        { 0, 0.3, 0.5, 0, -0.5, -0.5, 0, 0, 0, -1, -1, -0.5, 0, 0.8, 0.5, 0 }));
    presets.add(createPreset("Sharp Synth Lead", "Keys",
        "Extreme presence for synth lead",
        { -1, -0.5, 0, 0, 0, 0, 0, 0, 0.5, 1.2, 2, 1.5, 0.5, 1, 0.5, 0 }));

    // === STRINGS / ORCHESTRA ===
    presets.add(createPreset("Warm String Section", "Orchestra",
        "Body and silk, 1k nasality cut",
        { 0, 0.3, 0.5, 0.5, 0, -0.5, -0.5, -1, -0.5, 0, 0, 0, 0.5, 1, 1, 0.5 }));
    presets.add(createPreset("Solo Cello", "Orchestra",
        "Deep resonance, 1-2k singing tone",
        { 0, 0.5, 0.8, 1, 0.5, 0, -0.5, 0, 1, 1, 0.5, 0, 0, 0.5, 0, 0 }));
    presets.add(createPreset("Bright Brass", "Orchestra",
        "Brassy, 3k harsh cut",
        { -0.5, 0, 0, 0, 0, 0, -0.5, 0, 0.5, 1, 0.5, -1, 0.5, 1.5, 1, 0.5 }));
    presets.add(createPreset("Woodwinds / Flute Air", "Orchestra",
        "Breath and brilliance",
        { -0.5, 0, 0, 0, -0.5, -0.5, 0, 0, 0, 0.5, 0.5, 0.5, 1, 1.8, 1.5, 1 }));

    // === MASTERING ===
    presets.add(createPreset("Transparent Mastering", "Mastering",
        "Gentle correction, balanced loudness",
        { 0, 0.3, 0.3, 0, -0.5, -0.7, -0.3, 0, 0, 0, -0.5, -0.3, 0.3, 0.5, 0.5, 0.3 }));
    presets.add(createPreset("Warm Analog Mastering", "Mastering",
        "Low saturation, high softness",
        { 0.5, 0.8, 0.5, 0.3, 0.3, -0.3, -0.5, -0.3, 0, -0.5, -0.5, -0.3, 0, 0.3, 0, -0.2 }));
    presets.add(createPreset("Modern Bright Mastering", "Mastering",
        "Open modern, emphasized hi-end",
        { -0.2, 0, 0, 0, -0.5, -0.5, 0, 0, 0.2, 0.3, 0.5, 0.5, 0.8, 1, 0.8, 0.5 }));
    presets.add(createPreset("Maximum Loudness Mastering", "Mastering",
        "Energy, sub control, mud cut",
        { -0.5, 0, 0.5, 0.5, 0, -1, -1, -0.5, 0.5, 0.5, 0.3, 0, 0.5, 0.8, 0.5, 0 }));
    presets.add(createPreset("Vinyl Mastering", "Mastering",
        "Rolled-off extreme sub and air for vinyl",
        { -1, -0.5, 0, 0.3, 0, -0.3, 0, 0, 0, 0, -0.3, -0.5, -0.3, -0.5, -0.8, -1.5 }));

    // === MIX BUS / GENERIC ===
    presets.add(createPreset("Mix Bus Glue", "Mix",
        "Gentle glue, overall control",
        { 0.2, 0.3, 0.3, 0, -0.8, -0.8, -0.3, 0, 0, 0, -0.5, -0.3, 0.3, 0.6, 0.4, 0.2 }));
    presets.add(createPreset("EDM Pump", "Mix",
        "Extreme smile, pushed sub and air",
        { 1, 2, 1.5, 0.5, -0.5, -1.5, -1, 0, 0, 0, 0.5, 0.5, 1, 1.5, 1.2, 0.8 }));
    presets.add(createPreset("Lo-Fi Vintage", "Mix",
        "Telephone-like, extremes cut, mid push",
        { -4, -3, -2, 0, 1, 1, 0.5, 1, 1, 0.5, -1, -2, -1.5, -2, -3, -4 }));
    presets.add(createPreset("Telephone / Radio", "Mix",
        "Narrow band 300-3k",
        { -8, -8, -6, -2, 1, 2, 2, 1.5, 1, 0.5, -1, -4, -6, -6, -8, -8 }));
    presets.add(createPreset("Flat / Reset", "Utility",
        "No correction, starting point",
        { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }));
}
