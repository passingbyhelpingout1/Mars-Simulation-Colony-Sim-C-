<#  Builds mars.exe using the official Zig toolchain (Windows x64).
    Place next to mars_colony.cpp and run via Get-MarsExe.cmd (below).

    What it does:
      • Downloads Zig 0.15.1 from ziglang.org (official).
      • Extracts it locally (no system install).
      • Compiles mars_colony.cpp -> mars.exe (tries static, then dynamic).
      • Runs mars.exe with --no-pause (the CMD handles pausing).
#>

param(
  [string]$Cpp = "mars_colony.cpp",
  [string]$Out = "mars.exe",
  [string]$ZigVersion = "0.15.1"     # change if you want a different Zig version
)

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Check source file
if (!(Test-Path -LiteralPath $Cpp)) {
  Write-Error "Could not find '$Cpp' in '$((Get-Location).Path)'. Put this script next to your C++ file."
}

# Download Zig (official) if missing
$zipName   = "zig-x86_64-windows-$ZigVersion.zip"
$zigUrl    = "https://ziglang.org/download/$ZigVersion/$zipName"
$zipPath   = Join-Path $PWD $zipName
$extractTo = Join-Path $PWD "zig-$ZigVersion"

if (!(Test-Path -LiteralPath $zipPath)) {
  Write-Host "Downloading Zig $ZigVersion..." -ForegroundColor Cyan
  Invoke-WebRequest -UseBasicParsing -Uri $zigUrl -OutFile $zipPath
}

if (Test-Path -LiteralPath $extractTo) { Remove-Item -Recurse -Force $extractTo }
Write-Host "Extracting Zig..." -ForegroundColor Cyan
Expand-Archive -Path $zipPath -DestinationPath $extractTo -Force

# Find zig.exe
$zigFolder = Get-ChildItem -LiteralPath $extractTo -Directory |
             Where-Object { $_.Name -like "zig*windows*$ZigVersion*" } |
             Select-Object -First 1
if (-not $zigFolder) { throw "Could not locate extracted Zig folder under '$extractTo'." }
$zigEXE = Join-Path $zigFolder.FullName "zig.exe"
if (!(Test-Path -LiteralPath $zigEXE)) { throw "zig.exe not found at '$zigEXE'." }

# Build (try static first, then fall back)
$buildLog = Join-Path $PWD "build.log"
function Invoke-Build($argsArray) {
  Write-Host "zig $($argsArray -join ' ')" -ForegroundColor DarkGray
  & $zigEXE @argsArray 2>&1 | Tee-Object -FilePath $buildLog
  return $LASTEXITCODE
}

Write-Host "Compiling $Cpp -> $Out (static)..." -ForegroundColor Green
$exit = Invoke-Build @("c++","-std=c++17","-O2","-s","-static","-o",$Out,$Cpp)

if ($exit -ne 0 -or !(Test-Path -LiteralPath $Out)) {
  Write-Warning "Static link failed; retrying without -static (see build.log for details)."
  $exit = Invoke-Build @("c++","-std=c++17","-O2","-s","-o",$Out,$Cpp)
  if ($exit -ne 0 -or !(Test-Path -LiteralPath $Out)) {
    throw "Build failed. See 'build.log' for the full compiler/linker output."
  }
}

Write-Host "Built $Out successfully." -ForegroundColor Green
Write-Host ""
Write-Host "Launching $Out..." -ForegroundColor Cyan
& ".\${Out}" "--no-pause"
