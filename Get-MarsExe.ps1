# Get-MarsExe.ps1
$ErrorActionPreference = "Stop"

# You can override these from the command line:
#   .\Get-MarsExe.ps1 -Config Release -UseAsan $false -WarningsAsErrors $true
param(
  [ValidateSet("Debug","Release")][string]$Config = "Release",
  [bool]$UseAsan = $false,
  [bool]$WarningsAsErrors = $false,
  [bool]$UseLto = $true
)

$asan = if ($UseAsan) { "ON" } else { "OFF" }
$wx   = if ($WarningsAsErrors) { "ON" } else { "OFF" }
$lto  = if ($UseLto) { "ON" } else { "OFF" }

cmake -S . -B build -DCMAKE_BUILD_TYPE=$Config `
  -DMARS_ENABLE_ASAN=$asan `
  -DMARS_WARNINGS_AS_ERRORS=$wx `
  -DMARS_ENABLE_LTO=$lto

cmake --build build --config $Config

$exe = Join-Path -Path "build\$Config" -ChildPath "mars.exe"
if (-not (Test-Path $exe)) { $exe = "build\mars.exe" } # non-MSVC generators

if (Test-Path $exe) {
  Copy-Item $exe -Destination ".\mars.exe" -Force
  Write-Host "Built mars.exe ($Config)"
} else {
  throw "mars.exe not found"
}
