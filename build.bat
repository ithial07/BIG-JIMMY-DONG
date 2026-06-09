@echo off
setlocal EnableExtensions
cd /d "%~dp0"

if /i "%1"=="windows" goto win
if /i "%1"=="win" goto win
if /i "%1"=="auto" goto win
if "%1"=="" goto win

echo Usage:
echo   build windows
echo   START-HERE-WINDOWS.bat
echo.
pause
exit /b 1

:win
call "%~dp0START-HERE-WINDOWS.bat"
exit /b %ERRORLEVEL%
