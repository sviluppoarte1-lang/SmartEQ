#pragma once
#include <JuceHeader.h>

// Database microfoni per correzione ambiente stile Sansui SE-9
// Ogni profilo contiene la curva di compensazione per linearizzare la risposta

struct MicProfile
{
    juce::String name;
    juce::String category; // "Misura", "Condensatore", "Dinamico"
    juce::String description;

    // Curva di compensazione: punti (freq Hz, gain dB da sottrarre)
    // Es. SM58 ha +5dB a 5kHz -> compensazione -5dB
    struct CompPoint { double freq; double gainDB; };
    std::vector<CompPoint> compCurve;

    double getCompensationDB(double freq) const
    {
        if (compCurve.empty()) return 0.0;
        if (freq <= compCurve.front().freq) return compCurve.front().gainDB;
        if (freq >= compCurve.back().freq) return compCurve.back().gainDB;
        for (size_t i = 0; i + 1 < compCurve.size(); ++i)
        {
            if (freq >= compCurve[i].freq && freq <= compCurve[i+1].freq)
            {
                double t = (std::log(freq / compCurve[i].freq))
                         / std::log(compCurve[i+1].freq / compCurve[i].freq);
                return compCurve[i].gainDB + t * (compCurve[i+1].gainDB - compCurve[i].gainDB);
            }
        }
        return 0.0;
    }

    static std::vector<MicProfile> getAllProfiles()
    {
        std::vector<MicProfile> v;

        // --- Microfoni di misura (piatti) ---
        v.push_back({"Behringer ECM8000", "Misura", "Omni misura, quasi piatto",
            {{20,0},{100,0},{1000,0},{5000,0},{10000,0},{20000,0}}});
        v.push_back({"miniDSP UMIK-1", "Misura", "USB misura calibrato",
            {{20,0},{100,0},{1000,0},{5000,0},{10000,0},{20000,0}}});
        v.push_back({"miniDSP UMIK-2", "Misura", "USB misura nuova gen",
            {{20,0},{100,0},{1000,0},{5000,0},{10000,0},{20000,0}}});
        v.push_back({"Dayton EMM-6", "Misura", "Misura economico piatto",
            {{20,0},{100,0},{1000,0},{5000,0},{10000,0},{20000,0}}});
        v.push_back({"Earthworks M30", "Misura", "Riferimento laboratorio",
            {{20,0},{100,0},{1000,0},{5000,0},{10000,0},{20000,0}}});
        v.push_back({"Sonarworks XREF20", "Misura", "Bundle Sonarworks",
            {{20,0},{100,0},{1000,0},{5000,0},{10000,0},{20000,0}}});

        // --- Condensatori ---
        v.push_back({"Neumann U87", "Condensatore", "Classico condensatore largo diaframma",
            {{20,0},{100,0.5},{500,0},{2000,1.5},{5000,2.0},{10000,1.0},{20000,-0.5}}});
        v.push_back({"AKG C414", "Condensatore", "Multi-pattern",
            {{20,0},{100,0},{500,0},{2000,1.0},{5000,2.5},{10000,1.5},{20000,0}}});
        v.push_back({"Rode NT1-A", "Condensatore", "Low noise",
            {{20,-0.5},{100,0},{500,0},{2000,0.8},{5000,1.2},{10000,0.5},{20000,-0.5}}});
        v.push_back({"Audio-Technica AT2020", "Condensatore", "Entry level",
            {{20,-0.5},{100,0},{500,0},{2000,0.5},{5000,2.0},{8000,1.0},{20000,-1.0}}});
        v.push_back({"Neumann KM184", "Condensatore", "Small diaphragm",
            {{20,0},{100,0},{500,0},{2000,0.5},{5000,1.0},{10000,0.8},{20000,0}}});

        // --- Dinamici ---
        v.push_back({"Shure SM58", "Dinamico", "Presenza 2-6kHz",
            {{20,-2},{100,-1},{500,0},{2000,2.0},{5000,5.0},{7000,3.0},{10000,0},{20000,-2}}});
        v.push_back({"Shure SM57", "Dinamico", "Simile SM58 piu brillante",
            {{20,-1.5},{100,-0.5},{500,0},{2000,2.5},{5000,5.5},{7000,3.5},{10000,0.5},{20000,-1.5}}});
        v.push_back({"Sennheiser MD421", "Dinamico", "Bassa estesa",
            {{20,1.0},{100,0.5},{500,0},{2000,1.0},{5000,2.0},{8000,1.0},{20000,-1}}});
        v.push_back({"Electro-Voice RE20", "Dinamico", "Broadcast",
            {{20,0},{100,0},{500,0},{2000,1.0},{5000,1.5},{8000,0.5},{20000,-0.5}}});
        v.push_back({"Beyerdynamic M88", "Dinamico", "Ipercardioide",
            {{20,0.5},{100,0},{500,0},{2000,1.2},{5000,2.2},{8000,1.0},{20000,-0.5}}});

        // --- Generico / Flat ---
        v.push_back({"Generico Flat", "Generico", "Nessuna compensazione",
            {{20,0},{20000,0}}});
        v.push_back({"Smartphone", "Generico", "Microfono telefono, taglia bassi",
            {{20,-8},{100,-6},{300,-3},{1000,0},{4000,2},{8000,0},{20000,-4}}});
        v.push_back({"Laptop integrato", "Generico", "Microfono portatile",
            {{20,-10},{100,-8},{300,-4},{1000,0},{4000,3},{8000,1},{20000,-6}}});

        return v;
    }

    static juce::StringArray getProfileNames()
    {
        juce::StringArray names;
        for (auto& p : getAllProfiles()) names.add(p.name);
        return names;
    }
};
