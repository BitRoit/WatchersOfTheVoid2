param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$checkout=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $checkout 'build/atv-recovery-check'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $checkout
try {
    if(-not(Get-Command cl -ErrorAction SilentlyContinue)){. (Join-Path $checkout '../_toolchain/env.ps1')}
    function Extract($path,$names,$destination) {
        $source=[IO.File]::ReadAllText((Join-Path $checkout $path))
        $bodies=foreach($name in $names){
            $body=[regex]::Match($source,"(?ms)^(?:void\*|void|bool|FRotator) ${name}\(.*?^\}")
            if(-not $body.Success){throw "Production function missing: $name"}
            $body.Value
        }
        [IO.File]::WriteAllText((Join-Path $output $destination),($bodies -join "`r`n"),[Text.UTF8Encoding]::new($false))
    }
    Extract 'src/votv-coop/src/coop/interactables/atv_sync.cpp' @('EnsureHonkWatch','Install') 'atv_install.inc'
    Extract 'src/votv-coop/src/ue_wrap/devices/atv.cpp' @('TeleportRig','TeleportRigPreservingShape','SpawnMirror') 'atv_spawn.inc'
    Extract 'src/votv-coop/src/ue_wrap/engine/engine.cpp' @('RotatorToQuat','QuatToRotator') 'atv_engine_math.inc'
    foreach($name in @('atv_recovery','atv_spawn_preparation')){
        & cl /nologo /std:c++20 /EHsc /utf-8 /Gy /I agent/tests/state_2145_stubs /I src/votv-coop/include "/I$output" `
            "/Fo$output/$name.obj" "/Fe$output/$name.exe" "agent/tests/${name}_test.cpp" /link /OPT:REF
        if($LASTEXITCODE){throw "ATV compile failed: $name"}
        if($name -eq 'atv_recovery'){
            foreach($mode in @('metadata','capacity','frame')){
                & "$output/$name.exe" $mode
                if($LASTEXITCODE){throw "ATV recovery failed: $mode"}
            }
        }else{
            & "$output/$name.exe"
            if($LASTEXITCODE){throw "ATV regression failed: $name"}
        }
    }
    if($FaultControls){
        $include=Join-Path $output 'atv_spawn.inc'
        $original=[IO.File]::ReadAllText($include)
        $function=[regex]::Match($original,'(?ms)^bool TeleportRigPreservingShape\(.*?^\}')
        if(-not $function.Success){throw 'Advanced warp fault-control site changed.'}
        $mutant=$original.Replace($function.Value,'bool TeleportRigPreservingShape(void* atv,const FVector& loc,const FRotator& rot) {return TeleportRig(atv,loc,rot);}')
        try {
            [IO.File]::WriteAllText($include,$mutant,[Text.UTF8Encoding]::new($false))
            $exe=Join-Path $output 'atv_old_marker_fault.exe'
            & cl /nologo /std:c++20 /EHsc /utf-8 /Gy /I agent/tests/state_2145_stubs /I src/votv-coop/include "/I$output" "/Fo$output/atv_old_marker_fault.obj" "/Fe$exe" agent/tests/atv_spawn_preparation_test.cpp /link /OPT:REF
            if($LASTEXITCODE){throw 'Old-marker fault fixture failed to compile.'}
            $log=Join-Path $output 'old-marker-fault.log'
            # Capture the intentional CRT assertion without PowerShell treating
            # its stderr as a command failure before we can inspect the result.
            & cmd /c "`"$exe`" > `"$log`" 2>&1"
            if($LASTEXITCODE -eq 0 -or [IO.File]::ReadAllText($log) -notmatch 'calls\[3\]==2 && calls\[4\]==1 && calls\[2\]==0') {throw 'Old construction-marker warp was not rejected.'}
            Write-Output 'PASS fault rejected: old construction-marker teleport cannot replace Advanced articulated correction'
        } finally {[IO.File]::WriteAllText($include,$original,[Text.UTF8Encoding]::new($false))}
    }
    foreach($source in @('coop/interactables/atv_hit_guard','coop/interactables/atv_sync','ue_wrap/devices/atv')){
        $leaf=Split-Path -Leaf $source
        & cl /nologo /std:c++20 /EHsc /utf-8 /c /I src/votv-coop/include "/Fo$output/$leaf.obj" "src/votv-coop/src/$source.cpp"
        if($LASTEXITCODE){throw "ATV production compile failed: $source"}
    }
} finally {Pop-Location}
