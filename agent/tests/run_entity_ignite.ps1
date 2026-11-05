param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/entity-combined-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/player/player_damage.cpp') -Raw
$begin=$source.IndexOf('void DeliverIgnite(const sg::Call& c) {');$end=$source.IndexOf('// CLIENT: a hit',$begin)
$body=$source.Substring($begin,$end-$begin)
function Check([string]$name,[string]$code,[bool]$fault){
 [IO.File]::WriteAllText((Join-Path $out 'entity_ignite.inc'),$code)
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/I$root/src/votv-coop/include" "/Fo$out/$name.obj" "/Fe$out/$name.exe" (Join-Path $PSScriptRoot 'entity_ignite_test.cpp')
 if($LASTEXITCODE){throw 'Ignite fixture compile failed'}
 $result=& (Join-Path $out "$name.exe") | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out "$name.log"),$result)
 if($fault){if(!$status -or $result -notmatch 'FAIL '){throw "Fault was not rejected: $name"};Write-Output "PASS rejected fault: $name"}
 else {if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'ignite-production' $body $false
 if($FaultControls){Check 'ignite-double' ($body.Replace('sg::MarkScope(c,mark);','')) $true}
}finally{[IO.File]::WriteAllText((Join-Path $out 'entity_ignite.inc'),$body)}
