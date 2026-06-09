#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BUILD="$ROOT/build-windows-msys2"
PACKAGE="$ROOT/package-windows"

echo "Project: $ROOT"
echo "Using cmake: $(command -v cmake)"
echo "Using ninja: $(command -v ninja)"
echo "Using g++:   $(command -v g++)"
echo "Using qmake: $(command -v qmake6 || true)"

rm -rf "$BUILD" "$PACKAGE"
mkdir -p "$BUILD" "$PACKAGE"

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel

for exe in ardabest_client.exe ardabest_client_safe.exe; do
  if [[ ! -f "$BUILD/$exe" ]]; then
    echo "ERROR: Build finished but $BUILD/$exe does not exist."
    exit 1
  fi
  cp "$BUILD/$exe" "$PACKAGE/"
done

if command -v windeployqt6 >/dev/null 2>&1; then
  echo "Deploying Qt runtime with windeployqt6..."
  (cd "$PACKAGE" && windeployqt6 ardabest_client.exe && windeployqt6 ardabest_client_safe.exe)
elif command -v windeployqt >/dev/null 2>&1; then
  echo "Deploying Qt runtime with windeployqt..."
  (cd "$PACKAGE" && windeployqt ardabest_client.exe && windeployqt ardabest_client_safe.exe)
else
  echo "WARNING: windeployqt was not found."
fi

echo "Copying MinGW/MSYS2 runtime DLLs..."
copy_if_exists() { local src="$1"; if [[ -f "$src" ]]; then cp -u "$src" "$PACKAGE/"; echo "  copied $(basename "$src")"; fi; }
for dll in /ucrt64/bin/*.dll; do
  case "$(basename "$dll")" in
    libstdc++-6.dll|libgcc_s_seh-1.dll|libwinpthread-1.dll|libzstd.dll|zlib1.dll|libdouble-conversion.dll|libb2-1.dll|libpcre2-16-0.dll|libharfbuzz-0.dll|libfreetype-6.dll|libpng16-16.dll|libbrotli*.dll|libbz2-1.dll|libgraphite2.dll|libglib-2.0-0.dll|libintl-8.dll|libiconv-2.dll|libpcre2-8-0.dll|libmd4c.dll|libicui18n*.dll|libicuuc*.dll|libicudt*.dll|libjpeg-*.dll) copy_if_exists "$dll" ;;
  esac
done

if command -v ldd >/dev/null 2>&1; then
  for round in 1 2 3 4 5 6 7; do
    new_count=0
    while IFS= read -r -d '' binfile; do
      while IFS= read -r dep; do
        dep="${dep//$'\r'/}"; [[ -z "$dep" ]] && continue
        dep_base="$(basename "$dep")"; [[ -f "$PACKAGE/$dep_base" ]] && continue
        if [[ -f "$dep" ]]; then cp -u "$dep" "$PACKAGE/" && new_count=$((new_count+1)) || true; echo "  copied dependency $dep_base"; fi
      done < <(ldd "$binfile" 2>/dev/null | awk '/=> \/ucrt64\/bin\// {print $3} /^\/[a-zA-Z0-9_ -]*\/ucrt64\/bin\// {print $1}')
    done < <(find "$PACKAGE" -type f \( -iname "*.exe" -o -iname "*.dll" \) -print0)
    [[ "$new_count" -eq 0 ]] && break
  done
fi

cat > "$PACKAGE/RUN-ARDABEST-CLIENT.bat" <<'BAT'
@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title ArdaBestClient Safe Launcher
set "PATH=%~dp0;%PATH%"
set "QT_OPENGL=software"
set "QT_QUICK_BACKEND=software"
set "QT_AUTO_SCREEN_SCALE_FACTOR=0"
set "QT_ENABLE_HIGHDPI_SCALING=0"
set "QT_LOGGING_RULES=*.debug=false"
echo Starting full ArdaBestClient...
"%~dp0ardabest_client.exe"
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" exit /b 0
echo.
echo Full client closed/crashed with code %RC%.
echo Starting NUCLEAR SAFE fallback client now...
echo.
set "ARDABEST_SAFE_MODE=1"
set "ARDABEST_NO_BACKGROUND=1"
"%~dp0ardabest_client_safe.exe"
set "RC2=%ERRORLEVEL%"
echo.
echo Safe client closed with code %RC2%.
echo If both clients failed, send ardabest_startup_log.txt from this folder.
pause
exit /b %RC2%
BAT

cat > "$PACKAGE/RUN-SAFE-MODE-ONLY.bat" <<'BAT'
@echo off
cd /d "%~dp0"
set "PATH=%~dp0;%PATH%"
set "QT_OPENGL=software"
set "QT_QUICK_BACKEND=software"
set "ARDABEST_SAFE_MODE=1"
set "ARDABEST_NO_BACKGROUND=1"
"%~dp0ardabest_client_safe.exe"
pause
BAT

cat > "$PACKAGE/RESET-FULL-PROFILE-AND-RUN.bat" <<'BAT'
@echo off
cd /d "%~dp0"
if exist profile ren profile profile_backup_%RANDOM%
call RUN-ARDABEST-CLIENT.bat
BAT

cat > "$PACKAGE/README-RUN.txt" <<'TXT'
Use RUN-ARDABEST-CLIENT.bat.

This v22 package includes two clients:
1) ardabest_client.exe = full client.
2) ardabest_client_safe.exe = nuclear safe fallback client.

RUN-ARDABEST-CLIENT.bat tries the full client first. If it crashes, it automatically opens the safe client.
Use RUN-SAFE-MODE-ONLY.bat if you just want the fallback to open right away.

Keep this whole package-windows folder together. Do not drag only the EXE to the desktop.
TXT

echo
echo "Built package: $PACKAGE"
echo "Run: $PACKAGE/RUN-ARDABEST-CLIENT.bat"
