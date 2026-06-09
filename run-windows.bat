@echo off
cd /d "%~dp0"
if exist "package-windows\ardabest_client.exe" (
    start "" "%~dp0package-windows\ardabest_client.exe"
    exit /b 0
)
if exist "build-windows-msys2\ardabest_client.exe" (
    start "" "%~dp0build-windows-msys2\ardabest_client.exe"
    exit /b 0
)
echo The client has not been built yet.
echo Run START-HERE-WINDOWS.bat first.
pause
exit /b 1
