param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/atv-planar-velocity-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=[IO.File]::ReadAllText((Join-Path $root 'src/votv-coop/src/ue_wrap/devices/atv.cpp'))
$match=[regex]::Match($source,'(?ms)^bool MatchRigPlanarVelocity\(.*?^\}')
if(-not $match.Success){throw 'Production planar wrapper boundary changed.'}
$body=$match.Value;$inc=Join-Path $out 'atv_planar_velocity.inc'
function Check($name,$code,$negative){
 [IO.File]::WriteAllText($inc,$code,[Text.UTF8Encoding]::new($false))
 $exe=Join-Path $out ($name+'.exe')
 & cl /nologo /std:c++20 /EHsc /utf-8 /I (Join-Path $root 'src/votv-coop/include') "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'atv_planar_velocity_test.cpp')
 if($LASTEXITCODE){throw 'Planar wrapper fixture compile failed.'}
 $result=& $exe | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($status -eq 0 -or $result -notmatch 'FAIL '){throw "Mutation survived: $name"};Write-Output "PASS rejected fault: $name"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'production' $body $false
 if($FaultControls){
  Check 'vertical-zeroed' ($body.Replace('velocities[i].X+=delta.X;velocities[i].Y+=delta.Y;','velocities[i].X+=delta.X;velocities[i].Y+=delta.Y;velocities[i].Z=0;')) $true
  Check 'relative-motion-lost' ($body.Replace('velocities[i].X+=delta.X;velocities[i].Y+=delta.Y;','velocities[i].X=desired.X;velocities[i].Y=desired.Y;')) $true
  Check 'root-only' ($body.Replace('for(int j=0;j<4;++j)if(!parts[j].Get()','if(i>0)break; for(int j=0;j<4;++j)if(!parts[j].Get()')) $true
 }
}finally{[IO.File]::WriteAllText($inc,$body,[Text.UTF8Encoding]::new($false))}
