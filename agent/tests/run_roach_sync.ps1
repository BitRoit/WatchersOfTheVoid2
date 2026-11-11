param([switch]$FaultControls, [string]$NativeCapture)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root '../_toolchain/env.ps1')
$out = Join-Path $root 'build/roach-sync-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$source = Get-Content -Raw (Join-Path $root 'src/votv-coop/src/coop/creatures/roach_sync.cpp')
function RunCheck([string]$name, [string]$body, [bool]$negative) {
    $dir = Join-Path $out $name
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'roach_source.inc'), $body, [Text.UTF8Encoding]::new($false))
    & cl /nologo /std:c++20 /EHsc /utf-8 "/I$dir" "/I$PSScriptRoot/roach_stubs" "/I$root/src/votv-coop/include" `
        "/Fo$dir/roach_sync_test.obj" "/Fe$dir/roach_sync_test.exe" "$PSScriptRoot/roach_sync_test.cpp"
    if ($LASTEXITCODE) { throw "Roach fixture compile failed: $name" }
    $ErrorActionPreference = 'Continue'
    $result = & "$dir/roach_sync_test.exe" 2>&1 | Out-String
    $status = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($negative) {
        if ($status -ne 1 -or $result -notmatch 'FAIL line') { throw "Fault was not rejected: $name`n$result" }
        Write-Output "PASS rejected fault: $name"
    } else {
        if ($status) { throw "Roach production fixture failed: $result" }
        Write-Output $result.Trim()
    }
}
RunCheck 'production' $source $false
if ($FaultControls) {
    $old = @'
        if (cur[i].loc.x != g_lastSent[i].loc.x ||
            cur[i].loc.y != g_lastSent[i].loc.y ||
            cur[i].loc.z != g_lastSent[i].loc.z) return true;
'@
    $old = $old.Replace("`r`n", "`n")
    $source = $source.Replace("`r`n", "`n")
    $mutations = @(
        @{Name='slow-motion-gate'; Old=$old; New='        if (Dist2(cur[i].loc, g_lastSent[i].loc) > 625.f) return true;'},
        @{Name='nearby-birth-steals-survivor'; Old='if (m >= n) {'; New='if (false && m >= n) {'},
        @{Name='lost-local-consumption'; Old='if (!r.consumed && r.comp.Raw() && !r.comp.Alive())'; New='if (false && !r.consumed && r.comp.Raw() && !r.comp.Alive())'},
        @{Name='unbounded-births'; Old='kBirthAttemptsPerSecond = 8;'; New='kBirthAttemptsPerSecond = 999;'},
        @{Name='raw-component-reuse'; Old='void* comp = r.comp.Get();'; New='void* comp = r.comp.Raw();'},
        @{Name='unverified-native-add'; Old='if (additions == 1 && after.size() == before.size() + 1) PrimeRoach(r, added, e);'; New='if (after.size() >= before.size()) PrimeRoach(r, added, e);'},
        @{Name='reversal-from-display'; Old='const float dx = want.x - previousHost.x;'; New='const float dx = want.x - r.curPos.x;'},
        @{Name='long-yaw-wrap'; Old='r.errorYaw = std::remainder(r.targetYaw - r.curYaw, 360.f);'; New='r.errorYaw = r.targetYaw - r.curYaw;'},
        @{Name='flatten-surface'; Old='FRotator3{r.pitch, r.curYaw, r.roll}'; New='FRotator3{0.f, r.curYaw, 0.f}'},
        @{Name='unsafe-rotation-param'; Old='f.SetRaw(L"NewRotation", &rot, sizeof(rot)) &&'; New='true &&'},
        @{Name='transient-master-reset'; Old='if (!g_trackedMaster.Alive()) ClearClientState();'; New='ClearClientState();'},
        @{Name='no-jitter-cushion'; Old='static_cast<int>(kPollIntervalMs + kInterpCushionMs)'; New='static_cast<int>(kPollIntervalMs)'}
    )
    foreach ($mutation in $mutations) {
        if (-not $source.Contains($mutation.Old)) { throw "Missing mutation site: $($mutation.Name)" }
        RunCheck $mutation.Name ($source.Replace($mutation.Old, $mutation.New)) $true
    }
}
if ($NativeCapture) {
    & python "$PSScriptRoot/roach_native_test.py" $NativeCapture
    if ($LASTEXITCODE) { throw 'Native roach contract failed' }
}
