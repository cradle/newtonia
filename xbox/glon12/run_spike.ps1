<#
.SYNOPSIS
    Build and run the Newtonia GLon12 desktop rendering spike (Phase 2, 3a).

.DESCRIPTION
    Configures + builds glon12_probe, then runs it twice:
      1. baseline  — against the system/hardware GL driver (no Mesa DLLs),
      2. glon12     — with Mesa's desktop GLon12 DLLs copied next to the exe and
                      GALLIUM_DRIVER=d3d12 forcing the OpenGL-on-D3D12 driver.
    Both runs are headless (hidden window) so this works over RDP / in CI.

    Provide -MesaDir pointing at a folder that contains opengl32.dll,
    libgallium_wgl.dll and dxil.dll from a Mesa desktop build (e.g. a
    pal1000/mesa-dist-win release, "x64 desktop" set). See xbox/GLON12_SPIKE.md.

.EXAMPLE
    pwsh xbox/glon12/run_spike.ps1 -MesaDir C:\mesa\x64
#>
param(
    [string]$MesaDir = "",
    [string]$BuildDir = "xbox/glon12/build",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "== Configuring + building glon12_probe ==" -ForegroundColor Cyan
cmake -B $BuildDir -S "$root"
cmake --build $BuildDir --config $Config

# Locate the built exe (single- vs multi-config generators differ).
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter glon12_probe.exe |
       Select-Object -First 1 -ExpandProperty FullName
if (-not $exe) { throw "glon12_probe.exe not found under $BuildDir" }
$exeDir = Split-Path -Parent $exe
Write-Host "Built: $exe"

function Invoke-Probe([string]$label) {
    Write-Host ""
    Write-Host "== Probe run: $label ==" -ForegroundColor Cyan
    Push-Location $exeDir
    try {
        & $exe --hidden --frames 3
        $code = $LASTEXITCODE
    } finally { Pop-Location }
    Write-Host "[$label] exit code: $code"
    return $code
}

# --- Baseline: ensure no stale Mesa DLLs beside the exe ---
foreach ($d in @("opengl32.dll","libgallium_wgl.dll","dxil.dll")) {
    $p = Join-Path $exeDir $d
    if (Test-Path $p) { Remove-Item $p -Force }
}
$baseCode = Invoke-Probe "baseline (system GL)"

# --- GLon12 run ---
$glon12Code = $null
if ($MesaDir -and (Test-Path $MesaDir)) {
    foreach ($d in @("opengl32.dll","libgallium_wgl.dll","dxil.dll")) {
        $src = Join-Path $MesaDir $d
        if (Test-Path $src) { Copy-Item $src (Join-Path $exeDir $d) -Force }
        else { Write-Warning "Mesa DLL not found: $src" }
    }
    $env:GALLIUM_DRIVER = "d3d12"   # force OpenGL-on-D3D12, not llvmpipe
    $env:MESA_LOG_LEVEL = "info"
    $glon12Code = Invoke-Probe "glon12 (D3D12)"
    Remove-Item Env:\GALLIUM_DRIVER -ErrorAction SilentlyContinue
} else {
    Write-Warning "No -MesaDir given (or path missing): skipping the GLon12 run."
    Write-Warning "Download Mesa desktop GLon12 DLLs and pass -MesaDir. See xbox/GLON12_SPIKE.md."
}

Write-Host ""
Write-Host "== Summary ==" -ForegroundColor Green
Write-Host ("baseline exit : {0}" -f $baseCode)
if ($null -ne $glon12Code) { Write-Host ("glon12   exit : {0}" -f $glon12Code) }
Write-Host "Per-run details in $exeDir\glon12_probe.log"

if ($null -ne $glon12Code) { exit $glon12Code } else { exit $baseCode }
