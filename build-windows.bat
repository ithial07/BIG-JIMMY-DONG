@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title ArdaBestClient Windows Builder

echo ============================================================
echo   ArdaBestClient Windows Build
echo ============================================================
echo.
echo This script MUST be run from a Qt-enabled terminal, or from a
 echo normal terminal after Qt, CMake, and a compiler are installed.
echo.

echo [1/5] Checking for CMake...
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    echo CMake was not found. Starting the one-button installer/build instead.
    echo.
    call "%~dp0START-HERE-WINDOWS.bat"
    exit /b %ERRORLEVEL%
)
cmake --version

echo.
echo [2/5] Trying to find Qt 6...
if defined CMAKE_PREFIX_PATH (
    echo Using existing CMAKE_PREFIX_PATH=%CMAKE_PREFIX_PATH%
) else (
    call :FindQt
)
if defined CMAKE_PREFIX_PATH (
    echo Qt hint: %CMAKE_PREFIX_PATH%
) else (
    echo No Qt folder was auto-detected.
    echo If CMake cannot find Qt, open "Qt 6 Command Prompt" and run this again.
)

echo.
echo [3/5] Configuring build folder...
if exist build-windows rmdir /s /q build-windows
set "GENERATOR_TRIED="
where ninja >nul 2>nul
if not errorlevel 1 (
    echo Trying Ninja generator...
    if defined CMAKE_PREFIX_PATH (
        cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%"
    ) else (
        cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release
    )
    if not errorlevel 1 goto configured
    echo Ninja configure failed. Trying default generator...
    if exist build-windows rmdir /s /q build-windows
)

if defined CMAKE_PREFIX_PATH (
    cmake -S . -B build-windows -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%CMAKE_PREFIX_PATH%"
) else (
    cmake -S . -B build-windows -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 goto fail

:configured
echo.
echo [4/5] Building...
cmake --build build-windows --config Release --parallel
if errorlevel 1 goto fail

echo.
echo [5/5] Deploying Qt runtime if windeployqt is available...
set "EXE="
if exist "build-windows\Release\ardabest_client.exe" set "EXE=build-windows\Release\ardabest_client.exe"
if exist "build-windows\ardabest_client.exe" set "EXE=build-windows\ardabest_client.exe"
if not defined EXE (
    echo ERROR: Build finished but I could not find ardabest_client.exe.
    goto fail
)
where windeployqt >nul 2>nul
if not errorlevel 1 (
    windeployqt "%EXE%"
) else (
    echo windeployqt not found. The EXE may still run from a Qt Command Prompt.
)

echo.
echo ============================================================
echo SUCCESS!
echo Built: %EXE%
echo.
echo You can run it now with:
echo   "%CD%\%EXE%"
echo ============================================================
echo.
pause
exit /b 0

:FindQt
for /d %%A in ("C:\Qt\6.*") do (
    for /d %%B in ("%%~fA\msvc*_64" "%%~fA\mingw*_64") do (
        if exist "%%~fB\lib\cmake\Qt6\Qt6Config.cmake" (
            set "CMAKE_PREFIX_PATH=%%~fB"
            exit /b 0
        )
    )
)
exit /b 0

:fail
echo.
echo ============================================================
echo BUILD FAILED.
echo.
echo Most common fix:
echo   1. Run START-HERE-WINDOWS.bat.
echo   2. Or type: build windows
echo   3. If Windows blocks installs, right-click it and choose Run as administrator.
echo.
echo This window is paused so you can read the error above.
echo ============================================================
pause
exit /b 1
