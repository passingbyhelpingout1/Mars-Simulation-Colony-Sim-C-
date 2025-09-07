# Launch-MarsColony.ps1
# Robust launcher for Mars Colony (Windows)
# Usage examples:
#   .\Launch-MarsColony.ps1
#   .\Launch-MarsColony.ps1 -Update
#   .\Launch-MarsColony.ps1 -ResetConfig -- -yourGameArg1 -yourGameArg2

param(
    [switch]$Update,
    [switch]$ResetConfig
)

$ErrorActionPreference = 'Stop'

# --- Constants you can tweak ---
$Repo = 'passingbyhelpingout1/Mars-Simulation-Colony-Sim-C-'
$BinDirName = 'bin'              # where the game exe lives
$LogDirName = 'logs'             # where launcher logs go
$ConfigFileCandidates = @('config.json','config.ini') # optional: delete on -ResetConfig

# --- Setup paths & logging ---
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptRoot
$BinDir = Join-Path $ScriptRoot $BinDirName
$LogDir = Join-Path $ScriptRoot $LogDirName
New-Item -ItemType Directory -Force -Path $BinDir, $LogDir | Out-Null

$LogPath = Join-Path $LogDir ("launcher-" + (Get-Date -Format 'yyyyMMdd_HHmmss') + ".log")
Start-Transcript -Path $LogPath -Append | Out-Null

function Write-Info([string]$msg)  { Write-Host "[INFO ] $msg" }
function Write-Warn([string]$msg)  { Write-Warning "$msg" }
function Write-Err ([string]$msg)  { Write-Error "$msg" }

# --- Helper: pick newest exe in bin ---
function Get-LocalGameExe {
    $cands = Get-ChildItem -Path $BinDir -Filter *.exe -File -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending
    if ($cands) { return $cands[0].FullName }
    return $null
}

# --- Helper: call your existing fetchers if present ---
function Try-ExistingGetScript {
    $ps = Join-Path $ScriptRoot 'Get-MarsExe.ps1'
    $cmd = Join-Path $ScriptRoot 'Get-MarsExe.cmd'
    try {
        if (Test-Path $ps) {
            Write-Info "Found Get-MarsExe.ps1; fetching/updating..."
            & powershell -NoProfile -ExecutionPolicy Bypass -File $ps
            return $true
        } elseif (Test-Path $cmd) {
            Write-Info "Found Get-MarsExe.cmd; fetching/updating..."
            & "$cmd"
            return $true
        }
    } catch {
        Write-Warn "Existing fetch script failed: $($_.Exception.Message)"
    }
    return $false
}

# --- Helper: download from GitHub Releases if available ---
function Update-From-GitHubReleases {
    try {
        $api = "https://api.github.com/repos/$Repo/releases/latest"
        $headers = @{ 'User-Agent' = 'MarsColonyLauncher' }
        Write-Info "Querying $api ..."
        $rel = Invoke-RestMethod -Uri $api -Headers $headers

        if (-not $rel.assets) {
            Write-Warn "No assets found in latest Release."
            return $false
        }

        # Prefer .zip, fallback to .exe
        $asset = $rel.assets | Where-Object {
            $_.name -match '\.zip$' -or $_.name -match '\.exe$'
        } | Select-Object -First 1

        if (-not $asset) {
            Write-Warn "No .zip or .exe asset in latest Release."
            return $false
        }

        $tmp = Join-Path $env:TEMP $asset.name
        Write-Info "Downloading $($asset.name) ..."
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tmp -Headers $headers

        # Optional checksum if provided
        $shaAsset = $rel.assets | Where-Object { $_.name -match '\.sha256$' } | Select-Object -First 1
        if ($shaAsset) {
            Write-Info "Verifying checksum..."
            $expected = (Invoke-WebRequest -Uri $shaAsset.browser_download_url -Headers $headers).Content.Trim() -split '\s+' | Select-Object -First 1
            $actual = (Get-FileHash -Path $tmp -Algorithm SHA256).Hash
            if ($expected.ToLowerInvariant() -ne $actual.ToLowerInvariant()) {
                throw "Checksum mismatch for $($asset.name)."
            }
        }

        if ($asset.name -like '*.zip') {
            Write-Info "Expanding archive to $BinDir ..."
            Expand-Archive -Path $tmp -DestinationPath $BinDir -Force
        } else {
            # Place exe under bin with stable name if you prefer
            $target = Join-Path $BinDir $asset.name
            Move-Item -Force -Path $tmp -Destination $target
        }
        return $true
    } catch {
        Write-Warn "GitHub update failed: $($_.Exception.Message)"
        return $false
    }
}

# --- Optional: reset local config ---
if ($ResetConfig) {
    foreach ($c in $ConfigFileCandidates) {
        $p = Join-Path $ScriptRoot $c
        if (Test-Path $p) {
            Write-Info "Removing $c ..."
            Remove-Item -Force $p
        }
    }
}

# --- Ensure we have a binary ---
$exePath = Get-LocalGameExe
if ($Update -or -not $exePath) {
    if (-not (Try-ExistingGetScript)) {
        if (-not (Update-From-GitHubReleases)) {
            Write-Err "Could not obtain a game binary (local/release/fetch)."
            Stop-Transcript | Out-Null
            exit 1
        }
    }
    $exePath = Get-LocalGameExe
}

if (-not (Test-Path $exePath)) {
    Write-Err "Game executable still not found in '$BinDir'."
    Stop-Transcript | Out-Null
    exit 1
}

# --- Single instance check by exe name ---
$exeName = [System.IO.Path]::GetFileName($exePath)
$running = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -ieq ($exeName -replace '\.exe$','') }
if ($running) {
    Write-Warn "$exeName appears to already be running (PID(s): $($running.Id -join ', '))."
    Stop-Transcript | Out-Null
    exit 0
}

# --- Launch & propagate exit code ---
$gameArgs = $args  # pass through any extra args to the game
Write-Info "Launching: `"$exeName`" $($gameArgs -join ' ')"
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exePath
$psi.WorkingDirectory = $BinDir
$psi.Arguments = ($gameArgs -join ' ')
$proc = [System.Diagnostics.Process]::Start($psi)
$proc.WaitForExit()
$code = $proc.ExitCode
Write-Info "Game exited with code $code"
Stop-Transcript | Out-Null
exit $code
