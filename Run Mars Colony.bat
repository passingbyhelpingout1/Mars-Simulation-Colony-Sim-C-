@echo off
setlocal EnableExtensions

REM Always work from the script folder (no "cd /d" needed)
pushd "%~dp0"

set "BUILD_ROOT=build"
set "OUT_MSVC=%BUILD_ROOT%\msvc"
set "OUT_CLANG=%BUILD_ROOT%\clang"
set "OUT_MINGW=%BUILD_ROOT%\mingw"
set "OUT_ZIG=%BUILD_ROOT%\zig"
set "EXE_NAME=mars.exe"

REM Common include paths (adjust if you move folders)
set "INCLUDES=-I. -Isrc -Iengine -Iui -Iui\cli"
set "CXXSTD=-std=c++17"
set "OPT=-O2"

:menu
echo.
echo ================================================
echo   Mars Colony - Build and Run
echo ================================================
echo   1) MSVC (cl)
echo   2) LLVM Clang (clang++)
echo   3) MinGW-w64 (g++)
echo   4) Zig (zig c++)
echo   A) Auto (try MSVC, Clang, Zig, MinGW)
echo   R) Run existing EXE if present
echo   C) Clean build folders
echo   X) Exit
echo -----------------------------------------------
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
echo Invalid choice. Try again.
goto menu

REM ---- Helper: collect all .cpp files into a response file (one per line, quoted) ----
:collect_sources
set "RSP=%~1"
> "%RSP%" (
  for /r %%F in (*.cpp) do echo "%%~fF"
)
exit /b 0

REM ---- Helper: run exe or show error ----
:run_or_error
set "EXE=%~1"
if not exist "%EXE%" (
  echo.
  echo ERROR: File not found: "%EXE%"
  exit /b 1
)
echo.
echo ==== Running "%EXE%" ====
echo.
"%EXE%"
exit /b 0

REM ---- Tool discovery ----
:find_msvc
set "CL_EXE="
for %%I in (cl.exe) do set "CL_EXE=%%~$PATH:I"
if defined CL_EXE exit /b 0
if defined VSINSTALLDIR (
  for %%I in ("%VSINSTALLDIR%\VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe") do (
    if exist "%%~fI" set "CL_EXE=%%~fI"
  )
)
if defined CL_EXE exit /b 0
exit /b 1

:find_clang
set "CLANGXX="
for %%I in (clang++.exe) do set "CLANGXX=%%~$PATH:I"
if defined CLANGXX exit /b 0
exit /b 1

:find_mingw
set "GXX="
for %%I in (g++.exe) do set "GXX=%%~$PATH:I"
if defined GXX exit /b 0
exit /b 1

:find_zig
set "ZIG_EXE="
for %%I in (zig.exe) do set "ZIG_EXE=%%~$PATH:I"
if not defined ZIG_EXE (
  for %%I in ("%ProgramFiles%\Zig\zig.exe" "%LocalAppData%\Programs\Zig\zig.exe" "C:\tools\zig\zig.exe") do (
    if exist "%%~fI" set "ZIG_EXE=%%~fI"
  )
)
if not defined ZIG_EXE exit /b 1
"%ZIG_EXE%" version >nul 2>&1 || (set "ZIG_EXE=" & exit /b 1)
exit /b 0

REM ---- MSVC ----
:build_msvc
call :find_msvc || (echo MSVC not found. Install VS 2022 Build Tools with C++ workload.& goto menu)
mkdir "%OUT_MSVC%" 2>nul
call :collect_sources "%OUT_MSVC%\files.rsp"
pushd "%OUT_MSVC%"
cl /nologo /EHsc /std:c++17 /O2 /W4 %INCLUDES% @"files.rsp" /Fe:"%EXE_NAME%"
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (echo Build failed.& goto menu)
call :run_or_error "%OUT_MSVC%\%EXE_NAME%"
goto menu

REM ---- Clang ----
:build_clang
call :find_clang || (echo Clang not found. Install LLVM (Clang) and add to PATH.& goto menu)
mkdir "%OUT_CLANG%" 2>nul
call :collect_sources "%OUT_CLANG%\files.rsp"
"%CLANGXX%" %CXXSTD% %OPT% %INCLUDES% @"%OUT_CLANG%\files.rsp" -o "%OUT_CLANG%\%EXE_NAME%"
if errorlevel 1 (echo Build failed.& goto menu)
call :run_or_error "%OUT_CLANG%\%EXE_NAME%"
goto menu

REM ---- MinGW ----
:build_mingw
call :find_mingw || (echo MinGW g++ not found. Install MSYS2 and add mingw64\bin to PATH.& goto menu)
mkdir "%OUT_MINGW%" 2>nul
call :collect_sources "%OUT_MINGW%\files.rsp"
"%GXX%" %CXXSTD% %OPT% %INCLUDES% @"%OUT_MINGW%\files.rsp" -o "%OUT_MINGW%\%EXE_NAME%"
if errorlevel 1 (echo Build failed.& goto menu)
call :run_or_error "%OUT_MINGW%\%EXE_NAME%"
goto menu

REM ---- Zig (Option 4) ----
:build_zig
call :find_zig || (echo Zig not found. Install Zig and ensure zig.exe is on PATH.& goto menu)
mkdir "%OUT_ZIG%" 2>nul
call :collect_sources "%OUT_ZIG%\files.rsp"
set "LOG=%OUT_ZIG%\build-zig.log"
del "%LOG%" 2>nul

"%ZIG_EXE%" c++ %CXXSTD% %OPT% %INCLUDES% @"%OUT_ZIG%\files.rsp" -o "%OUT_ZIG%\%EXE_NAME%" >"%LOG%" 2>&1
if errorlevel 1 (
  echo First attempt failed; retrying with -target x86_64-windows-gnu ...
  "%ZIG_EXE%" c++ -target x86_64-windows-gnu %CXXSTD% %OPT% %INCLUDES% @"%OUT_ZIG%\files.rsp" -o "%OUT_ZIG%\%EXE_NAME%" >>"%LOG%" 2>&1
)
if not exist "%OUT_ZIG%\%EXE_NAME%" (
  echo Build failed. See log:
  type "%LOG%"
  goto menu
)
call :run_or_error "%OUT_ZIG%\%EXE_NAME%"
goto menu

REM ---- Auto ----
:build_auto
echo Trying MSVC, Clang, Zig, MinGW in that order ...
call :find_msvc  && (goto build_msvc)
call :find_clang && (goto build_clang)
call :find_zig   && (goto build_zig)
call :find_mingw && (goto build_mingw)
echo No C++ compiler found. Install one and try again.
goto menu

REM ---- Run existing ----
:run_existing
for %%D in ("%OUT_MSVC%" "%OUT_CLANG%" "%OUT_MINGW%" "%OUT_ZIG%") do (
  if exist "%%~fD\%EXE_NAME%" (
    call :run_or_error "%%~fD\%EXE_NAME%"
    goto menu
  )
)
if exist "%EXE_NAME%" (
  call :run_or_error "%EXE_NAME%"
  goto menu
)
echo No built EXE found. Build first.
goto menu

REM ---- Clean ----
:clean
rmdir /s /q "%BUILD_ROOT%" 2>nul
echo Clean done.
goto menu

:end
popd
endlocal
