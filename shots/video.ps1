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
# Wants .\newtonia.exe and ffmpeg. If ffmpeg is not on PATH the usual places
# are checked (MSYS2, winget, choco, scoop, Program Files) and -Ffmpeg takes an
# explicit path. Install with:  winget install Gyan.FFmpeg  - and then open a
# NEW terminal, because winget updates PATH for new sessions only.
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
  [switch]$Info,
  [string]$Ffmpeg = ""
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$exe = $null
if (Test-Path .\newtonia.exe) { $exe = (Resolve-Path .\newtonia.exe).Path }
if (-not $exe) {
  Write-Error ("video.ps1: newtonia.exe not found. Build it in an MSYS2 " +
    "MINGW64 shell (make NETPLAY=0 -j8), or take the Windows workflow's " +
    "artifact from a green run. With neither, the 'Render replay video' " +
    "workflow does the whole job in CI and hands back an mp4 - no local " +
    "toolchain at all.")
  exit 1
}

# An exe built before the video harness existed knows nothing about
# NEWTONIA_VIDEO, so it ignores every variable set below and opens the ordinary
# game instead: two windows sitting at the menu, no frames, no error. Look for
# the harness in the binary rather than letting that happen - env var names are
# plain strings in the image, so this is a substring search, not a launch.
$img = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
if ($img.IndexOf("NEWTONIA_VIDEO_REPLAY") -lt 0) {
  Write-Error ("video.ps1: this newtonia.exe predates the video harness - it " +
    "would just open the game. Rebuild it after pulling: in an MSYS2 MINGW64 " +
    "shell, 'make NETPLAY=0 -j8'.")
  exit 1
}
$img = $null

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
  $iOut = Join-Path $env:TEMP "newtonia-info.txt"
  $iErr = Join-Path $env:TEMP "newtonia-info.err"
  Start-Process -FilePath $exe -NoNewWindow -Wait `
    -RedirectStandardOutput $iOut -RedirectStandardError $iErr | Out-Null
  foreach ($f in @($iOut, $iErr)) {
    if (Test-Path $f) { Get-Content $f | Select-String -Pattern "video:" }
  }
  exit 0
}

# ffmpeg: PATH first, then the places Windows installers actually put it. A
# fresh "winget install" does not reach an already-open shell, which makes
# "not on PATH" the single most likely way this script fails for someone who
# has just installed it.
function Resolve-Ffmpeg {
  if ($Ffmpeg) {
    if (Test-Path $Ffmpeg) { return (Resolve-Path $Ffmpeg).Path }
    Write-Error "video.ps1: -Ffmpeg '$Ffmpeg' does not exist"
    exit 1
  }
  $onPath = Get-Command ffmpeg -ErrorAction SilentlyContinue
  if ($onPath) { return $onPath.Source }
  $candidates = @(
    "C:\msys64\mingw64\bin\ffmpeg.exe",
    "$env:LOCALAPPDATA\Microsoft\WinGet\Links\ffmpeg.exe",
    "$env:ProgramData\chocolatey\bin\ffmpeg.exe",
    "$env:USERPROFILE\scoop\shims\ffmpeg.exe",
    "$env:ProgramFiles\ffmpeg\bin\ffmpeg.exe"
  )
  foreach ($c in $candidates) { if ($c -and (Test-Path $c)) { return $c } }
  # WinGet installs the real binary under a versioned Packages directory and
  # only links it into Links\; if the link is missing, go find the binary.
  $pkg = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
  if (Test-Path $pkg) {
    $found = Get-ChildItem -Path $pkg -Filter ffmpeg.exe -Recurse -ErrorAction SilentlyContinue |
             Select-Object -First 1
    if ($found) { return $found.FullName }
  }
  return $null
}

$FfmpegExe = Resolve-Ffmpeg
if (-not $FfmpegExe) {
  Write-Error ("video.ps1: ffmpeg not found. Install it with " +
    "'winget install Gyan.FFmpeg' and then open a NEW terminal (winget " +
    "updates PATH for new sessions only), or pass -Ffmpeg C:\path\to\" +
    "ffmpeg.exe. No ffmpeg at all? The 'Render replay video' workflow does " +
    "the whole job in CI and hands back an mp4.")
  exit 1
}
Write-Host "=== ffmpeg: $FfmpegExe"
$FfprobeExe = Join-Path (Split-Path -Parent $FfmpegExe) "ffprobe.exe"

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
  # Start-Process -Wait, not the call operator: newtonia.exe is built for the
  # GUI subsystem (see CLAUDE.md), and PowerShell does not block on a GUI app -
  # it returns at once and the next pass starts on top of this one. That is the
  # second half of "two instances at the menu".
  $aOut = Join-Path $work "audio.out"
  $aErr = Join-Path $work "audio.err"
  Start-Process -FilePath $exe -NoNewWindow -Wait `
    -RedirectStandardOutput $aOut -RedirectStandardError $aErr | Out-Null
  Remove-Item Env:\NEWTONIA_VIDEO_AUDIO
  $alog = @()
  foreach ($f in @($aOut, $aErr)) {
    if (Test-Path $f) { $alog += (Get-Content $f) }
  }
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
$bat = Join-Path $work "render.bat"

# The .bat is built with a HERE-STRING, not by concatenating pieces. Building
# it as '"' + $exe + '" | ' + $ff inside an array literal did not produce one
# line: PowerShell took the fragments as separate ARRAY ELEMENTS, Set-Content
# wrote one per line, and cmd then tried to execute a lone quote as a command
# ("'"' is not recognized as an internal or external command"). A double-quoted
# here-string interpolates variables and leaves quotes alone, so what you read
# here is exactly what lands in the file.
$vErr = Join-Path $work "render.err"
$batText = @"
@echo off
"$exe" 2>"$vErr" | "$FfmpegExe" -hide_banner -loglevel warning -y -f rawvideo -pix_fmt rgb24 -s ${W}x${H} -r $Fps -i - -an -c:v libx264 -preset slow -crf $Crf -pix_fmt yuv420p -movflags +faststart "$silent"
"@
Set-Content -Path $bat -Value $batText -Encoding ASCII

# Echo the pipeline. If it fails, the command that failed is on screen rather
# than inside a temporary file that the cleanup below has already deleted.
Write-Host "=== $((Get-Content $bat)[1])"
Write-Host "=== the game logs to $vErr"

# Run the pipeline in the background and TAIL its log, rather than waiting in
# silence for a job that takes minutes. Printing the log only after cmd
# returned meant a stall showed nothing at all - no progress, no error, just a
# white window - and the file holding the answer was deleted by the cleanup
# before anyone could read it.
$proc = Start-Process -FilePath $env:ComSpec -ArgumentList "/c", "`"$bat`"" `
          -NoNewWindow -PassThru
$shown = 0
while (-not $proc.HasExited) {
  Start-Sleep -Milliseconds 500
  if (Test-Path $vErr) {
    $lines = @(Get-Content $vErr -ErrorAction SilentlyContinue)
    if ($lines.Count -gt $shown) {
      $lines[$shown..($lines.Count - 1)] |
        Where-Object { $_ -match "video:|replay:" } |
        ForEach-Object { Write-Host "  $_" }
      $shown = $lines.Count
    }
  }
}
$proc.WaitForExit()
$rc = $proc.ExitCode
Remove-Item Env:\NEWTONIA_VIDEO
# Anything the tail missed between its last poll and the exit.
if (Test-Path $vErr) {
  $lines = @(Get-Content $vErr -ErrorAction SilentlyContinue)
  if ($lines.Count -gt $shown) {
    $lines[$shown..($lines.Count - 1)] |
      Where-Object { $_ -match "video:|replay:" } |
      ForEach-Object { Write-Host "  $_" }
  }
}
if ($rc -ne 0 -or -not (Test-Path $silent)) {
  Write-Host "--- $bat was:"
  Get-Content $bat | ForEach-Object { Write-Host "    $_" }
  if (Test-Path $vErr) {
    Write-Host "--- the game said:"
    Get-Content $vErr | ForEach-Object { Write-Host "    $_" }
  }
  Write-Host "--- keeping $work for inspection"
  Write-Error "video.ps1: the render failed (exit $rc). The pipeline above is what cmd ran."
  exit 1
}

# --- mux: the video is copied, not re-encoded ---
if ((-not $NoAudio) -and (Test-Path $raw) -and ((Get-Item $raw).Length -gt 0)) {
  & $FfmpegExe -hide_banner -loglevel error -y -i $silent `
      -f s16le -ar $rate -ac $ch -i $raw `
      -c:v copy -c:a aac -b:a 320k -shortest -movflags +faststart $Out
} else {
  if (-not $NoAudio) { Write-Host "video.ps1: the audio pass produced nothing - writing silent" }
  Move-Item -Force $silent $Out
}
if (-not $Keep) { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }

Write-Host "video.ps1: wrote $Out"
if (Test-Path $FfprobeExe) {
  & $FfprobeExe -hide_banner -loglevel error -show_entries `
      "format=duration,size:stream=codec_name,width,height,r_frame_rate" `
      -of default=noprint_wrappers=1 $Out
}
