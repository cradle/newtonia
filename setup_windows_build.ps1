# Bootstraps a Windows build environment for Newtonia (MSYS2/MinGW64) and
# builds the game — repeatable and idempotent, safe to re-run any time.
#
#   ./setup_windows_build.ps1                 # toolchain + netplay deps + build + selftest
#   ./setup_windows_build.ps1 -SkipNetplay    # plain (netless) build: make NETPLAY=0
#   ./setup_windows_build.ps1 -SkipBuild      # toolchain/deps only, no game build
#
# Mirrors .github/workflows/windows.yml: MSYS2 MINGW64 gcc + static SDL2/
# SDL2_mixer/freeglut, and a static libdatachannel for the default netplay
# build (built by build_netplay_deps.sh, which has a matching Windows branch).
#
# After setup, day-to-day builds don't need this script: open the
# "MSYS2 MINGW64" shell and run `make -j8` (netplay by default) or
# `make NETPLAY=0 -j8` for a netless binary.

param(
    [switch]$SkipNetplay,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$Msys = 'C:\msys64'
$Bash = "$Msys\usr\bin\bash.exe"
$Repo = ($PSScriptRoot -replace '\\', '/')

function Invoke-Mingw([string]$Cmd) {
    $env:MSYSTEM = 'MINGW64'
    & $Bash -lc $Cmd
    if ($LASTEXITCODE) { throw "MSYS2 command failed (exit $LASTEXITCODE): $Cmd" }
}

# --- MSYS2 itself ----------------------------------------------------------
$fresh = -not (Test-Path $Bash)
if ($fresh) {
    Write-Host '== installing MSYS2 (winget)'
    winget install --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements --silent
    if (-not (Test-Path $Bash)) { throw "MSYS2 install failed: $Bash not found" }
} else {
    Write-Host "== MSYS2 already installed at $Msys"
}

# --- packages ---------------------------------------------------------------
# First -Syu on a fresh install may only stage core packages; run it twice.
Write-Host '== updating MSYS2 packages'
Invoke-Mingw 'pacman -Syu --noconfirm'
if ($fresh) { Invoke-Mingw 'pacman -Syu --noconfirm' }

Write-Host '== installing toolchain packages'
Invoke-Mingw ('pacman -S --noconfirm --needed make git ' +
    'mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf ' +
    'mingw-w64-x86_64-freeglut mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_mixer ' +
    'mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-openssl')

# --- netplay deps (static libdatachannel into ./netplay-libs) ---------------
if (-not $SkipNetplay) {
    if (Test-Path "$Repo/netplay-libs/include/rtc/rtc.h") {
        Write-Host '== netplay deps already present (netplay-libs/) — skipping; delete the dir to force a rebuild'
    } else {
        Write-Host '== building netplay deps (libdatachannel)'
        Invoke-Mingw "cd '$Repo' && ./build_netplay_deps.sh"
    }
}

# --- build + selftest --------------------------------------------------------
if (-not $SkipBuild) {
    if ($SkipNetplay) {
        Write-Host '== building newtonia.exe (netless: NETPLAY=0)'
        Invoke-Mingw "cd '$Repo' && make NETPLAY=0 -j8"
    } else {
        Write-Host '== building newtonia.exe (netplay, the default)'
        Invoke-Mingw "cd '$Repo' && make -j8"
        Write-Host '== netplay loopback selftest'
        Invoke-Mingw "cd '$Repo' && NEWTONIA_NET_SELFTEST=1 SDL_AUDIODRIVER=dummy ./newtonia.exe | tee selftest.log; grep -q 'NET SELFTEST PASS' selftest.log"
    }
}

Write-Host ''
Write-Host 'Done. Day-to-day: open the "MSYS2 MINGW64" shell and run:'
Write-Host '  make -j8              # netplay build (the default)'
Write-Host '  make NETPLAY=0 -j8    # plain netless build'
