<#
.SYNOPSIS
    Single-Machine Dual-Instance Test Harness for Watchers of the Void (Multivoid).
.DESCRIPTION
    Launches two instances of VotV on the local machine side-by-side:
      - Instance 1 (Host): Uses profile 'Default' on the left half of the display.
      - Instance 2 (Client): Uses profile 'Client1' on the right half with -nosound, pre-configured to connect to 127.0.0.1:47621.
    Eliminates the need to package ZIPs and copy them to a secondary laptop for multiplayer testing.
.PARAMETER Mode
    'Both' (default), 'Host', 'Client', or 'Kill' (terminates running VotV instances).
.PARAMETER KillExisting
    Automatically terminates any running VotV processes before launch.
.PARAMETER SyncDll
    Copies the latest build\votv-coop\Release\main.dll into both profiles before launch.
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

    [switch]$SyncDll = $true,

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

# 2. Synchronize Release main.dll to both profiles if requested
if ($SyncDll) {
    $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
    $repoRoot = Split-Path -Parent $scriptDir
    $builtDll = Join-Path $repoRoot 'build\votv-coop\Release\main.dll'
    if (Test-Path -LiteralPath $builtDll) {
        $builtHash = (Get-FileHash -LiteralPath $builtDll -Algorithm SHA256).Hash
        $targets = @(
            (Join-Path $profileDefault 'shimloader\mod\ElvardFIC-WatchersOfTheVoid\dlls\main.dll'),
            (Join-Path $profileClient1 'shimloader\mod\ElvardFIC-WatchersOfTheVoid\dlls\main.dll')
        )
        foreach ($target in $targets) {
            $targetHash = if (Test-Path -LiteralPath $target) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash } else { '' }
            if ($targetHash -ne $builtHash) {
                Write-Host "Deploying built main.dll ($($builtHash.Substring(0,8))...) -> $target" -ForegroundColor Cyan
                Copy-Item -LiteralPath $builtDll -Destination $target -Force
            }
        }
    } else {
        Write-Host "Note: build/votv-coop/Release/main.dll not found; using existing deployed profile DLLs." -ForegroundColor Gray
    }
}

# 3. Configure multivoid.ini for local loopback connection and perf_probe
$iniFiles = @(
    (Join-Path $profileDefault 'shimloader\mod\ElvardFIC-WatchersOfTheVoid\multivoid.ini'),
    (Join-Path $profileClient1 'shimloader\mod\ElvardFIC-WatchersOfTheVoid\multivoid.ini')
)
foreach ($iniPath in $iniFiles) {
    if (Test-Path -LiteralPath $iniPath) {
        $content = Get-Content -LiteralPath $iniPath -Raw
        $mod = $false
        if ($iniPath -eq $iniFiles[1]) {
            if ($content -match 'browser\.lastdirect=.*') {
                $newContent = $content -replace 'browser\.lastdirect=.*', 'browser.lastdirect=127.0.0.1:47621'
                if ($newContent -ne $content) { $content = $newContent; $mod = $true }
            } else {
                $content += "`r`nbrowser.lastdirect=127.0.0.1:47621`r`n"
                $mod = $true
            }
        }
        foreach ($prop in @('perf_probe=0', 'perf_probe_dispatch=0', 'perf_probe_selftime=0')) {
            $name = $prop.Split('=')[0]
            if ($content -match "$name=.*") {
                $newContent = $content -replace "$name=.*", $prop
                if ($newContent -ne $content) { $content = $newContent; $mod = $true }
            } else {
                if ($content -match '\[dev\]') {
                    $content = $content -replace '\[dev\]', "[dev]`r`n$prop"
                } else {
                    $content += "`r`n[dev]`r`n$prop`r`n"
                }
                $mod = $true
            }
        }
        if ($mod) {
            Set-Content -LiteralPath $iniPath -Value $content -Encoding UTF8
            Write-Host "Configured $iniPath (release perf_probe=0)" -ForegroundColor Green
        }
    }
}

# 4. Ensure background throttling is disabled so the unfocused window runs at full FPS
$localEngineIni = Join-Path $env:LOCALAPPDATA 'VotV\Saved\Config\WindowsNoEditor\Engine.ini'
if (Test-Path -LiteralPath $localEngineIni) {
    $iniText = Get-Content -LiteralPath $localEngineIni -Raw
    $toAppend = ""
    if ($iniText -notmatch '\[ApplicationCore\]') {
        $toAppend += "`r`n[ApplicationCore]`r`nbThrottleCPUWhenNotForeground=False`r`n"
    }
    if ($iniText -notmatch 't\.IdleWhenNotForeground') {
        $toAppend += "`r`n[SystemSettings]`r`nt.IdleWhenNotForeground=0`r`n"
    }
    if ($toAppend) {
        Add-Content -LiteralPath $localEngineIni -Value $toAppend -Encoding UTF8
        Write-Host "Configured Engine.ini: background throttling disabled (unthrottled FPS)" -ForegroundColor Green
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
        "-WinY=$winY",
        '-ExecCmds="t.IdleWhenNotForeground 0; r.VSync 0; t.MaxFPS 0"'
    )
    if ($noSound) {
        $argsList += '-nosound'
    }
    return ($argsList -join ' ')
}

# 5. Launch Host
if ($Mode -in @('Both', 'Host')) {
    Write-Host "`n=== Launching Host (Profile: Default, Pos: 10,20) ===" -ForegroundColor Cyan
    $hostArgs = Build-LaunchArgs -profilePath $profileDefault -winX 10 -winY 20 -noSound $false
    $hostLog = Join-Path $profileDefault 'shimloader\mod\ElvardFIC-WatchersOfTheVoid\multivoid.host.log'
    try {
        $env:VOTVCOOP_LOG = $hostLog
        $hostProc = Start-Process -FilePath $gameExe -ArgumentList $hostArgs -PassThru
    } finally {
        $env:VOTVCOOP_LOG = $null
    }
    Write-Host "Host process started (PID: $($hostProc.Id))." -ForegroundColor Green
}

# 6. Launch Client
if ($Mode -in @('Both', 'Client')) {
    if ($Mode -eq 'Both') {
        Write-Host "Waiting 5 seconds for Host engine initialization..." -ForegroundColor Gray
        Start-Sleep -Seconds 5
    }
    Write-Host "`n=== Launching Client (Profile: Client1, Pos: 1280,20, -nosound) ===" -ForegroundColor Cyan
    $clientArgs = Build-LaunchArgs -profilePath $profileClient1 -winX 1280 -winY 20 -noSound $true
    $clientLog = Join-Path $profileClient1 'shimloader\mod\ElvardFIC-WatchersOfTheVoid\multivoid.client.log'
    try {
        $env:VOTVCOOP_LOG = $clientLog
        $clientProc = Start-Process -FilePath $gameExe -ArgumentList $clientArgs -PassThru
    } finally {
        $env:VOTVCOOP_LOG = $null
    }
    Write-Host "Client process started (PID: $($clientProc.Id))." -ForegroundColor Green
}

Write-Host "`n========================================================" -ForegroundColor Yellow
Write-Host " Local Two-Player Co-op Test is Running! " -ForegroundColor Yellow
Write-Host " Instructions:" -ForegroundColor White
Write-Host "   1. In LEFT window (Host): Start Game or Load Story Save." -ForegroundColor White
Write-Host "   2. In RIGHT window (Client): Open Multivoid Menu -> Direct Connect." -ForegroundColor White
Write-Host "      (Pre-filled with 127.0.0.1:47621, Nick: Player2) -> Click Join!" -ForegroundColor White
Write-Host "   3. To close all instances when done, run:" -ForegroundColor White
Write-Host "      .\agent\test_local_coop.ps1 -Mode Kill" -ForegroundColor White
Write-Host "========================================================`n" -ForegroundColor Yellow
