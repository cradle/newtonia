# Render scene scripts to PNGs on Windows (the NEWTONIA_SHOT harness).
# PowerShell twin of run.sh — no Xvfb needed: a real window opens briefly
# per scene and the PNG lands in shots\out\.
#
#   .\shots\run.ps1                            # every shots\*.shot
#   .\shots\run.ps1 shots\steam1_level1.shot   # just one
#   $env:NEWTONIA_SHOT_SIZE="2560x1440"; .\shots\run.ps1 shots\hero.shot
#
# Wants .\newtonia.exe built first (MSYS2 MINGW64: make -j8, or
# make NETPLAY=0 -j8 — see CLAUDE.md "Windows (MSYS2/MinGW64)").
# If ImageMagick's `magick` is on PATH the PNGs are losslessly
# recompressed (~10x smaller); otherwise they're just bigger, not worse.
param([string[]]$Scenes)

Set-Location (Join-Path $PSScriptRoot "..")
if (-not (Test-Path .\newtonia.exe)) {
  Write-Error "run.ps1: build newtonia.exe first (MINGW64 shell: make -j8)"
  exit 1
}
New-Item -ItemType Directory -Force -Path shots\out | Out-Null

if (-not $Scenes -or $Scenes.Count -eq 0) {
  $Scenes = Get-ChildItem shots\*.shot | ForEach-Object { $_.FullName }
}

# A scene without its own `size` line renders at this default, matching
# run.sh (instead of whatever the local preferences file holds).
$DefSize = "1280x800"
$UserSize = $env:NEWTONIA_SHOT_SIZE

$fail = $false
foreach ($s in $Scenes) {
  $name = [IO.Path]::GetFileNameWithoutExtension($s)
  if ($UserSize) { $size = $UserSize }
  elseif (Select-String -Path $s -Pattern '^size ' -Quiet) { $size = $null }
  else { $size = $DefSize }
  Write-Host "=== $name$(if ($size) { " ($size)" })"

  $env:SDL_AUDIODRIVER = "dummy"       # silent renders
  $env:NEWTONIA_SHOT = "shots\out\$name.png"
  $env:NEWTONIA_SHOT_SCENE = $s
  if ($size) { $env:NEWTONIA_SHOT_SIZE = $size }
  else { Remove-Item Env:NEWTONIA_SHOT_SIZE -ErrorAction SilentlyContinue }

  # The plain build is GUI-subsystem: stdout only shows when piped, which
  # this pipeline does.
  $out = & .\newtonia.exe 2>&1 | Where-Object { $_ -match '^shot:' }
  $out | ForEach-Object { Write-Host $_ }
  if (-not ($out -match 'shot: wrote')) { $fail = $true }

  $png = "shots\out\$name.png"
  if ((Test-Path $png) -and (Get-Command magick -ErrorAction SilentlyContinue)) {
    magick $png -strip "PNG24:$png.tmp.png"
    if ($LASTEXITCODE -eq 0) { Move-Item -Force "$png.tmp.png" $png }
  }
}

# Restore the caller's env override state.
if ($UserSize) { $env:NEWTONIA_SHOT_SIZE = $UserSize }
else { Remove-Item Env:NEWTONIA_SHOT_SIZE -ErrorAction SilentlyContinue }

if ($fail) { exit 1 } else { exit 0 }
