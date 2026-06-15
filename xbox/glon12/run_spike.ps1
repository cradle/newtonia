<#
.SYNOPSIS
    Build and run the Newtonia GLon12 desktop rendering spike (Phase 2, 3a).

.DESCRIPTION
    Zero-setup one-liner:  pwsh xbox/glon12/run_spike.ps1

    Builds glon12_probe, downloads Mesa's desktop GLon12 build (OpenGL-on-D3D12)
    if you don't already have it, then runs the probe:
      1. baseline  — the system/hardware GL driver (no Mesa DLLs),
      2. d3d12     — Mesa's GLon12 with GALLIUM_DRIVER=d3d12 (the console path);
                     on a box with a GPU this exercises the real D3D12 backend,
      3. llvmpipe  — fallback if d3d12 can't make a surface (GPU-less / headless),
                     still proving Mesa exposes our GL 3.3 core feature set.
    All runs are headless (hidden window). See xbox/GLON12_SPIKE.md.

.PARAMETER MesaDir
    Skip the download and use Mesa DLLs already on disk (folder containing
    opengl32.dll, libgallium_wgl.dll, dxil.dll — e.g. a pal1000/mesa-dist-win
    "x64 desktop" set).

.PARAMETER MesaVersion
    pal1000/mesa-dist-win release tag to download when -MesaDir is not given.

.EXAMPLE
    pwsh xbox/glon12/run_spike.ps1
.EXAMPLE
    pwsh xbox/glon12/run_spike.ps1 -MesaDir C:\mesa\x64
#>
param(
    [string]$MesaDir = "",
    [string]$MesaVersion = "24.3.4",
    [string]$BuildDir = "",
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
# Anchor the build dir to the script location so it doesn't depend on the
# directory you launched pwsh from.
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }

function Find-SevenZip {
    foreach ($c in @("7z", "7za")) {
        $cmd = Get-Command $c -ErrorAction SilentlyContinue
        if ($cmd) { return $cmd.Source }
    }
    foreach ($p in @("$env:ProgramFiles\7-Zip\7z.exe", "${env:ProgramFiles(x86)}\7-Zip\7z.exe")) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

# Run the probe (headless) from $dir, with the given env vars set just for it.
# Returns the process exit code.
function Invoke-Probe([string]$dir, [string]$label, [hashtable]$envVars) {
    Write-Host ""
    Write-Host "== probe: $label ==" -ForegroundColor Cyan
    Push-Location $dir
    try {
        foreach ($k in $envVars.Keys) { Set-Item "env:$k" $envVars[$k] }
        & (Join-Path $dir "glon12_probe.exe") --hidden --frames 3
        $code = $LASTEXITCODE
    } finally {
        foreach ($k in $envVars.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
        Pop-Location
    }
    Write-Host "[$label] exit code: $code"
    return $code
}

# ---- Build ----
# cmake is a native command: PowerShell does NOT stop on its non-zero exit even
# with $ErrorActionPreference=Stop, so check $LASTEXITCODE explicitly — otherwise
# a configure/build failure is masked by a downstream "exe not found".
Write-Host "== Configuring + building glon12_probe ==" -ForegroundColor Cyan
cmake -B $BuildDir -S "$root"
if ($LASTEXITCODE -ne 0) {
    throw "cmake configure failed (exit $LASTEXITCODE). A C++ toolchain CMake can drive is required " +
          "(install 'Desktop development with C++' via Visual Studio Build Tools), since SDL2 is built from source."
}
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    throw "cmake build failed (exit $LASTEXITCODE) — see the compiler/linker output above."
}

$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter glon12_probe.exe |
       Select-Object -First 1 -ExpandProperty FullName
if (-not $exe) {
    throw "Build reported success but glon12_probe.exe was not found under $BuildDir. Contents: " +
          ((Get-ChildItem -Path $BuildDir -Recurse -Filter *.exe | Select-Object -ExpandProperty FullName) -join ', ')
}
$exeDir = Split-Path -Parent $exe
Write-Host "Built: $exe"

# ---- Acquire Mesa desktop GLon12 DLLs ----
if (-not $MesaDir) {
    $asset = "mesa3d-$MesaVersion-release-msvc.7z"
    $url   = "https://github.com/pal1000/mesa-dist-win/releases/download/$MesaVersion/$asset"
    $dl    = Join-Path $BuildDir $asset
    if (-not (Test-Path $dl)) {
        Write-Host "Downloading $url"
        Invoke-WebRequest -Uri $url -OutFile $dl
    } else {
        Write-Host "Using cached $dl"
    }
    $sevenZip = Find-SevenZip
    if (-not $sevenZip) {
        throw "7-Zip not found (needed to extract $asset). Install it (e.g. 'winget install 7zip.7zip') or pass -MesaDir."
    }
    $extract = Join-Path $BuildDir "mesa"
    & $sevenZip x $dl "-o$extract" -y | Out-Null
    $gl = Get-ChildItem -Path $extract -Recurse -Filter opengl32.dll | Select-Object -First 1
    if (-not $gl) { throw "opengl32.dll not found in $asset" }
    $MesaDir = $gl.Directory.FullName
}
Write-Host "Mesa GLon12 DLL dir: $MesaDir"

# Stage the FULL Mesa DLL set + the exe together so opengl32.dll's siblings
# resolve and the app-dir search loads Mesa rather than the system GL.
$runDir = Join-Path $BuildDir "glonrun"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
Get-ChildItem $runDir -Filter *.dll -ErrorAction SilentlyContinue | Remove-Item -Force
Copy-Item (Join-Path $MesaDir "*.dll") $runDir -Force
Copy-Item $exe $runDir -Force

# ---- Run ----
# Baseline runs from the (Mesa-free) build output dir → system GL.
$baseCode = Invoke-Probe $exeDir "baseline (system GL)" @{}

$d3d12Code = Invoke-Probe $runDir "GLon12 (GALLIUM_DRIVER=d3d12)" @{ GALLIUM_DRIVER = "d3d12"; MESA_LOG_LEVEL = "info" }

$llvmCode = $null
if ($d3d12Code -ne 0) {
    Write-Warning "d3d12 backend did not produce a working context (often: no GPU / headless). Trying llvmpipe."
    $llvmCode = Invoke-Probe $runDir "Mesa software (GALLIUM_DRIVER=llvmpipe)" @{ GALLIUM_DRIVER = "llvmpipe"; MESA_LOG_LEVEL = "info" }
}

Write-Host ""
Write-Host "== Summary ==" -ForegroundColor Green
Write-Host ("baseline (system GL) exit : {0}" -f $baseCode)
Write-Host ("GLon12 d3d12         exit : {0}" -f $d3d12Code)
if ($null -ne $llvmCode) { Write-Host ("Mesa llvmpipe        exit : {0}" -f $llvmCode) }
Write-Host "Per-run details: $runDir\glon12_probe.log (and $exeDir\glon12_probe.log for baseline)"
if ($d3d12Code -eq 0) {
    Write-Host "GLon12 (D3D12) PASS — the target console translation layer works here." -ForegroundColor Green
} elseif ($llvmCode -eq 0) {
    Write-Host "Mesa GL frontend PASS via llvmpipe; D3D12 backend unavailable on this machine." -ForegroundColor Yellow
}

# Exit 0 if the GLon12 stack proved the feature set under either Mesa driver.
if ($d3d12Code -eq 0 -or $llvmCode -eq 0) { exit 0 } else { exit ($d3d12Code) }
