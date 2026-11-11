<#
.SYNOPSIS
    Single-Machine Dual-Instance Test Harness for Watchers of the Void (Multivoid).
.DESCRIPTION
    Launches two instances of VotV on the local machine side-by-side:
      - Instance 1 (Host): Uses profile 'Default' on the left half of the display.
      - Instance 2 (Client): Uses profile 'Client1' on the right half with -nosound. Join 127.0.0.1:47621 from Direct Connect.
    Uses the installed profile DLLs and existing game settings.
.PARAMETER Mode
    'Both' (default), 'Host', 'Client', or 'Kill' (terminates running VotV instances).
.PARAMETER KillExisting
    Automatically terminates any running VotV processes before launch.
.PARAMETER ResX
    Window width per instance (default: 1260 for 2560x1440 displays).
.PARAMETER ResY
    Window height per instance (default: 1380 for 2560x1440 displays).
#>
[CmdletBinding()]
param(
    [ValidateSet('Both', 'Host', 'Client', 'Kill')]
    [string]$Mode = 'Both',

    [switch]$KillExisting,

    [int]$ResX = 1260,

    [int]$ResY = 1380
)

$ErrorActionPreference = 'Stop'

$userProfile = if ($env:USERPROFILE) { $env:USERPROFILE } else { [System.Environment]::GetFolderPath('UserProfile') }
$gameExe = Join-Path $userProfile 'Documents\VOTV\0.9\WindowsNoEditor\VotV.exe'
$appData = $env:APPDATA
$profileRoot = Join-Path $appData 'Thunderstore Mod Manager\DataFolder\VotV\profiles'
$profileDefault = Join-Path $profileRoot 'Default'
$profileClient1 = Join-Path $profileRoot 'Client1'

if (-not (Test-Path -LiteralPath $gameExe)) {
    throw "Game executable not found at: $gameExe"
}
if (-not (Test-Path -LiteralPath $profileDefault)) {
    throw "Host profile 'Default' not found at: $profileDefault"
}
if (-not (Test-Path -LiteralPath $profileClient1)) {
    throw "Client profile 'Client1' not found at: $profileClient1"
}

# 1. Handle process kill request or existing processes
$running = @(Get-Process -Name 'VotV', 'VotV-Win64-Shipping' -ErrorAction SilentlyContinue)
if ($Mode -eq 'Kill') {
    if ($running.Count -gt 0) {
        Write-Host "Terminating $($running.Count) running VotV process(es)..." -ForegroundColor Yellow
        $running | Stop-Process -Force
        Start-Sleep -Milliseconds 500
        Write-Host "All VotV processes terminated." -ForegroundColor Green
    } else {
        Write-Host "No VotV processes currently running." -ForegroundColor Cyan
    }
    return
}

if ($running.Count -gt 0) {
    if ($KillExisting) {
        Write-Host "Terminating $($running.Count) existing VotV process(es) per -KillExisting..." -ForegroundColor Yellow
        $running | Stop-Process -Force
        Start-Sleep -Milliseconds 800
    } else {
        Write-Warning "Found $($running.Count) running VotV process(es). Add -KillExisting to stop them automatically."
        $choice = Read-Host "Kill existing VotV processes now? (Y/N)"
        if ($choice -match '^[Yy]') {
            $running | Stop-Process -Force
            Start-Sleep -Milliseconds 800
        } else {
            throw "Aborted: cannot launch test while previous instance is running."
        }
    }
}

function Build-LaunchArgs([string]$profilePath, [int]$winX, [int]$winY, [bool]$noSound) {
    $modDir = (Join-Path $profilePath 'shimloader\mod').Replace('\', '/')
    $pakDir = (Join-Path $profilePath 'shimloader\pak').Replace('\', '/')
    $cfgDir = (Join-Path $profilePath 'shimloader\cfg').Replace('\', '/')
    $overlayDir = (Join-Path $profilePath 'shimloader\overlay').Replace('\', '/')

    $argsList = @(
        '--mod-dir', "`"$modDir`"",
        '--pak-dir', "`"$pakDir`"",
        '--cfg-dir', "`"$cfgDir`"",
        '--overlay-dir', "`"$overlayDir`"",
        '-WINDOWED',
        "-ResX=$ResX",
        "-ResY=$ResY",
        "-WinX=$winX",
        "-WinY=$winY"
    )
    if ($noSound) {
        $argsList += '-nosound'
    }
    return ($argsList -join ' ')
}

# The mod expects a filename of at most 63 characters. Use a fresh pair
# for each run so the game preserves earlier test logs.
$logRun = "$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')-$PID"
$previousLog = $env:VOTVCOOP_LOG

# 2. Launch Host
if ($Mode -in @('Both', 'Host')) {
    Write-Host "`n=== Launching Host (Profile: Default, Pos: 10,20) ===" -ForegroundColor Cyan
    $hostArgs = Build-LaunchArgs -profilePath $profileDefault -winX 10 -winY 20 -noSound $false
    $hostLog = "multivoid.host.$logRun.log"
    try {
        $env:VOTVCOOP_LOG = $hostLog
        $hostProc = Start-Process -FilePath $gameExe -ArgumentList $hostArgs -PassThru
    } finally {
        $env:VOTVCOOP_LOG = $previousLog
    }
    Write-Host "Host process started (PID $($hostProc.Id), log $hostLog)." -ForegroundColor Green
}

# 3. Launch Client
if ($Mode -in @('Both', 'Client')) {
    if ($Mode -eq 'Both') {
        Write-Host "Waiting 5 seconds for Host engine initialization..." -ForegroundColor Gray
        Start-Sleep -Seconds 5
    }
    Write-Host "`n=== Launching Client (Profile: Client1, Pos: 1280,20, -nosound) ===" -ForegroundColor Cyan
    $clientArgs = Build-LaunchArgs -profilePath $profileClient1 -winX 1280 -winY 20 -noSound $true
    $clientLog = "multivoid.client.$logRun.log"
    try {
        $env:VOTVCOOP_LOG = $clientLog
        $clientProc = Start-Process -FilePath $gameExe -ArgumentList $clientArgs -PassThru
    } finally {
        $env:VOTVCOOP_LOG = $previousLog
    }
    Write-Host "Client process started (PID $($clientProc.Id), log $clientLog)." -ForegroundColor Green
}

Write-Host "`n========================================================" -ForegroundColor Yellow
Write-Host " Local Two-Player Co-op Test is Running! " -ForegroundColor Yellow
Write-Host " Instructions:" -ForegroundColor White
Write-Host "   1. In LEFT window (Host): Start Game or Load Story Save." -ForegroundColor White
Write-Host "   2. In RIGHT window (Client): Open Multivoid Menu -> Direct Connect." -ForegroundColor White
Write-Host "      Enter 127.0.0.1:47621 and click Join." -ForegroundColor White
Write-Host "   3. To close all instances when done, run:" -ForegroundColor White
Write-Host "      .\agent\test_local_coop.ps1 -Mode Kill" -ForegroundColor White
Write-Host "========================================================`n" -ForegroundColor Yellow
