@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "SRC=mars_colony.cpp"
set "EXE=MarsColony.exe"

:: 1) Try MSVC (cl) — use vswhere if available to load vcvarsall
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%I in (`
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  `) do (
    set "VSINSTALL=%%I"
  )
  if defined VSINSTALL (
    call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
  )
)

:: Fallback: common VS 2022 locations if vswhere isn't present
if not defined VSINSTALL (
  for %%P in (
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    "C:\Program Files\Microsoft Visual Studio\2022\Community"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community"
  ) do (
    if exist "%%~P\VC\Auxiliary\Build\vcvarsall.bat" (
      call "%%~P\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
      goto :check_cl
    )
  )
)

:check_cl
where cl >nul 2>&1
if %errorlevel%==0 goto :build_msvc

:: 2) Try Clang (LLVM for Windows)
where clang++ >nul 2>&1
if %errorlevel%==0 goto :build_clang

:: 3) Try Zig (brings its own toolchain)
where zig >nul 2>&1
if %errorlevel%==0 goto :build_zig

:: 4) Try MinGW g++ (MSYS2 typical path) or PATH g++
if exist "C:\msys64\mingw64\bin\g++.exe" (
  set "PATH=C:\msys64\mingw64\bin;%PATH%"
)
where g++ >nul 2>&1
if %errorlevel%==0 goto :build_gpp

echo.
echo No C++ compiler found (tried MSVC, Clang, Zig, MinGW).
echo Install one of the following and re-run this BAT:
echo   1) Visual Studio 2022 Build Tools (C++ workload),
echo   2) LLVM (Clang) for Windows,
echo   3) MSYS2 with MinGW-w64 (add mingw64\bin to PATH),
echo   4) Zig (for 'zig c++').
echo.
pause
exit /b 1

:build_msvc
echo Building with MSVC (cl)...
:: /MT links the static CRT so the EXE runs without the redist on most systems.
cl /nologo /EHsc /std:c++17 /O2 /W4 /MT /Fe:"%EXE%" "%SRC%"
if errorlevel 1 goto :build_failed
goto :run

:build_clang
echo Building with Clang (clang++)...
clang++ -std=c++17 -O2 -Wall -Wextra -o "%EXE%" "%SRC%"
if errorlevel 1 goto :build_failed
goto :run

:build_zig
echo Building with Zig C++ (zig c++)...
zig c++ -std=c++17 -O2 -static -o "%EXE%" "%SRC%"
if errorlevel 1 goto :build_failed
goto :run

:build_gpp
echo Building with MinGW g++...
g++ -std=c++17 -O2 -Wall -Wextra -o "%EXE%" "%SRC%"
if errorlevel 1 goto :build_failed
goto :run

:build_failed
echo.
echo Build failed. See errors above for details.
echo.
pause
exit /b 1

:run
echo.
echo Launching %EXE%...
"%EXE%" --no-pause
echo.
pause
exit /b 0
