@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title ArdaBestClient Missing DLL Fix
cls
echo ============================================================
echo   ArdaBestClient Missing DLL Fix
echo ============================================================
echo.
echo This fixes errors like:
echo   libstdc++-6.dll was not found
echo   libgcc_s_seh-1.dll was not found
echo   libwinpthread-1.dll was not found
echo.

if not exist "package-windows\ardabest_client.exe" (
  echo ERROR: package-windows\ardabest_client.exe was not found.
  echo Run START-HERE-WINDOWS.bat first, then run this again.
  goto endfail
)

set "BIN="
for %%D in (
  "C:\msys64\ucrt64\bin"
  "%LOCALAPPDATA%\Programs\msys2\ucrt64\bin"
  "%ProgramFiles%\MSYS2\ucrt64\bin"
  "C:\msys64\mingw64\bin"
  "%LOCALAPPDATA%\Programs\msys2\mingw64\bin"
  "%ProgramFiles%\MSYS2\mingw64\bin"
) do (
  if exist "%%~fD\libstdc++-6.dll" (
    set "BIN=%%~fD"
    goto foundbin
  )
)

echo ERROR: I could not find MSYS2 runtime DLLs.
echo Run START-HERE-WINDOWS.bat again, or install MSYS2.
goto endfail

:foundbin
echo Found runtime folder:
echo   !BIN!
echo.
echo Copying needed DLLs into package-windows...
for %%F in (
  libstdc++-6.dll
  libgcc_s_seh-1.dll
  libwinpthread-1.dll
  libzstd.dll
  zlib1.dll
  libdouble-conversion.dll
  libb2-1.dll
  libpcre2-16-0.dll
  libbrotlicommon.dll
  libbrotlidec.dll
  libbz2-1.dll
  libfreetype-6.dll
  libglib-2.0-0.dll
  libgraphite2.dll
  libharfbuzz-0.dll
  libiconv-2.dll
  libintl-8.dll
  libmd4c.dll
  libpng16-16.dll
) do (
  if exist "!BIN!\%%F" (
    copy /Y "!BIN!\%%F" "package-windows\%%F" >nul
    echo   copied %%F
  )
)

echo.
echo Creating safe launcher...
(
  echo @echo off
  echo cd /d "%%~dp0"
  echo set "PATH=%%~dp0;%%PATH%%"
  echo start "" "%%~dp0ardabest_client.exe"
) > "package-windows\RUN-ARDABEST-CLIENT.bat"

echo.
echo ============================================================
echo FIX DONE.
echo Now run:
echo   package-windows\RUN-ARDABEST-CLIENT.bat
echo ============================================================
goto endok

:endfail
echo.
echo FAILED. Send me a screenshot of this window.
:endok
echo.
pause
