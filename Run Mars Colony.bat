@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Prefer PowerShell launcher for robustness
if exist ".\Launch-MarsColony.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File ".\Launch-MarsColony.ps1" -- %*
  exit /b %ERRORLEVEL%
)

rem Fallback to your existing flow if PS launcher is missing
echo [WARN] Launch-MarsColony.ps1 not found. Falling back...
if exist ".\Get-MarsExe.ps1" (
  powershell -NoProfile -ExecutionPolicy Bypass -File ".\Get-MarsExe.ps1"
) else if exist ".\Get-MarsExe.cmd" (
  call ".\Get-MarsExe.cmd"
)

rem Try to find a game exe in .\bin
set "BIN=%cd%\bin"
if exist "%BIN%" (
  for /f "delims=" %%F in ('dir /b /a:-d /o:-d "%BIN%\*.exe" 2^>nul') do (
     set "GAME=%%F"
     goto :found
  )
)

echo [ERROR] Could not find the game executable in ".\bin".
pause
exit /b 1

:found
echo [INFO] Launching "%BIN%\%GAME%" %*
pushd "%BIN%"
"%BIN%\%GAME%" %*
set "CODE=%ERRORLEVEL%"
popd
exit /b %CODE%
