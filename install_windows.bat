@echo off
echo === SmartEQ Installer Windows (Fear Escape) ===
echo Full (16 bands + analyzer + song-map, 45-min demo) + Free (8 bands, fully working)

set VST3_PRO=build\SmartEQ_artefacts\Release\VST3\SmartEQ.vst3
set VST3_FREE=build\SmartEQFree_artefacts\Release\VST3\SmartEQFree.vst3
set VST3_DEST=%COMMONPROGRAMFILES%\VST3

if not exist "%VST3_PRO%" if not exist "%VST3_FREE%" (
  echo VST3 non trovato. Esegui prima:
  echo   cmake -B build
  echo   cmake --build build --config Release
  pause
  exit /b 1
)

if not exist "%VST3_DEST%" mkdir "%VST3_DEST%"

if exist "%VST3_PRO%" (
  echo Copia SmartEQ Full...
  xcopy /E /I /Y "%VST3_PRO%" "%VST3_DEST%\SmartEQ.vst3"
)
if exist "%VST3_FREE%" (
  echo Copia SmartEQ Free...
  xcopy /E /I /Y "%VST3_FREE%" "%VST3_DEST%\SmartEQFree.vst3"
)

echo.
echo Full version: 16 bands + analyzer + song-map (45-minute demo per session).
echo Free version: 8 bands, fully working.
echo Buy the Full version: https://www.paypal.com/paypalme/fearescape/19.99
echo Riavvia la DAW e rescansiona i plugin VST3.
pause
