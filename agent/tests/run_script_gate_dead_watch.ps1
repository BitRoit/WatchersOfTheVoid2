param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/script-gate-dead-watch-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$core=Join-Path $root 'src/votv-coop/src/ue_wrap/core'
$registry=Get-Content -LiteralPath (Join-Path $core 'script_gate_registry.cpp') -Raw
$names=Get-Content -LiteralPath (Join-Path $core 'script_gate_names.cpp') -Raw
$fixture=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'script_gate_dead_watch_test.cpp') -Raw
function Check([string]$name,[string]$r,[string]$n,[bool]$fault){
 $dir=Join-Path $out $name;New-Item -ItemType Directory -Force -Path $dir | Out-Null
 Copy-Item -LiteralPath (Join-Path $core 'script_gate_detail.h') -Destination $dir -Force
 [IO.File]::WriteAllText((Join-Path $dir 'script_gate_registry.cpp'),$r)
 [IO.File]::WriteAllText((Join-Path $dir 'script_gate_names.cpp'),$n)
 $test=$fixture.Replace('../../src/votv-coop/src/ue_wrap/core/script_gate_registry.cpp','script_gate_registry.cpp').Replace('../../src/votv-coop/src/ue_wrap/core/script_gate_names.cpp','script_gate_names.cpp')
 foreach($unit in @('deer_sync','centipede_sync')){$test=$test.Replace("../../src/votv-coop/src/coop/creatures/$unit.cpp",(Join-Path $root "src/votv-coop/src/coop/creatures/$unit.cpp").Replace('\','/'))}
 [IO.File]::WriteAllText((Join-Path $dir 'test.cpp'),$test)
 & cl /nologo /std:c++20 /EHsc /Gy /utf-8 "/I$dir" "/I$root/src/votv-coop/include" "/Fo$dir/test.obj" "/Fe$dir/test.exe" (Join-Path $dir 'test.cpp') /link /OPT:REF
 if($LASTEXITCODE){throw 'Name watch fixture compile failed'}
 $result=& (Join-Path $dir 'test.exe') | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $dir 'test.log'),$result)
 if($fault){if(!$status -or $result -notmatch 'FAIL '){throw "Fault was not rejected: $name"};Write-Output "PASS rejected dead-watch fault: $name"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
Check 'production' $registry $names $false
if($FaultControls){
 Check 'dead-name-retry' $registry ($names.Replace('if (e.dead.load(std::memory_order_relaxed)) return false;','if (e.dead.load(std::memory_order_relaxed)) continue;')) $true
 Check 'dead-key-retry' ($registry.Replace('if (e.dead.load(std::memory_order_relaxed)) return false;','if (e.dead.load(std::memory_order_relaxed)) continue;')) $names $true
 Check 'literal-query' $registry ($names.Replace('!SameLiteral(e.name,name) || !SameLiteral(e.className,className)','e.name != name || e.className != className')) $true
}
