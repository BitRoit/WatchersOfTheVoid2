param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/entity-combined-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/served_player.cpp') -Raw
$begin=$source.IndexOf('void UpdateCreatureTargets()');$end=$source.IndexOf('}  // namespace',$begin)
$body=$source.Substring($begin,$end-$begin)
function Check([string]$name,[string]$code,[bool]$fault){
 [IO.File]::WriteAllText((Join-Path $out 'creature_targets.inc'),$code)
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$out/$name.exe" (Join-Path $PSScriptRoot 'creature_target_test.cpp')
 if($LASTEXITCODE){throw 'Creature target fixture compile failed'}
 $result=& (Join-Path $out "$name.exe") | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out "$name.log"),$result)
 if($fault){if(!$status -or $result -notmatch 'FAIL '){throw "Fault was not rejected: $name"};Write-Output "PASS rejected fault: $name"}
 else {if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'target-production' $body $false
 if($FaultControls){
  Check 'target-host-only' ($body.Replace('slot < coop::players::kMaxPeers','slot < 1')) $true
  Check 'target-dead' ($body.Replace('peer->GetHealth() > 0','true')) $true
  Check 'target-no-hysteresis' ($body.Replace('if (std::isfinite(previous)','if (false && std::isfinite(previous)')) $true
 }
}finally{[IO.File]::WriteAllText((Join-Path $out 'creature_targets.inc'),$body)}
