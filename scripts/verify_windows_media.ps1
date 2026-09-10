# ShowMaster Windows media-backend verification script
# Purpose: compile + run the REAL media branch (FFmpeg 8 decode + WASAPI render)
#          on Windows, mirroring .github/workflows/windows-build.yml, plus an
#          [audio] startup-log probe and an optional audible playback smoke.
#
# Usage (in "x64 Native Tools Command Prompt for VS 2022", or any PowerShell
#        that can find MSVC via the preset):
#   powershell -ExecutionPolicy Bypass -File scripts\verify_windows_media.ps1
#
# Optional parameters:
#   -Preset windows-msvc-debug|windows-msvc-release   (default: debug)
#   -MediaFile "C:\path\to\file-with-audio.mp4"        (optional audible smoke)
#   -SkipVcpkgInstall                                   (deps already installed)
#   -Port <n>                                           (headless smoke port)

param(
    [string]$Preset = "windows-msvc-debug",
    [string]$MediaFile = "",
    [switch]$SkipVcpkgInstall,
    [int]$Port = 8099
)

$ErrorActionPreference = "Stop"
$Root = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $Root "build\$Preset"
$Exe = Join-Path $BuildDir "sm_headless.exe"
$LogOut = Join-Path $env:TEMP "sm_headless_stdout.log"
$LogErr = Join-Path $env:TEMP "sm_headless_stderr.log"
$CfgLog = Join-Path $env:TEMP "sm_cmake_configure.log"
$Passed = 0; $Warned = 0; $Failed = 0

function Pass([string]$m) { $script:Passed++; Write-Host "[PASS] $m" -ForegroundColor Green }
function Warn([string]$m) { $script:Warned++; Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Fail([string]$m) { $script:Failed++; Write-Host "[FAIL] $m" -ForegroundColor Red }

Write-Host "== ShowMaster Windows media-backend verification ==" -ForegroundColor Cyan
Write-Host ("Root : " + $Root)
Write-Host ("Preset: " + $Preset + "  MediaFile: " + $(if ($MediaFile) { $MediaFile } else { "(none, compile-only path)" }))
if (-not (Test-Path $Exe)) { Write-Host "Will configure + build first (no existing build found)." }
else { Write-Host "Existing build found; build step will refresh it." }
Write-Host ""

# ---- 0. vcpkg dependency check -------------------------------------------
if (-not $env:VCPKG_ROOT) {
    # 必须硬失败：VCPKG_ROOT 为空时下面的 -DCMAKE_TOOLCHAIN_FILE 会拼成不存在的路径，
    # Join-Path 在 $ErrorActionPreference=Stop 下抛出的是一句无从下手的参数绑定异常。
    Fail "VCPKG_ROOT is not set - the vcpkg toolchain path cannot be built, so ffmpeg/sqlite3 will not be found."
    Write-Host "  Set it first, e.g.:  `$env:VCPKG_ROOT = 'C:\vcpkg'"
    exit 1
} elseif (-not $SkipVcpkgInstall) {
    Write-Host "[1/5] vcpkg install sqlite3 + ffmpeg ..." -ForegroundColor Cyan
    & (Join-Path $env:VCPKG_ROOT "vcpkg.exe") install sqlite3:x64-windows
    if ($LASTEXITCODE -ne 0) { Fail "vcpkg install sqlite3 failed (exit $LASTEXITCODE)"; exit 1 }
    & (Join-Path $env:VCPKG_ROOT "vcpkg.exe") install ffmpeg:x64-windows
    if ($LASTEXITCODE -ne 0) { Fail "vcpkg install ffmpeg failed (exit $LASTEXITCODE)"; exit 1 }
    Pass "vcpkg dependencies installed"
}

# ---- 1. Configure (capture output to assert REAL branch was enabled) -----
Write-Host "[2/5] cmake configure ($Preset) ..." -ForegroundColor Cyan
Push-Location $Root
try {
    $tc = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
    & cmake --preset $Preset "-DCMAKE_TOOLCHAIN_FILE=$tc" *> $CfgLog
    if ($LASTEXITCODE -ne 0) {
        Fail "cmake configure failed (exit $LASTEXITCODE). Tail of log:"
        Get-Content $CfgLog -Tail 30
        exit 1
    }
    $cfg = Get-Content $CfgLog -Raw
    # 只匹配 ASCII 子串。CMakeLists.txt 原文是 "FFmpeg found — enabling real media backend"
    # （破折号 U+2014），非 UTF-8 控制台代码页下该字符会被替换成 "?"，整句匹配会静默漏掉，
    # 结果是「真后端」与「stub 后端」两个分支全都不命中、掉进 Warn —— 门禁形同虚设。
    if ($cfg -match "FFmpeg found") {
        Pass "configured with REAL media backend (FFmpeg found)"
    } elseif ($cfg -match "stub media backend") {
        Fail "configured with STUB backend - ffmpeg not found. Check vcpkg ffmpeg:x64-windows + VCPKG_ROOT toolchain."
        exit 1
    } else {
        Warn "configure succeeded but the 'FFmpeg found' substring was not seen - inspect $CfgLog"
    }
} finally { Pop-Location }

# ---- 2. Build -------------------------------------------------------------
Write-Host "[3/5] cmake build ($Preset) ..." -ForegroundColor Cyan
Push-Location $Root
try {
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { Fail "build failed (exit $LASTEXITCODE)"; exit 1 }
    Pass "build succeeded"
} finally { Pop-Location }

# ---- 3. ctest --------------------------------------------------------------
Write-Host "[4/5] ctest ($Preset) ..." -ForegroundColor Cyan
Push-Location $Root
try {
    & ctest --preset $Preset --output-on-failure
    if ($LASTEXITCODE -ne 0) { Fail "ctest failed (exit $LASTEXITCODE)"; exit 1 }
    Pass "ctest all green"
} finally { Pop-Location }

# ---- 4. Headless smoke: startup log + HTTP /api/status --------------------
Write-Host "[5/5] headless smoke on port $Port ..." -ForegroundColor Cyan
if (Test-Path $LogOut) { Remove-Item $LogOut -Force }
if (Test-Path $LogErr) { Remove-Item $LogErr -Force }
$proc = $null
try {
    $proc = Start-Process -FilePath $Exe -ArgumentList "--port", "$Port" `
        -WorkingDirectory $BuildDir -RedirectStandardOutput $LogOut `
        -RedirectStandardError $LogErr -PassThru -NoNewWindow
    Start-Sleep -Seconds 4

    $alive = -not $proc.HasExited
    if ($alive) {
        Pass "sm_headless.exe started (pid $($proc.Id))"
    } else {
        Fail "sm_headless.exe exited early (code $($proc.ExitCode)). stderr:"
        Get-Content $LogErr -Tail 20
        exit 1
    }

    # HTTP status is the hard gate; the [audio] log line is best-effort
    # because stdout is fully buffered when redirected to a file.
    #
    # /api/status 是需要鉴权的（web_gateway.cpp: check_auth 从 Authorization: Bearer
    # 或 ?token= 取会话 token），裸请求会 401；而 Invoke-WebRequest 在
    # $ErrorActionPreference=Stop 下会把 401 变成终止性异常，直接炸掉最后一步。
    # 因此先 POST /api/login（默认 admin/admin123）换 token，与 CI 步骤一致。
    $login = $null
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            $login = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/api/login" `
                -Method Post -ContentType "application/json" `
                -Body '{"username":"admin","password":"admin123"}' -TimeoutSec 3
            if ($login.token) { break }
        } catch { }
    }
    if (-not $login -or -not $login.token) {
        Fail "POST /api/login did not return a token within ~15s"
    } else {
        Pass "POST /api/login -> session token acquired"
        try {
            $hdr = @{ Authorization = "Bearer $($login.token)" }
            $resp = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/api/status" `
                -Headers $hdr -UseBasicParsing -TimeoutSec 5
            if ($resp.StatusCode -eq 200) {
                Pass "GET /api/status -> $($resp.StatusCode)"
            } else {
                Fail "GET /api/status -> $($resp.StatusCode) (expected 200)"
            }
        } catch {
            Fail "GET /api/status threw: $($_.Exception.Message)"
        }
    }

    $stdout = ""
    if (Test-Path $LogOut) { $stdout = Get-Content $LogOut -Raw }
    if ($stdout -match "\[audio\] WASAPI") {
        Pass "[audio] WASAPI default render endpoint available (audible playback enabled)"
    } elseif ($stdout -match "\[audio\]") {
        Warn "[audio] line present but endpoint unavailable (pure-video mode). "
        Warn "Expected when running without an audio session (e.g. CI / Session 0 / no sound card)."
        Warn "For the audible path run this script in an interactive desktop session with a sound device."
    } else {
        Warn "[audio] log line not observed yet - stdout may still be buffered. "
        Warn "The service itself is healthy (HTTP 200). Re-run after process exit or use a terminal (no redirect)."
    }
} finally {
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
}

# ---- 5. Optional audible playback smoke ------------------------------------
if ($MediaFile) {
    Write-Host ""
    Write-Host "Audible smoke requested with: $MediaFile" -ForegroundColor Cyan
    if (-not (Test-Path $MediaFile)) {
        Fail "Media file not found: $MediaFile"
    } else {
        Write-Host "Manual step (audio must be audible on this machine):"
        Write-Host "  1. Browse http://127.0.0.1:$Port  (login admin/admin123)"
        Write-Host "  2. Import/play: $MediaFile"
        Write-Host "  3. Expect sound within ~1s; volume obeys gain + fade-in envelope."
        Write-Host "  4. Repeat open -> start -> stop -> close several times."
        Write-Host "     Watch Task Manager: handle/thread counts must not grow (no leak)."
    }
}

# ---- Summary ---------------------------------------------------------------
Write-Host ""
Write-Host ("==== Result: PASS=$Passed WARN=$Warned FAIL=$Failed ====") -ForegroundColor Cyan
if ($Failed -gt 0) { exit 1 } else { exit 0 }
