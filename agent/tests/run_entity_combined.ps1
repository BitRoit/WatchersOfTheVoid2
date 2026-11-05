param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/entity-combined-check'
$inc=Join-Path $out 'include/coop/creatures'
New-Item -ItemType Directory -Force -Path $inc | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$header=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/include/coop/creatures/npc_spawn_pending.h') -Raw
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/creature_hit.cpp') -Raw
$index=$source.IndexOf('namespace coop::creature_hit {')
if($index -lt 0){throw 'Production damage namespace changed'}
$body=$source.Substring($index)
function Check([string]$name,[string]$h,[string]$cpp,[bool]$fault){
 [IO.File]::WriteAllText((Join-Path $inc 'npc_spawn_pending.h'),$h)
 [IO.File]::WriteAllText((Join-Path $out 'creature_hit.inc'),$cpp)
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out/include" "/I$out" "/I$root/src/votv-coop/include" "/Fo$out/$name.obj" "/Fe$out/$name.exe" (Join-Path $PSScriptRoot 'entity_combined_test.cpp')
 if($LASTEXITCODE){throw 'Entity fixture compile failed'}
 $result=& (Join-Path $out "$name.exe") | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out "$name.log"),$result)
 if($fault){if(!$status -or $result -notmatch 'FAIL '){throw "Fault was not rejected: $name"};Write-Output "PASS rejected fault: $name"}
 else {if($status){throw $result};Write-Output $result.Trim()}
}
try {
 Check 'production' $header $body $false
 if($FaultControls){
  Check 'unbounded' ($header.Replace('rows_.size() >= kLimit','false')) $body $true
  Check 'no-expiry' ($header.Replace('return now >= p.expires;','return false;')) $body $true
  Check 'stale-pose' ($header.Replace('row->spawn.locX = pose.x;','row->spawn.locX = 0;')) $body $true
  Check 'duplicate-damage' $header ($body.Replace('return R::NameEquals(name, L"hoelUfoAttack_C") ||','return true || R::NameEquals(name, L"hoelUfoAttack_C") ||')) $true
 }
}finally{
 [IO.File]::WriteAllText((Join-Path $inc 'npc_spawn_pending.h'),$header)
 [IO.File]::WriteAllText((Join-Path $out 'creature_hit.inc'),$body)
}
