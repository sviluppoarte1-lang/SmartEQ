
<img width="1289" height="736" alt="smarteq" src="https://github.com/user-attachments/assets/1fb20568-b95b-4bd7-8b4f-607d0e9cbc34" />

# SmartEQ by Fear Escape — Intelligent Equalizer VST3

**Two editions: SmartEQ Free (8 bands) and SmartEQ Full (16 bands + professional spectrum analyzer + auto-fix)**

Compatible **Windows &amp; Linux** — VST3 + Standalone — Mastering &amp; Editing.

---

## Editions

| | **SmartEQ Free** | **SmartEQ Full** |
|---|---|---|
| Bands | 8 (63 – 10k Hz musical spread) | 16 (25 Hz – 20 kHz ISO) |
| Filter types per band | All 10 professional types | All 10 professional types |
| Spectrum analyzer | No | Yes (FFT 2048, full-panel) |
| ANALYZE &amp; FIX engine | No | Yes |
| License | Free | 19.99 EUR |
| Demo | N/A (fully working) | 45 minutes per session, then audio mutes |

**Buy the Full version:** https://www.paypal.com/paypalme/fearescape/19.99 —
after payment you receive the fully unlocked Full build. This repository
contains no license keys: the Free edition is fully working, the Full
edition here runs the 45-minute demo.

---

## Features (both editions)

### Graphic EQ with 10 professional filter types per band
- Bell, Low Shelf, High Shelf, **Low Pass 12 / 24**, **High Pass 12 / 24**,
  Band Pass, Notch, All Pass — selectable per band from the Type combo box
- Gain ±18 dB (bell/shelves), Q 0.1–10, bypass per band
- RBJ cookbook biquads, Transposed Direct Form II, anti-denormal,
  24 dB/oct slopes via cascaded stages
- Drag &amp; drop directly on the EQ curve (Shift+drag = Q);
  cut-filter nodes lock to 0 dB since they ignore gain
- Fully automatable host parameters, persistent session state

### Professional spectrum analyzer (Full only)
- Real-time FFT 2048, Blackman-Harris window, 30 fps,
  512 log-spaced points from 20 Hz to 20 kHz
- Full-panel display: gradient spectrum fill + glowing EQ curve,
  color-coded band nodes, log grid ±18 dB

### ANALYZE &amp; FIX engine (Full only)
The red **ANALYZE &amp; FIX** button listens to the track (2–4 seconds),
detects narrow resonances, muddiness, harshness, sibilance, nasality,
weak sub and missing air, then auto-corrects with an adjustable
**AutoFix Strength** control. Produces a 0–100 quality score and a
red/orange overlay on the spectrum.

It also performs **global tilt balancing**: it measures the overall
low-vs-high balance and gently brightens dark material (or warms thin
material) with wide low/high corrections capped at ±2.5 dB — only when
the lean is clear, never on balanced mixes.

Narrow-resonance detection uses a **harmonic veto**: peaks belonging to
a harmonic family (integer-ratio relatives, or a strong tone at the
subharmonic) are musical partials, not defects, and are only flagged
above +14 dB (+18 dB always flags). Solo inharmonic peaks — room modes,
mic ringing, harshness — keep the normal +8 dB threshold, so the
auto-fix cuts problems, never music.

### 38 professional presets (English)
Vocals (Transparent Lead, Female Air, Warm Male, Rap/Trap, Choir/Backing,
Podcast/Voice-Over), Drums (Kick Punch, Snare, Hi-Hat Shine, Toms,
Bus Glue, 808), Bass, Guitars, Keys, Orchestra, Mastering
(Transparent, Warm Analog, Modern Bright, Loudness Max, Vinyl),
Mix (Bus Glue, EDM Pump, Lo-Fi Vintage, Telephone/Radio, Flat Reset).
The last-used preset is remembered on reopen.

---

## Build instructions

### Requirements
- CMake ≥3.22, C++17, Git
- **Linux**: `build-essential cmake libasound2-dev libfreetype6-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libfontconfig1-dev`
- **Windows**: Visual Studio 2022 + CMake, Windows SDK

### Linux
```bash
git clone <repo> SmartEQ-VST3
cd SmartEQ-VST3
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
# Full VST3:  build/SmartEQ_artefacts/Release/VST3/SmartEQ.vst3
# Free VST3:  build/SmartEQFree_artefacts/Release/VST3/SmartEQFree.vst3
# Standalone: build/SmartEQ_artefacts/Release/Standalone/SmartEQ
#             build/SmartEQFree_artefacts/Release/Standalone/SmartEQFree
./install_linux.sh   # installs both into ~/.vst3/ and ~/.local/bin/
```

### Ready-made packages (SmartEQ Free)
```bash
cd SmartEQ-VST3
bash package_free_release.sh   # -> dist/smarteqfree_1.0.0_amd64.deb
                               # -> dist/SmartEQFree-1.0.0-x86_64.AppImage
# Debian/Ubuntu system install (VST3 to /usr/lib/vst3 + standalone):
sudo dpkg -i dist/smarteqfree_1.0.0_amd64.deb
# ...or portable, no install needed:
./dist/SmartEQFree-1.0.0-x86_64.AppImage              # run standalone
./dist/SmartEQFree-1.0.0-x86_64.AppImage --install-vst3  # install VST3 to ~/.vst3
```

### Windows (PowerShell / VS2022)
```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# install_windows.bat copies both bundles into %COMMONPROGRAMFILES%\VST3\
```

---

## Technical layout

```
Source/
 ├─ DSP/
 │   ├─ EQBand.h              Biquad RBJ (10 types) + EQBand (1-2 stages)
 │   ├─ EQProcessor.{h,cpp}   8/16-band cascade (SMARTEQ_FREE_VERSION), response curve
 │   ├─ IntelligentAnalyzer   FFT 4096 + 6 detectors (Full only at runtime)
 │   ├─ SongAnalyzer          Full-song bar-by-bar EQ map: learn + follow (Full)
 │   └─ SpectrumAnalyzer      FFT 2048 real-time UI (Full only at runtime)
 ├─ Presets/
 │   └─ PresetManager         38 English presets
 ├─ UI/
 │   ├─ SpectrumComponent     Full-panel paint, grid, drag (Full only at runtime)
 │   └─ ModernLookAndFeel     Glow faders, arc knobs, pill switches
 ├─ PluginProcessor           APVTS bands×gain/freq/Q/enabled/type + song params, demo clock (Full)
 └─ PluginEditor              Analyzer + song learn/follow controls + demo bar (Full)
CMakeLists.txt                JUCE 7.0.12, two targets: SmartEQ + SmartEQFree
```

---

## License

JUCE 7.0.12 (GPLv3 / personal). SmartEQ DSP + branding by Fear Escape.
Commercial JUCE use requires a JUCE license.

Created by Fear Escape — 2026.
