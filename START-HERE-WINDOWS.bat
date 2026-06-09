@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title ArdaBestClient One-Button Windows Setup

cls
echo ============================================================
echo   ArdaBestClient v23 NUCLEAR SAFE Setup + Build
echo ============================================================
echo.
echo This tries to do the boring setup for you:
echo   - install MSYS2 with winget if MSYS2 is missing
echo   - install the C++ compiler, CMake, Ninja, and Qt 6 in MSYS2
echo   - build ArdaBestClient
echo   - copy the finished EXE and Qt DLLs into package-windows
echo.
echo It may ask Windows for permission to install MSYS2.
echo.
choice /C YN /M "Continue"
if errorlevel 2 goto cancelled

echo.
echo [1/5] Finding or installing MSYS2...
call :FindBash
if defined BASH goto have_bash

where winget >nul 2>nul
if errorlevel 1 (
    echo.
    echo ERROR: winget was not found.
    echo Install "App Installer" from the Microsoft Store, then run this again.
    echo Or manually install MSYS2 from https://www.msys2.org/
    goto fail
)

echo MSYS2 not found. Installing MSYS2 with winget...
winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
if errorlevel 1 (
    echo.
    echo ERROR: MSYS2 install failed or was cancelled.
    goto fail
)

call :FindBash
if not defined BASH (
    echo.
    echo ERROR: MSYS2 was installed, but I could not find bash.exe.
    echo Close this window, open it again, and run START-HERE-WINDOWS.bat again.
    goto fail
)

:have_bash
echo Found MSYS2 bash:
echo   %BASH%
echo.

set "MSYSTEM=UCRT64"
set "CHERE_INVOKING=1"
set "MSYS2_PATH_TYPE=inherit"
set "PROJECT_DIR=%CD%"

if not exist "%~dp0tools\msys2-install-and-build.sh" (
    echo ERROR: Missing tools\msys2-install-and-build.sh
    goto fail
)

echo [2/5] Updating MSYS2 package database...
echo This can take several minutes the first time.
echo.
"%BASH%" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; pacman -Sy --noconfirm"
if errorlevel 1 goto fail

echo.
echo [3/5] Installing build tools and Qt 6...
echo This can take a while. Let it finish.
echo.
"%BASH%" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; pacman -S --needed --noconfirm base-devel mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools"
if errorlevel 1 goto fail

echo.
echo [4/5] Building ArdaBestClient...
"%BASH%" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; cd \"$(cygpath -u \"$PROJECT_DIR\")\" && bash tools/msys2-install-and-build.sh"
if errorlevel 1 goto fail

echo.
echo [5/5] Done.
echo.
echo ============================================================
echo SUCCESS!
echo Your finished client should be here:
echo.
echo   %CD%\package-windows\RUN-ARDABEST-CLIENT.bat
echo.
echo You can double-click it, or run:
echo   package-windows\RUN-ARDABEST-CLIENT.bat
echo ============================================================
echo.
choice /C YN /M "Run the client now"
if errorlevel 2 goto done
if exist "%CD%\package-windows\RUN-ARDABEST-CLIENT.bat" call "%CD%\package-windows\RUN-ARDABEST-CLIENT.bat"

goto done

:FindBash
set "BASH="
for %%B in (
    "C:\msys64\usr\bin\bash.exe"
    "%LOCALAPPDATA%\Programs\msys2\usr\bin\bash.exe"
    "%ProgramFiles%\MSYS2\usr\bin\bash.exe"
    "%ProgramFiles(x86)%\MSYS2\usr\bin\bash.exe"
) do (
    if exist "%%~fB" (
        set "BASH=%%~fB"
        exit /b 0
    )
)
exit /b 0

:cancelled
echo Cancelled.
goto done

:fail
echo.
echo ============================================================
echo SETUP OR BUILD FAILED.
echo.
echo Try this:
echo   1. Right-click START-HERE-WINDOWS.bat
echo   2. Choose "Run as administrator"
echo   3. Let MSYS2 install and let pacman finish
echo.
echo If it still fails, send me a screenshot of the FIRST red/error line.
echo ============================================================
echo.
pause
exit /b 1

:done
echo.
pause
exit /b 0
