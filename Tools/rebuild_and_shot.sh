#!/usr/bin/env bash
# Rebuilds the C++ module, regenerates the UE assets, runs the game and saves Saved/PixelShot.png.
# Usage: Tools/rebuild_and_shot.sh [--no-assets] [extra game args, e.g. -PixelWalk]
set -u
UE="/c/Program Files/Epic Games/UE_5.8/Engine"
PROJ="C:/EpicGames/PixelRPG/PixelRPG.uproject"
cd "$(dirname "$0")/.."

ASSETS=1
if [ "${1:-}" = "--no-assets" ]; then ASSETS=0; shift; fi

"$UE/Build/BatchFiles/Build.bat" PixelRPGEditor Win64 Development -Project="$PROJ" -WaitMutex 2>&1 | grep -E "error|Result"

if [ $ASSETS -eq 1 ]; then
  "$UE/Binaries/Win64/UnrealEditor-Cmd.exe" "$PROJ" -ExecutePythonScript="C:/EpicGames/PixelRPG/Tools/setup_pixel_assets.py" -unattended -nosplash -nopause > /dev/null 2>&1
  grep -E "PixelRPG: asset|Traceback|LogPython: Error|error X[0-9]" Saved/Logs/PixelRPG.log | cut -c1-240 | tail -8
  rm -f Content/Maps/_PixelTestTmp.umap
fi

rm -f Saved/PixelShot.png Saved/PixelRT.png
"$UE/Binaries/Win64/UnrealEditor.exe" "$PROJ" -game -windowed -ResX=1280 -ResY=720 -PixelShot=${SHOT:-20} -unattended -nosplash "$@"
grep -E "PixelShot:|PixelQuest:|PixelSave:|PixelWorld:" Saved/Logs/PixelRPG.log | cut -c1-200
ls Saved/PixelShot.png Saved/PixelRT.png
