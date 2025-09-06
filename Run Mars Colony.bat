@echo off
setlocal
pushd "%~dp0"

if not exist MarsColony.exe (
  echo Building Mars Colony...
  g++ -std=c++17 -O2 -Wall -Wextra -o MarsColony.exe mars_colony.cpp || (
    echo Build failed. Make sure MinGW-w64 (g++) is installed and on PATH.
    pause
    exit /b 1
  )
)

echo.
echo Launching Mars Colony...
echo.
MarsColony.exe
echo.
pause
