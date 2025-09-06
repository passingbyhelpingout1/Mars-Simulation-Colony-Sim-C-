@echo off
setlocal enabledelayedexpansion
pushd "%~dp0"

set SRC=mars_colony.cpp
set EXE=MarsColony.exe

where g++ >nul 2>&1
if errorlevel 1 (
  echo g++ not found. Install MinGW-w64 and ensure g++ is on PATH.
  pause
  exit /b 1
)

echo Building %EXE% (static)...
g++ -std=c++17 -O2 -s -static -static-libstdc++ -static-libgcc -o "%EXE%" "%SRC%" 2>build_static.err
if errorlevel 1 (
  echo Static link failed. Retrying without -static...
  g++ -std=c++17 -O2 -s -o "%EXE%" "%SRC%"
  if errorlevel 1 (
    echo Build failed. See errors above.
    type build_static.err
    pause
    exit /b 1
  )
  echo Note: This build may require MinGW runtime DLLs:
  echo   libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll
  echo If double-clicking the EXE fails, run via this BAT or copy those DLLs next to the EXE.
)

echo.
echo Launching %EXE%...
"%EXE%" --no-pause
echo.
pause
