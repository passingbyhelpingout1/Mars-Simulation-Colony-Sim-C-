# Build helper for Windows
$ErrorActionPreference = "Stop"

# Configure with Visual Studio generator
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Parallel build (CMake maps to MSBuild's /m)
cmake --build build --config Release --parallel 2

# Copy the exe next to this script for convenience
$exe = Join-Path -Path "build\Release" -ChildPath "mars_colony.exe"
if (Test-Path $exe) {
    Copy-Item $exe -Destination ".\mars_colony.exe" -Force
}
Write-Host "Built: $exe"
