# Render a recorded replay to an MP4 on Windows - the PowerShell twin of
# video.sh (see shots/README.md "Video capture"). No Xvfb: a real window opens
# and renders on the GPU, which is a good deal faster than the headless
# software path.
#
#   .\shots\video.ps1 -Info                       # what's in best.nrp?
#   .\shots\video.ps1                             # best.nrp -> shots\out\gameplay.mp4
#   .\shots\video.ps1 -Replay s1/16124542961030418748 -Duration 1:30
#   .\shots\video.ps1 -Start 0:30 -Duration 1:00 -Size 2560x1440
#
# -Replay takes a leaderboard season/run_id (the pair the site's WATCH link
# carries - it is downloaded for you), a path to a .nrp, or one of the local
# shorthands best/recent/current/bestcoop/online/last.
#
# Wants .\newtonia.exe and ffmpeg on PATH (winget install Gyan.FFmpeg).
#
# KEEP THIS FILE PURE ASCII. Windows PowerShell 5.1 reads a .ps1 as ANSI
# unless it has a UTF-8 BOM, so a UTF-8 em dash arrives as three CP1252
# characters - and the last of them is a curly close-quote, which 5.1 accepts
# as a STRING DELIMITER. One em dash inside a string ended the string early
# and the file would not parse at all (field, 2026-08-05). Comments survive
# it; strings do not. Do not rely on the difference.
#
# ---- why this is not just video.sh under MSYS2 ----
#
# video.sh streams frames through a FIFO, and MSYS2's fifos are emulated for
# MSYS2-linked programs: a native newtonia.exe cannot open one. So that script
# cannot work here however it is invoked. This uses NEWTONIA_VIDEO=- instead:
# frames on stdout, piped to ffmpeg by cmd, which is byte-clean. A PowerShell
# pipeline is not - it decodes bytes as text and would destroy the frames -
# which is why the pipe runs inside a generated .bat below.
#
# The two passes run one after the other here rather than together as they do
# on POSIX. The audio pass is paced by the audio device and cannot be starved
# without losing sync: overlapping it with the render and x264 measured a
# 135 ms stall against 23 ms for the same window alone. Sequential costs the
# clip's own duration and removes the problem entirely.
param(
  [string]$Replay   = "best",
  [string]$Out      = "shots\out\gameplay.mp4",
  [string]$Size     = "1920x1080",
  [int]$Fps         = 60,
  [string]$Start    = "0",
  [string]$Duration = "0",
  [int]$Crf         = 16,
  [switch]$NoAudio,
  [switch]$NoHud,
  [switch]$Chrome,
  [switch]$Keep,
  [switch]$Info
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

if (-not (Test-Path .\newtonia.exe)) {
  Write-Error ("video.ps1: newtonia.exe not found. Build it in an MSYS2 " +
    "MINGW64 shell (make NETPLAY=0 -j8), or take the Windows workflow's " +
    "artifact from a green run. With neither, the 'Render replay video' " +
    "workflow does the whole job in CI and hands back an mp4 - no local " +
    "toolchain at all.")
  exit 1
}

# "90", "1:30" and "500ms" -> milliseconds, matching video.sh's to_ms.
function ConvertTo-Ms([string]$t) {
  if ($t -match '^(\d+)ms$')     { return [int]$Matches[1] }
  if ($t -match '^(\d+):(\d+)$') { return ([int]$Matches[1] * 60000 + [int]$Matches[2] * 1000) }
  return ([int]$t * 1000)
}
$StartMs = ConvertTo-Ms $Start
$DurMs   = ConvertTo-Ms $Duration
$W, $H   = $Size -split 'x'

# A leaderboard run (season/run_id) is fetched from the board's public blob
# endpoint - the same GET the site's WATCH link uses. Anything else is passed
# through: a path, or one of the game's own shorthands.
$ReplayArg = $Replay
if ($Replay -match '^[A-Za-z0-9._-]+/[0-9]{1,20}$') {
  # 5.1 on an older build can still default to TLS 1.0 here, which the
  # worker refuses.
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  $url = "https://newtonia-board.gfmcc.workers.dev/replay/$Replay.nrp"
  $ReplayArg = Join-Path $env:TEMP ("newtonia-" + ($Replay -replace '/', '-') + ".nrp")
  if (-not (Test-Path $ReplayArg)) {
    Write-Host "=== downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $ReplayArg -UseBasicParsing
  }
}

$env:NEWTONIA_VIDEO_REPLAY   = $ReplayArg
$env:NEWTONIA_VIDEO_SIZE     = $Size
$env:NEWTONIA_VIDEO_FPS      = "$Fps"
$env:NEWTONIA_VIDEO_START_MS = "$StartMs"
$env:NEWTONIA_VIDEO_MS       = "$DurMs"
if ($NoHud)  { $env:NEWTONIA_VIDEO_HUD = "0" } else { $env:NEWTONIA_VIDEO_HUD = "1" }
if ($Chrome) { $env:NEWTONIA_VIDEO_CHROME = "1" } else { $env:NEWTONIA_VIDEO_CHROME = "0" }
# One stream per run - they are separate passes and the game refuses both.
Remove-Item Env:\NEWTONIA_VIDEO -ErrorAction SilentlyContinue
Remove-Item Env:\NEWTONIA_VIDEO_AUDIO -ErrorAction SilentlyContinue
Remove-Item Env:\NEWTONIA_VIDEO_INFO -ErrorAction SilentlyContinue

if ($Info) {
  $env:NEWTONIA_VIDEO = "NUL"
  $env:NEWTONIA_VIDEO_INFO = "1"
  & .\newtonia.exe 2>&1 | Select-String -Pattern "video:"
  exit 0
}

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
  Write-Error "video.ps1: ffmpeg not found on PATH (winget install Gyan.FFmpeg)"
  exit 1
}

$outDir = Split-Path -Parent $Out
if (-not $outDir) { $outDir = "." }
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$work = Join-Path $outDir ("." + [IO.Path]::GetFileNameWithoutExtension($Out) + ".work")
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$raw    = Join-Path $work "audio.raw"
$silent = Join-Path $work "silent.mp4"

Write-Host "=== rendering $Replay -> $Out ($Size @ ${Fps}fps)"

# --- audio pass: the mix, at the device's own rate, nothing drawn ---
$rate = 44100
$ch   = 2
if (-not $NoAudio) {
  $env:NEWTONIA_VIDEO_AUDIO = $raw
  $alog = & .\newtonia.exe 2>&1
  Remove-Item Env:\NEWTONIA_VIDEO_AUDIO
  $alog | Select-String -Pattern "video:" | ForEach-Object { Write-Host "audio pass: $_" }
  # The raw stream carries no format; take the rate and channels the pass
  # actually opened rather than assuming them.
  $spec = $alog | Select-String -Pattern 'audio (\d+) Hz (\d+) ch' | Select-Object -First 1
  if ($spec) {
    $rate = [int]$spec.Matches[0].Groups[1].Value
    $ch   = [int]$spec.Matches[0].Groups[2].Value
  }
}

# --- video pass: frames on stdout, piped to ffmpeg ---
# The pipeline goes in a generated .bat rather than on a cmd /c command line:
# cmd's quote handling for /c strings with embedded quotes is its own special
# hell, and a path with a space in it (OneDrive\Documents, say) is exactly
# where it bites.
$env:NEWTONIA_VIDEO = "-"
$exe = (Resolve-Path .\newtonia.exe).Path
$bat = Join-Path $work "render.bat"
$ff  = "ffmpeg -hide_banner -loglevel warning -y -f rawvideo -pix_fmt rgb24 " +
       "-s ${W}x${H} -r $Fps -i - -an -c:v libx264 -preset slow -crf $Crf " +
       "-pix_fmt yuv420p -movflags +faststart " + '"' + $silent + '"'
Set-Content -Path $bat -Encoding ASCII -Value @(
  "@echo off",
  '"' + $exe + '" | ' + $ff
)
& cmd /c "`"$bat`""
$rc = $LASTEXITCODE
Remove-Item Env:\NEWTONIA_VIDEO
if ($rc -ne 0 -or -not (Test-Path $silent)) {
  Write-Error "video.ps1: the render failed (exit $rc)"
  exit 1
}

# --- mux: the video is copied, not re-encoded ---
if ((-not $NoAudio) -and (Test-Path $raw) -and ((Get-Item $raw).Length -gt 0)) {
  & ffmpeg -hide_banner -loglevel error -y -i $silent `
      -f s16le -ar $rate -ac $ch -i $raw `
      -c:v copy -c:a aac -b:a 320k -shortest -movflags +faststart $Out
} else {
  if (-not $NoAudio) { Write-Host "video.ps1: the audio pass produced nothing - writing silent" }
  Move-Item -Force $silent $Out
}
if (-not $Keep) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }

Write-Host "video.ps1: wrote $Out"
if (Get-Command ffprobe -ErrorAction SilentlyContinue) {
  & ffprobe -hide_banner -loglevel error -show_entries `
      "format=duration,size:stream=codec_name,width,height,r_frame_rate" `
      -of default=noprint_wrappers=1 $Out
}
