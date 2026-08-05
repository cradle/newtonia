# Render a recorded replay to an MP4 on Windows — PowerShell twin of
# video.sh (see shots/README.md "Video capture"). No Xvfb: a real window
# opens and renders on the GPU, which is a good deal faster than the
# headless software path.
#
#   .\shots\video.ps1 -Info                       # what's in best.nrp?
#   .\shots\video.ps1                             # best.nrp -> shots\out\gameplay.mp4
#   .\shots\video.ps1 -Replay s1/1612454296103041874 -Duration 1:30
#   .\shots\video.ps1 -Start 0:30 -Duration 1:00 -Size 2560x1440
#
# -Replay takes a leaderboard season/run_id (the pair the site's WATCH link
# carries — it is downloaded for you), a path to a .nrp, or one of the local
# shorthands best/recent/current/bestcoop/online/last.
#
# Wants .\newtonia.exe and ffmpeg on PATH (winget install Gyan.FFmpeg).
#
# ---- why this is not just video.sh under MSYS2 ----
#
# video.sh streams frames through a FIFO, and MSYS2's fifos are emulated for
# MSYS2-linked programs: a native newtonia.exe cannot open one. So that script
# cannot work here however it is invoked. This uses NEWTONIA_VIDEO=- instead —
# frames on stdout, piped to ffmpeg through cmd, which is byte-clean (a
# PowerShell pipeline is not: it would decode the frames as text and destroy
# them, which is why the pipe below runs under cmd /c).
#
# The two passes run one after the other here rather than together as they do
# on POSIX. The audio pass is paced by the audio device and cannot be starved
# without losing sync — overlapping it with the render and x264 measured a
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
    "MINGW64 shell (make NETPLAY=0 -j8), or download the Windows artifact " +
    "from a green run of the Windows workflow. If you have neither, the " +
    "Render replay video workflow does the whole job in CI and hands back " +
    "an mp4 — no local toolchain at all.")
  exit 1
}

# "90", "1:30" and "500ms" -> milliseconds, matching video.sh's to_ms.
function ConvertTo-Ms([string]$t) {
  if ($t -match '^(\d+)ms$') { return [int]$Matches[1] }
  if ($t -match '^(\d+):(\d+)$') { return ([int]$Matches[1] * 60000 + [int]$Matches[2] * 1000) }
  return ([int]$t * 1000)
}
$StartMs = ConvertTo-Ms $Start
$DurMs   = ConvertTo-Ms $Duration
$W, $H = $Size -split 'x'

# A leaderboard run (season/run_id) is fetched from the board's public blob
# endpoint — the same GET the site's WATCH link uses. Anything else is passed
# through: a path, or one of the game's own shorthands.
$ReplayArg = $Replay
if ($Replay -match '^[A-Za-z0-9._-]+/[0-9]{1,20}$') {
  $url = "https://newtonia-board.gfmcc.workers.dev/replay/$Replay.nrp"
  $ReplayArg = Join-Path $env:TEMP ("newtonia-" + ($Replay -replace '/', '-') + ".nrp")
  if (-not (Test-Path $ReplayArg)) {
    Write-Host "=== downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $ReplayArg
  }
}

$env:NEWTONIA_VIDEO_REPLAY   = $ReplayArg
$env:NEWTONIA_VIDEO_SIZE     = $Size
$env:NEWTONIA_VIDEO_FPS      = "$Fps"
$env:NEWTONIA_VIDEO_START_MS = "$StartMs"
$env:NEWTONIA_VIDEO_MS       = "$DurMs"
$env:NEWTONIA_VIDEO_HUD      = if ($NoHud) { "0" } else { "1" }
$env:NEWTONIA_VIDEO_CHROME   = if ($Chrome) { "1" } else { "0" }
# Only one stream per run — they are separate passes and the game refuses both.
Remove-Item Env:\NEWTONIA_VIDEO -ErrorAction SilentlyContinue
Remove-Item Env:\NEWTONIA_VIDEO_AUDIO -ErrorAction SilentlyContinue

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

$work = Join-Path (Split-Path -Parent $Out) ("." + [IO.Path]::GetFileNameWithoutExtension($Out) + ".work")
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$raw    = Join-Path $work "audio.raw"
$silent = Join-Path $work "silent.mp4"

Write-Host "=== rendering $Replay -> $Out ($Size @ ${Fps}fps)"

# --- audio pass: the mix, at the device's own rate, nothing drawn ---
$rate = 44100; $ch = 2
if (-not $NoAudio) {
  $env:NEWTONIA_VIDEO_AUDIO = $raw
  & .\newtonia.exe 2>&1 | Tee-Object -Variable alog | Select-String -Pattern "video:"
  Remove-Item Env:\NEWTONIA_VIDEO_AUDIO
  # The raw stream carries no format; take the rate and channels the pass
  # actually opened rather than assuming them.
  $spec = $alog | Select-String -Pattern 'audio (\d+) Hz (\d+) ch'
  if ($spec) {
    $rate = [int]$spec.Matches[0].Groups[1].Value
    $ch   = [int]$spec.Matches[0].Groups[2].Value
  }
}

# --- video pass: frames on stdout, piped to ffmpeg by cmd (see the header) ---
$env:NEWTONIA_VIDEO = "-"
$ff = "ffmpeg -hide_banner -loglevel warning -y -f rawvideo -pix_fmt rgb24 " +
      "-s ${W}x${H} -r $Fps -i - -an -c:v libx264 -preset slow -crf $Crf " +
      "-pix_fmt yuv420p -movflags +faststart `"$silent`""
& cmd /c "`"$((Resolve-Path .\newtonia.exe).Path)`" | $ff"
$rc = $LASTEXITCODE
Remove-Item Env:\NEWTONIA_VIDEO
if ($rc -ne 0 -or -not (Test-Path $silent)) {
  Write-Error "video.ps1: the render failed (exit $rc)"
  exit 1
}

# --- mux ---
if ((-not $NoAudio) -and (Test-Path $raw) -and (Get-Item $raw).Length -gt 0) {
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
