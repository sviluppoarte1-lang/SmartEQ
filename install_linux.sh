#!/bin/bash
set -e
echo "=== SmartEQ Installer Linux (Fear Escape) ==="
echo "Full (16 bands + analyzer + song-map, 45-min demo) + Free (8 bands, fully working)"

VST3_PRO="build/SmartEQ_artefacts/Release/VST3/SmartEQ.vst3"
VST3_FREE="build/SmartEQFree_artefacts/Release/VST3/SmartEQFree.vst3"
STANDALONE_PRO="build/SmartEQ_artefacts/Release/Standalone/SmartEQ"
STANDALONE_FREE="build/SmartEQFree_artefacts/Release/Standalone/SmartEQFree"
VST3_DEST="$HOME/.vst3"
BIN_DEST="$HOME/.local/bin"

if [ ! -d "$VST3_PRO" ] && [ ! -d "$VST3_FREE" ]; then
  echo "VST3 non trovato. Esegui prima: cmake -B build && cmake --build build --config Release"
  exit 1
fi

# Remove legacy bundle with spaces from older installs
if [ -d "$VST3_DEST/SmartEQ Intelligent Equalizer.vst3" ]; then
  echo "Rimuovo vecchio bundle..."
  rm -rf "$VST3_DEST/SmartEQ Intelligent Equalizer.vst3"
fi

mkdir -p "$VST3_DEST"
if [ -d "$VST3_PRO" ]; then
  echo "Copia SmartEQ Full in $VST3_DEST..."
  cp -r "$VST3_PRO" "$VST3_DEST/"
  chmod -R +r "$VST3_DEST/SmartEQ.vst3" 2>/dev/null || true
  echo "  ✓ Full: $VST3_DEST/SmartEQ.vst3"
fi
if [ -d "$VST3_FREE" ]; then
  echo "Copia SmartEQ Free in $VST3_DEST..."
  cp -r "$VST3_FREE" "$VST3_DEST/"
  chmod -R +r "$VST3_DEST/SmartEQFree.vst3" 2>/dev/null || true
  echo "  ✓ Free: $VST3_DEST/SmartEQFree.vst3"
fi

mkdir -p "$BIN_DEST"
if [ -f "$STANDALONE_PRO" ]; then
  cp "$STANDALONE_PRO" "$BIN_DEST/SmartEQ"
  chmod +x "$BIN_DEST/SmartEQ"
  echo "  ✓ Standalone Full: $BIN_DEST/SmartEQ"
fi
if [ -f "$STANDALONE_FREE" ]; then
  cp "$STANDALONE_FREE" "$BIN_DEST/SmartEQFree"
  chmod +x "$BIN_DEST/SmartEQFree"
  echo "  ✓ Standalone Free: $BIN_DEST/SmartEQFree"
fi
echo ""
echo "Full version: 16 bands + analyzer + song-map (45-minute demo per session)."
echo "Free version: 8 bands, fully working."
echo "Buy the Full version: https://www.paypal.com/paypalme/fearescape/19.99"
echo "Riavvia la DAW e rescansiona i plugin VST3."
