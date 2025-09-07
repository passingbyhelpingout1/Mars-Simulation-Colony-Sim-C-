# Get-MarsExe.ps1
$ErrorActionPreference = "Stop"

# Configure + build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Copy Windows artifact next to the script if present
$exe = Join-Path -Path "build\Release" -ChildPath "mars_colony.exe"
if (Test-Path $exe) {
    Copy-Item $exe -Destination ".\mars_colony.exe" -Force
}
Write-Host "Done."
