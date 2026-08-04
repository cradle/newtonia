# Render the store screenshots at mobile sizes with the touch OSD —
# PowerShell twin of mobile.sh. Output: shots\out\mobile\.
#
# CAVEAT: these sizes (up to 2868x1320 / 2752x2064) need a display at
# least that large — on a 1080p desktop Windows clamps the window and the
# capture comes out smaller (the `shot: wrote` line prints the real
# size). On a smaller display use WSL + shots/mobile.sh (any size,
# software-rendered), or take the repo's committed renders.
param([string[]]$Scenes)

Set-Location (Join-Path $PSScriptRoot "..")
if (-not (Test-Path .\newtonia.exe)) {
  Write-Error "mobile.ps1: build newtonia.exe first (MINGW64 shell: make -j8)"
  exit 1
}
$Out = "shots\out\mobile"
New-Item -ItemType Directory -Force -Path $Out | Out-Null

if (-not $Scenes -or $Scenes.Count -eq 0) {
  $Scenes = @("menu_mobile", "steam1_level1", "steam3_level5", "steam4_level14", "steam5_level20")
}
$Devices = @(
  @{ name = "iphone69";   size = "2868x1320" },
  @{ name = "ipad13";     size = "2752x2064" },
  @{ name = "android";    size = "1920x1080" },
  @{ name = "androidtab"; size = "2560x1600" }
)

$fail = $false
foreach ($s in $Scenes) {
  $scene = "shots\$s.shot"
  if (-not (Test-Path $scene)) { Write-Error "no such scene $scene"; $fail = $true; continue }
  foreach ($d in $Devices) {
    Write-Host "=== $s @ $($d.name) ($($d.size))"
    $env:SDL_AUDIODRIVER = "dummy"
    $env:NEWTONIA_FORCE_TOUCH = "1"
    $env:NEWTONIA_SHOT = "$Out\${s}_$($d.name).png"
    $env:NEWTONIA_SHOT_SCENE = $scene
    $env:NEWTONIA_SHOT_SIZE = $d.size
    $out = & .\newtonia.exe 2>&1 | Where-Object { $_ -match '^shot:' }
    $out | ForEach-Object { Write-Host $_ }
    if (-not ($out -match 'shot: wrote')) { $fail = $true }
    $png = "$Out\${s}_$($d.name).png"
    if ((Test-Path $png) -and (Get-Command magick -ErrorAction SilentlyContinue)) {
      magick $png -strip "$png.tmp.png"
      if ($LASTEXITCODE -eq 0) { Move-Item -Force "$png.tmp.png" $png }
    }
  }
}
Remove-Item Env:NEWTONIA_FORCE_TOUCH -ErrorAction SilentlyContinue
if ($fail) { exit 1 } else { exit 0 }
