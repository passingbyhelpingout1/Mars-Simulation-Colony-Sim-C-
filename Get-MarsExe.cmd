@echo off
setlocal enabledelayedexpansion

rem Configure with Visual Studio generator
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 || exit /b 1

rem Parallel build (CMake will pass /m to MSBuild)
cmake --build build --config Release --parallel 2 || exit /b 1

if exist build\Release\mars_colony.exe copy /Y build\Release\mars_colony.exe . >nul
echo Built: build\Release\mars_colony.exe
