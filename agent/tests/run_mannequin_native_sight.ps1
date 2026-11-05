param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/mannequin-native-sight-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/ue_wrap/actors/mannequin.cpp') -Raw
$start=$source.IndexOf('namespace ue_wrap::mannequin {');if($start -lt 0){throw 'Wrapper boundary changed'}
$body=$source.Substring($start);$include=Join-Path $out 'mannequin_native_sight.inc'
function Check([string]$name,[string]$code,[bool]$negative){
 [IO.File]::WriteAllText($include,$code)
 $exe=Join-Path $out ($name+'.exe')
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'mannequin_native_sight_test.cpp')
 if($LASTEXITCODE){throw 'Native sight fixture compile failed'}
 $result=& $exe | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($status -eq 0 -or $result -notmatch 'FAIL '){throw "Fault control did not fail: $name"};Write-Output "PASS rejected native sight fault: $name"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'production' $body $false
 if($FaultControls){
  Check 'recent-render-ignored' ($body.Replace('seen=inFrustum&&recent;','seen=inFrustum;')) $true
  Check 'fixed-bounds' ($body.Replace('origin)||!frustum.Set(fields[2].name.c_str(),extent)','FVector{})||!frustum.Set(fields[2].name.c_str(),FVector{})')) $true
 }
}finally{[IO.File]::WriteAllText($include,$body)}
