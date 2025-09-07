@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul
title Mars Colony — Runner
cd /d "%~dp0"

REM ------------------------------------------------------------------------------
REM Configuration
REM ------------------------------------------------------------------------------
set "BUILD_ROOT=build"
set "OUTDIR_MSVC=%BUILD_ROOT%\msvc"
set "OUTDIR_CLANG=%BUILD_ROOT%\clang"
set "OUTDIR_MINGW=%BUILD_ROOT%\mingw"
set "OUTDIR_ZIG=%BUILD_ROOT%\zig"
set "EXE_NAME=mars.exe"

set "INCLUDES=-I. -Isrc -Iengine -Iui -Iui\cli"
set "CXXSTD=-std=c++17"
set "OPTFLAGS_RELEASE=-O2"

REM ------------------------------------------------------------------------------
REM Menu
REM ------------------------------------------------------------------------------
:menu
echo(
echo ===========================================================
echo   Mars Colony — choose a compiler to build and run
echo ===========================================================
echo   1^) MSVC (cl.exe)
echo   2^) LLVM Clang (clang++)
echo   3^) MinGW-w64 (g++)
echo   4^) Zig (zig c^^^+^^^+)
echo   A^) Auto  (try MSVC, Clang, Zig, MinGW)
echo   R^) Run existing %EXE_NAME% if present
echo   C^) Clean build folders
echo   X^) Exit
echo -----------------------------------------------------------
set "choice="
set /p choice="Your choice [1/2/3/4/A/R/C/X]: "
if /i "%choice%"=="1" goto build_msvc
if /i "%choice%"=="2" goto build_clang
if /i "%choice%"=="3" goto build_mingw
if /i "%choice%"=="4" goto build_zig
if /i "%choice%"=="A" goto build_auto
if /i "%choice%"=="R" goto run_existing
if /i "%choice%"=="C" goto clean
if /i "%choice%"=="X" goto end
echo(
echo Invalid choice. Try again.
goto menu

REM ------------------------------------------------------------------------------
REM Utility: collect sources into a response file (handles spaces safely)
REM ------------------------------------------------------------------------------
:collect_sources
set "RSP=%~1"
> "%RSP%" (
  for /r %%F in (*.cpp) do (
    REM Quote each path on its own line for clang/zig response-file parsing
    echo "%%~fF"
  )
)
exit /b 0

REM ------------------------------------------------------------------------------
REM Utility: run the EXE if it exists
REM ------------------------------------------------------------------------------
:run_or_error
set "EXE=%~1"
if not exist "%EXE%" (
  echo(
  echo ERROR: Build produced no "%EXE%".
  exit /b 1
)
echo(
echo ==== Running: "%EXE%" ====
echo(
"%EXE%"
exit /b 0

REM ------------------------------------------------------------------------------
REM Compiler discovery helpers (avoid broken 'where "zig c++"')
REM ------------------------------------------------------------------------------
:find_msvc
set "CL_EXE="
for %%I in (cl.exe) do set "CL_EXE=%%~$PATH:I"
if not defined CL_EXE (
  REM Try common VS DevCmd environment var hint
  if defined VSINSTALLDIR for %%I in ("%VSINSTALLDIR%\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe") do if exist "%%~fI" set "CL_EXE=%%~fI"
)
if defined CL_EXE (exit /b 0) else (exit /b 1)

:find_clang
set "CLANGXX="
for %%I in (clang++.exe) do set "CLANGXX=%%~$PATH:I"
if defined CLANGXX (exit /b 0) else (exit /b 1)

:find_mingw
set "GXX="
for %%I in (g++.exe) do set "GXX=%%~$PATH:I"
if defined GXX (exit /b 0) else (exit /b 1)

:find_zig
set "ZIG_EXE="
for %%I in (zig.exe) do set "ZIG_EXE=%%~$PATH:I"
if not defined ZIG_EXE (
  for %%I in ("%ProgramFiles%\Zig\zig.exe" "%LocalAppData%\Programs\Zig\zig.exe" "C:\tools\zig\zig.exe") do (
    if exist "%%~fI" set "ZIG_EXE=%%~fI"
  )
)
if not defined ZIG_EXE exit /b 1

REM Verify zig works
"%ZIG_EXE%" version >nul 2>&1 || (set "ZIG_EXE=" & exit /b 1)
exit /b 0

REM ------------------------------------------------------------------------------
REM Build: MSVC
REM ------------------------------------------------------------------------------
:build_msvc
call :find_msvc || (echo(
  echo MSVC not found. Install "Visual Studio 2022 Build Tools" with C++ workload.
  goto menu)
)
echo(
echo ==== Building with MSVC (cl) ====
mkdir "%OUTDIR_MSVC%" 2>nul
set "RSP=%OUTDIR_MSVC%\files.rsp"
call :collect_sources "%RSP%"
pushd "%OUTDIR_MSVC%"
REM Use MSVC flags
cl /nologo /EHsc /std:c++17 /O2 /W4 %INCLUDES% @"files.rsp" /Fe:"%EXE_NAME%"
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (echo Build failed. & goto menu)
call :run_or_error "%OUTDIR_MSVC%\%EXE_NAME%"
goto menu

REM ------------------------------------------------------------------------------
REM Build: LLVM Clang
REM ------------------------------------------------------------------------------
:build_clang
call :find_clang || (echo(
  echo Clang not found. Install LLVM (Clang) for Windows and add to PATH.
  goto menu)
)
echo(
echo ==== Building with Clang (clang++) ====
mkdir "%OUTDIR_CLANG%" 2>nul
set "RSP=%OUTDIR_CLANG%\files.rsp"
call :collect_sources "%RSP%"
"%CLANGXX%" %CXXSTD% %OPTFLAGS_RELEASE% %INCLUDES% @"%RSP%" -o "%OUTDIR_CLANG%\%EXE_NAME%"
if errorlevel 1 (echo Build failed. & goto menu)
call :run_or_error "%OUTDIR_CLANG%\%EXE_NAME%"
goto menu

REM ------------------------------------------------------------------------------
REM Build: MinGW-w64
REM ------------------------------------------------------------------------------
:build_mingw
call :find_mingw || (echo(
  echo MinGW-w64 g++ not found. Install MSYS2 and add mingw64\bin to PATH.
  goto menu)
)
echo(
echo ==== Building with MinGW-w64 (g++) ====
mkdir "%OUTDIR_MINGW%" 2>nul
set "RSP=%OUTDIR_MINGW%\files.rsp"
call :collect_sources "%RSP%"
"%GXX%" %CXXSTD% %OPTFLAGS_RELEASE% %INCLUDES% @"%RSP%" -o "%OUTDIR_MINGW%\%EXE_NAME%"
if errorlevel 1 (echo Build failed. & goto menu)
call :run_or_error "%OUTDIR_MINGW%\%EXE_NAME%"
goto menu

REM ------------------------------------------------------------------------------
REM Build: Zig  (FIXED)
REM ------------------------------------------------------------------------------
:build_zig
call :find_zig || (echo(
  echo Zig not found. Install Zig and ensure zig.exe is on PATH.
  echo See: https://ziglang.org/ (Getting Started > Setting up PATH)
  goto menu)
)
echo(
echo ==== Building with Zig (zig c++) ====
mkdir "%OUTDIR_ZIG%" 2>nul
set "RSP=%OUTDIR_ZIG%\files.rsp"
call :collect_sources "%RSP%"

set "LOG=%OUTDIR_ZIG%\build-zig.log"
del "%LOG%" 2>nul

REM First try native target
"%ZIG_EXE%" c++ %CXXSTD% %OPTFLAGS_RELEASE% %INCLUDES% @"%RSP%" -o "%OUTDIR_ZIG%\%EXE_NAME%" >"%LOG%" 2>&1
if errorlevel 1 (
  echo First attempt failed; retrying with -target x86_64-windows-gnu ...
  "%ZIG_EXE%" c++ -target x86_64-windows-gnu %CXXSTD% %OPTFLAGS_RELEASE% %INCLUDES% @"%RSP%" -o "%OUTDIR_ZIG%\%EXE_NAME%" >>"%LOG%" 2>&1
)

if not exist "%OUTDIR_ZIG%\%EXE_NAME%" (
  echo Build failed. See log:
  type "%LOG%"
  goto menu
)

call :run_or_error "%OUTDIR_ZIG%\%EXE_NAME%"
goto menu

REM ------------------------------------------------------------------------------
REM Auto
REM ------------------------------------------------------------------------------
:build_auto
echo(
echo ==== Auto-detecting a working C++ toolchain ====
call :find_msvc  && (echo Using MSVC ...  & goto build_msvc)
call :find_clang && (echo Using Clang ... & goto build_clang)
call :find_zig   && (echo Using Zig ...   & goto build_zig)
call :find_mingw && (echo Using MinGW ... & goto build_mingw)
echo(
echo No C++ compiler found (tried MSVC, Clang, Zig, MinGW).
echo Install one and re-run this BAT.
goto menu

REM ------------------------------------------------------------------------------
REM Run existing
REM ------------------------------------------------------------------------------
:run_existing
for %%D in ("%OUTDIR_MSVC%" "%OUTDIR_CLANG%" "%OUTDIR_MINGW%" "%OUTDIR_ZIG%") do (
  if exist "%%~fD\%EXE_NAME%" (
    call :run_or_error "%%~fD\%EXE_NAME%"
    goto menu
  )
)
if exist "%EXE_NAME%" (
  call :run_or_error "%EXE_NAME%"
  goto menu
)
echo(
echo No built EXE found. Build first.
goto menu

REM ------------------------------------------------------------------------------
REM Clean
REM ------------------------------------------------------------------------------
:clean
echo Deleting "%BUILD_ROOT%" ...
rmdir /s /q "%BUILD_ROOT%" 2>nul
echo Done.
goto menu

:end
endlocal
