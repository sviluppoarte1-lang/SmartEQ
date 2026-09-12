#!/bin/bash
# Fear Escape - SmartEQ Free release packaging (.deb + AppImage)
# Run from the SmartEQ-VST3 directory after a Release build.
set -e
cd "$(dirname "$0")"

VERSION="1.0.0"
ARCH="amd64"
NAME="SmartEQFree"
DISPLAY="SmartEQ Free"
DESC="8-band graphic equalizer with 10 professional filter types per band"
VST3_SRC="build/SmartEQFree_artefacts/Release/VST3/SmartEQFree.vst3"
STANDALONE_SRC="build/SmartEQFree_artefacts/Release/Standalone/SmartEQFree"
LOGO="../logo.jpg"
DIST="dist"
PKGDIR="$DIST/deb_${NAME,,}"

for f in "$VST3_SRC/Contents/x86_64-linux/SmartEQFree.so" "$STANDALONE_SRC" "$LOGO"; do
  [ -e "$f" ] || { echo "MISSING: $f (build Release first)"; exit 1; }
done
command -v dpkg-deb >/dev/null || { echo "dpkg-deb missing"; exit 1; }

rm -rf "$DIST"
mkdir -p "$PKGDIR/DEBIAN" \
         "$PKGDIR/usr/lib/vst3" \
         "$PKGDIR/usr/bin" \
         "$PKGDIR/usr/share/applications" \
         "$PKGDIR/usr/share/icons/hicolor/256x256/apps" \
         "$PKGDIR/usr/share/doc/${NAME,,}"

# --- payload ---
cp -r "$VST3_SRC" "$PKGDIR/usr/lib/vst3/"
cp "$STANDALONE_SRC" "$PKGDIR/usr/bin/$NAME"
chmod 755 "$PKGDIR/usr/bin/$NAME"
find "$PKGDIR/usr/lib/vst3" -type f -name "*.so" -exec chmod 755 {} \;

# --- icon (center-cropped square, brand background) ---
convert "$LOGO" -gravity center -crop 720x720+0+0 +repage -resize 256x256 \
  "$PKGDIR/usr/share/icons/hicolor/256x256/apps/${NAME,,}.png"

# --- desktop entry (standalone) ---
cat > "$PKGDIR/usr/share/applications/${NAME,,}.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$DISPLAY
GenericName=Graphic Equalizer
Comment=$DESC by Fear Escape (free)
Exec=$NAME
Icon=${NAME,,}
Categories=AudioVideo;Audio;Music;
Terminal=false
StartupNotify=false
Keywords=EQ;equalizer;VST;audiomixing;mastering;
EOF

# --- docs ---
cp README.md "$PKGDIR/usr/share/doc/${NAME,,}/README" 2>/dev/null || echo "$DISPLAY $VERSION by Fear Escape (free)" > "$PKGDIR/usr/share/doc/${NAME,,}/README"
cat > "$PKGDIR/usr/share/doc/${NAME,,}/copyright" <<EOF
$DISPLAY $VERSION - Copyright (C) 2026 Fear Escape. All rights reserved.
Binary VST3 plugin + standalone application. Free to use, do not redistribute
modified binaries without permission.
Built with JUCE 7 (GPLv3 / ISC dual-licensed components, see JUCE docs).
EOF

# --- control ---
INSTALLED_KB=$(du -sk "$PKGDIR/usr" | cut -f1)
cat > "$PKGDIR/DEBIAN/control" <<EOF
Package: ${NAME,,}
Version: $VERSION
Section: sound
Priority: optional
Architecture: $ARCH
Maintainer: Fear Escape
Installed-Size: $INSTALLED_KB
Depends: libc6, libstdc++6, libgcc-s1, libfreetype6, libx11-6, libxext6, libxcomposite1, libxcursor1, libxinerama1, libxrandr2, libfontconfig1, libasound2
Homepage: https://fearescape.example.com
Description: $DISPLAY - free 8-band graphic equalizer (VST3 + standalone)
 $DESC by Fear Escape.
 .
 Installs the VST3 plugin to /usr/lib/vst3 and the standalone app to /usr/bin.
 Free to use, no license needed.
EOF

fakeroot dpkg-deb --build "$PKGDIR" "$DIST/${NAME,,}_${VERSION}_${ARCH}.deb" 2>/dev/null \
  || dpkg-deb --build "$PKGDIR" "$DIST/${NAME,,}_${VERSION}_${ARCH}.deb"

echo ""
echo "=== AppImage (portable standalone + --install-vst3) ==="
if ! command -v appimagetool >/dev/null; then
  echo "appimagetool missing - skipping AppImage (deb is ready)"
else
  APPDIR="$DIST/AppDir_${NAME}"
  rm -rf "$APPDIR"
  mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/vst3"
  cp "$STANDALONE_SRC" "$APPDIR/usr/bin/$NAME"
  cp -r "$VST3_SRC" "$APPDIR/usr/lib/vst3/"
  cp "$PKGDIR/usr/share/icons/hicolor/256x256/apps/${NAME,,}.png" "$APPDIR/${NAME,,}.png"
  cp "$PKGDIR/usr/share/applications/${NAME,,}.desktop" "$APPDIR/"
  cat > "$APPDIR/AppRun" <<EOF
#!/bin/bash
HERE="\$(dirname "\$(readlink -f "\$0")")"
case "\$1" in
  --install-vst3)
    mkdir -p "\$HOME/.vst3"
    cp -r "\$HERE/usr/lib/vst3/SmartEQFree.vst3" "\$HOME/.vst3/"
    echo "SmartEQ Free VST3 installed to \$HOME/.vst3/ - rescan plugins in your DAW."
    ;;
  --help|-h)
    echo "SmartEQ Free (portable)"
    echo "  Run with no arguments : launch the standalone equalizer"
    echo "  --install-vst3        : install the bundled VST3 into ~/.vst3"
    ;;
  *) exec "\$HERE/usr/bin/$NAME" "\$@" ;;
esac
EOF
  chmod +x "$APPDIR/AppRun"
  (cd "$DIST" && ARCH=x86_64 appimagetool "AppDir_${NAME}" "${NAME}-${VERSION}-x86_64.AppImage")
fi

echo ""
echo "=== dist/ ==="
ls -la "$DIST" | grep -vE "^totale|^d" || ls -la "$DIST"
