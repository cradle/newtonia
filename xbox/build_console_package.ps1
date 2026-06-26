<#
.SYNOPSIS
  Build + package the Xbox Series (Scarlett) GLon12 console build of Newtonia.

.DESCRIPTION
  The decided console path (xbox/PORT_PLAN.md Option A): the desktop-GL renderer
  built for Scarlett via Ninja + the NDA GXDK, run through Mesa's GLon12
  (OpenGL-on-D3D12). Produces an .xvc package via makepkg that can be installed
  on a dev kit or a retail console in Developer Mode / your Partner Center
  sandbox.

  Run this on a Windows machine that has: Visual Studio 2022 Build Tools (C++),
  CMake + Ninja, and the GDK *with Xbox extensions* (GXDK) installed. It is the
  hand-run equivalent of .github/workflows/disabled/deploy-xbox.yml's build job.

  NOTE: this cannot run on this repo's CI (public, GXDK is NDA) or on Linux.
  It is GXDK-gated infrastructure — the renderer/present path is the same code
  the GDK Desktop build and xbox-console-smoke.yml already exercise, but the
  Scarlett link (xgameplatform.lib, the Xbox GLon12 build) needs the GXDK and a
  dev kit to finalise. Several paths below are marked TODO accordingly.

.PARAMETER Glon12Dir
  Directory containing the Xbox GLon12 redist: opengl32.dll, libgallium_wgl.dll
  (and dxil.dll if required) plus the opengl32.lib import lib. Defaults to
  $env:NEWTONIA_GLON12_DIR.

.PARAMETER Configuration
  Release (default) or Debug.

.EXAMPLE
  $env:XBOX_IDENTITY_NAME='...'; $env:XBOX_IDENTITY_PUBLISHER='CN=...'
  $env:XBOX_PUBLISHER_DISPLAY_NAME='...'; $env:XBOX_STORE_ID='............'
  ./xbox/build_console_package.ps1 -Glon12Dir C:\glon12-xbox
#>
[CmdletBinding()]
param(
    [string]$Glon12Dir    = $env:NEWTONIA_GLON12_DIR,
    [ValidateSet('Release','Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $repo

# --- 1. Identity secrets (kept out of source; same tokens as deploy-xbox.yml) --
$idVars = 'XBOX_IDENTITY_NAME','XBOX_IDENTITY_PUBLISHER',
          'XBOX_PUBLISHER_DISPLAY_NAME','XBOX_STORE_ID'
$missing = $idVars | Where-Object { -not [Environment]::GetEnvironmentVariable($_) }
if ($missing) {
    throw "Missing identity env var(s): $($missing -join ', '). See xbox/PARTNER_CENTER_VALUES.md."
}

# --- 2. Locate GXDK ----------------------------------------------------------
$gdkLatest = [Environment]::GetEnvironmentVariable('GameDKLatest','Machine')
if (-not $gdkLatest) {
    $base = 'C:\Program Files (x86)\Microsoft GDK'
    $found = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -match '^\d+$' } |
               Sort-Object Name -Descending | Select-Object -First 1
    if ($found) { $gdkLatest = $found.FullName }
}
if (-not $gdkLatest) {
    throw "GXDK not found. Install the GDK with Xbox extensions (GXDK)."
}
$gdkLatest = $gdkLatest.TrimEnd('\')
$gdkEdition = Split-Path $gdkLatest -Leaf
Write-Host "GXDK edition $gdkEdition at $gdkLatest"

# GXDK header / lib locations. TODO(GXDK): confirm these against the installed
# layout on the dev machine — the exact subpaths vary by GDK edition.
$grdkInclude  = Join-Path $gdkLatest 'GRDK\GameKit\Include'
$gxdkInclude  = Join-Path $gdkLatest 'GXDK\GameKit\Include'
$gxdkLibDir   = Join-Path $gdkLatest 'GXDK\GameKit\Lib\amd64\Scarlett'
$makepkg      = Join-Path $gdkLatest 'GRDK\bin\makepkg.exe'

if (-not $Glon12Dir) {
    throw "Set -Glon12Dir (or NEWTONIA_GLON12_DIR) to the Xbox GLon12 redist dir " +
          "(opengl32.dll/.lib, libgallium_wgl.dll). Building Mesa's d3d12 driver " +
          "for the Xbox cross target is GXDK-gated — see xbox/PORT_PLAN.md Phase 2."
}
$glon12Lib = Join-Path $Glon12Dir 'opengl32.lib'

# --- 3. Configure + build (Ninja, Scarlett GLon12) ---------------------------
$buildDir = 'xbox\build-console'
$vcvars = 'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    $vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
}

$cmakeArgs = @(
    '-G','Ninja','-B',$buildDir,'-S','xbox','-DXBOX_SCARLETT=ON',
    "-DCMAKE_BUILD_TYPE=$Configuration",
    '-DCMAKE_C_COMPILER=cl','-DCMAKE_CXX_COMPILER=cl',
    "-DGDK_INCLUDE_DIR=$grdkInclude",
    "-DGXDK_INCLUDE_DIR=$gxdkInclude",
    "-DGXDK_LIB_DIR=$gxdkLibDir",
    "-DGLON12_LIB=$glon12Lib"
) -join ' '

# Run configure + build inside a vcvars64 shell.
cmd /c "`"$vcvars`" && cmake $cmakeArgs && cmake --build $buildDir"
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }

$exe = Join-Path $buildDir 'newtonia.exe'
if (-not (Test-Path $exe)) { throw "Build produced no $exe." }

# --- 4. Stage the makepkg layout --------------------------------------------
$out = 'xbox_output'
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$out\bin" | Out-Null

Copy-Item $exe "$out\bin\"

# GLon12 redist DLLs (decided renderer). dxil.dll is optional depending on the
# Mesa build.
foreach ($dll in 'opengl32.dll','libgallium_wgl.dll','dxil.dll') {
    $src = Join-Path $Glon12Dir $dll
    if (Test-Path $src) { Copy-Item $src "$out\bin\" }
    elseif ($dll -ne 'dxil.dll') { Write-Warning "GLon12 DLL missing: $src" }
}

Copy-Item -Recurse audio "$out\bin\audio"

# Identity injected from env into the committed __FILL_* tokens — nothing
# deployment-specific lives in source (same pattern as Steam's app_build.vdf).
$cfg = Get-Content xbox\MicrosoftGame.config -Raw
$cfg = $cfg.Replace('__FILL_IDENTITY_NAME__',          $env:XBOX_IDENTITY_NAME)
$cfg = $cfg.Replace('__FILL_IDENTITY_PUBLISHER__',     $env:XBOX_IDENTITY_PUBLISHER)
$cfg = $cfg.Replace('__FILL_PUBLISHER_DISPLAY_NAME__', $env:XBOX_PUBLISHER_DISPLAY_NAME)
$cfg = $cfg.Replace('__FILL_STORE_ID__',               $env:XBOX_STORE_ID)
if ($cfg -match '__FILL_') { throw "Unsubstituted __FILL_ token in MicrosoftGame.config." }
Set-Content "$out\MicrosoftGame.config" $cfg -Encoding utf8

if (Test-Path xbox\Assets) { Copy-Item -Recurse xbox\Assets "$out\Assets" }
else { Write-Warning "xbox\Assets missing — run xbox/generate_assets.py before submitting." }

# --- 5. Package with makepkg -------------------------------------------------
if (-not (Test-Path $makepkg)) { throw "makepkg not found at $makepkg." }
New-Item -ItemType Directory -Force -Path packages | Out-Null
& $makepkg pack /f xbox\PackagingLayout.xml /d $out /pd packages /pc
if ($LASTEXITCODE -ne 0) { throw "makepkg failed ($LASTEXITCODE)." }

Write-Host "`nConsole package(s):"
Get-ChildItem packages\*.xvc | ForEach-Object { Write-Host "  $($_.FullName)" }
Write-Host "Install on a dev kit / Dev Mode console with: xbapp install <pkg>.xvc"
