param([switch]$FaultControls)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'build/roomba-sync-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { . (Join-Path $root '../_toolchain/env.ps1') }
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { throw 'MSVC toolchain unavailable.' }
$wrapper = [IO.File]::ReadAllText((Join-Path $root 'src/votv-coop/src/ue_wrap/devices/roomba.cpp')).Replace("`r`n", "`n")
$sync = [IO.File]::ReadAllText((Join-Path $root 'src/votv-coop/src/coop/props/roomba_sync.cpp')).Replace("`r`n", "`n")
$fixture = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'roomba_sync_test.cpp'))

function Check([string]$name, [string]$wrapperSource, [string]$syncSource, [bool]$fault) {
    $dir = Join-Path $out $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'wrapper.cpp'), $wrapperSource)
    [IO.File]::WriteAllText((Join-Path $dir 'sync.cpp'), $syncSource)
    $test = $fixture.Replace('../../src/votv-coop/src/ue_wrap/devices/roomba.cpp', 'wrapper.cpp').Replace('../../src/votv-coop/src/coop/props/roomba_sync.cpp', 'sync.cpp')
    [IO.File]::WriteAllText((Join-Path $dir 'test.cpp'), $test)
    & cl /nologo /std:c++20 /EHsc /Gy /utf-8 "/I$dir" "/I$PSScriptRoot/roomba_stubs" "/I$PSScriptRoot/stubs" "/I$root/src/votv-coop/include" "/Fo$dir/test.obj" "/Fe$dir/test.exe" (Join-Path $dir 'test.cpp') /link /OPT:REF
    if ($LASTEXITCODE) { throw "Compile failed: $name" }
    $result = & (Join-Path $dir 'test.exe') | Out-String
    $status = $LASTEXITCODE
    if ($fault) {
        if (!$status -or $result -notmatch 'FAIL ') { throw "Fault survived: $name" }
        Write-Output "PASS rejected fault: $name"
    } else {
        if ($status) { throw $result }
        Write-Output $result.Trim()
    }
}
Check 'production' $wrapper $sync $false
& cl /nologo /std:c++20 /EHsc /Gy /utf-8 "/I$PSScriptRoot/stubs" "/I$root/src/votv-coop/include" "/I$root/src/votv-coop/src" "/Fo$out/integration.obj" "/Fe$out/integration.exe" (Join-Path $PSScriptRoot 'roomba_drive_integration_test.cpp') /link /OPT:REF
if ($LASTEXITCODE) { throw 'Roomba drive integration fixture compilation failed' }
& (Join-Path $out 'integration.exe')
if ($LASTEXITCODE) { throw 'Roomba drive integration fixture failed' }
if ($FaultControls) {
    $cases = @(
        @{Name='wrong-angle-branch'; Which='wrapper'; Before='*target = current.Yaw - diff;'; After='*target += 360.0f;'},
        @{Name='ignore-movement-state'; Which='wrapper'; Before='if (!*reinterpret_cast<const bool*>(base + g_rotatOff) &&' + "`n" + '        !*reinterpret_cast<const bool*>(base + g_movOff))'; After='if (!*reinterpret_cast<const bool*>(base + g_rotatOff))'},
        @{Name='unconditional-client-torque'; Which='sync'; Before='!net_pump::IsInAnnouncedWorld(source) ||' + "`n" + '        !prop_drive_stream::IsParked(source)'; After='!net_pump::IsInAnnouncedWorld(source)'},
        @{Name='missing-host-stream'; Which='sync'; Before='prop_drive_host::Coast(c.object, "roomba");'; After='(void)c;'},
        @{Name='held-host-prop'; Which='sync'; Before='!ue_wrap::roomba::IsActive(c.object) || local_prop_hold::IsHeld(c.object) ||'; After='!ue_wrap::roomba::IsActive(c.object) ||'},
        @{Name='foreign-torque-owner'; Which='sync'; Before='if (!owner || !R::NameEquals(R::NameOf(owner), L"prop_roomba_C")) return SG::Verdict::Run;'; After='(void)owner;'}
    )
    foreach ($case in $cases) {
        $w = $wrapper; $s = $sync
        if ($case.Which -eq 'wrapper') {
            if (!$w.Contains($case.Before)) { throw "Missing fault target: $($case.Name)" }
            $w = $w.Replace($case.Before, $case.After)
        } else {
            if (!$s.Contains($case.Before)) { throw "Missing fault target: $($case.Name)" }
            $s = $s.Replace($case.Before, $case.After)
        }
        Check $case.Name $w $s $true
    }
}
