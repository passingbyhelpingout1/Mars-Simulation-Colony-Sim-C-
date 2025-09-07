@echo off
setlocal enabledelayedexpansion
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release || exit /b 1
if exist build\Release\mars_colony.exe copy /Y build\Release\mars_colony.exe . >nul
echo Done.
