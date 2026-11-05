param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/entity-combined-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/piramid_sync.cpp') -Raw
$begin=$source.IndexOf('struct PendingGather {');$end=$source.IndexOf('PendingGather g_pending;',$begin)
[IO.File]::WriteAllText((Join-Path $out 'piramid_pending.inc'),$source.Substring($begin,$end-$begin))
$begin=$source.IndexOf('void OnPyramidGather(const ');$end=$source.IndexOf('bool DebugHooksArmed()',$begin)
$body=$source.Substring($begin,$end-$begin)
function Check([string]$name,[string]$code,[bool]$fault){
 [IO.File]::WriteAllText((Join-Path $out 'piramid_receive.inc'),$code)
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/I$root/src/votv-coop/include" "/Fo$out/$name.obj" "/Fe$out/$name.exe" (Join-Path $PSScriptRoot 'piramid_join_state_test.cpp')
 if($LASTEXITCODE){throw 'Pyramid receiver fixture compile failed'}
 $result=& (Join-Path $out "$name.exe") | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out "$name.log"),$result)
 if($fault){if(!$status -or $result -notmatch 'FAIL '){throw "Fault was not rejected: $name"};Write-Output "PASS rejected fault: $name"}
 else {if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'piramid-production' $body $false
 if($FaultControls){
  Check 'piramid-lost-tail' ($body.Replace('!(state.consumed && !state.gather.wispEid)','true')) $true
  Check 'piramid-lost-phase' ($body.Replace('g_pending.state=state;','g_pending.state={};')) $true
  Check 'piramid-invalid-flags' ($body.Replace('(state.timelineFlags & ~uint8_t{63})','false')) $true
 }
}finally{[IO.File]::WriteAllText((Join-Path $out 'piramid_receive.inc'),$body)}
