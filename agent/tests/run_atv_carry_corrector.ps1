param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/atv-carry-corrector-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/interactables/atv_corrector.cpp') -Raw
$start=$source.IndexOf('namespace coop::atv_corrector {')
if($start -lt 0){throw 'Production corrector namespace boundary changed.'}
$body=$source.Substring($start)
$include=Join-Path $out 'atv_corrector_body.inc'
function Check([string]$name,[string]$code,[bool]$negative,[string]$expectedFailure='HeldStill'){
 [IO.File]::WriteAllText($include,$code,[Text.UTF8Encoding]::new($false))
 $exe=Join-Path $out ($name+'.exe')
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'atv_carry_corrector_test.cpp')
 if($LASTEXITCODE){throw 'Corrector fixture compile failed.'}
 $result=& $exe | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($status -eq 0 -or $result -notmatch ('FAIL '+$expectedFailure)){throw "Fault $name was not rejected by $expectedFailure."};Write-Output "PASS fault rejected: $name ($expectedFailure)"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'production' $body $false
 if($FaultControls){
  $needle='const bool linAtRest = !carried && (Len(wireLin) <= kRestLinCmS || poseAtRest);'
  if(-not $body.Contains($needle)){throw 'Carry fault-control site changed.'}
  Check 'old-rest-gate' ($body.Replace($needle,'const bool linAtRest = Len(wireLin) <= kRestLinCmS;')) $true
  $pose='else poseAtRest = now - e.restAnchorSinceMs >= kRestPoseMs;'
  if(-not $body.Contains($pose)){throw 'Pose fault-control site changed.'}
  Check 'velocity-only-rest' ($body.Replace($pose,'else poseAtRest = false;')) $true 'ParkedSuspensionNoise'
  $anchor='const bool within = e.restAnchorSinceMs && withinAnchor;'
  if(-not $body.Contains($anchor)){throw 'Anchor fault-control site changed.'}
  Check 'travel-ignored' ($body.Replace($anchor,'const bool within = e.restAnchorSinceMs;')) $true 'SlowCoastAndDriver'
  $budget='if (e.restReplaces >= kRestMaxReplaces) return;'
  if(-not $body.Contains($budget)){throw 'Rest budget fault-control site changed.'}
  Check 'time-rearmed-rest' ($body.Replace($budget,'if (now-e.lastRestPlaceMs>10000) e.restReplaces=0; '+$budget)) $true 'ParkedProtection'
  Check 'idle-momentum-kept' ($body.Replace('if(idle&&!MatchIdlePlanar(e,p,idleMotion,now))return;','// old pose-only rest leaves mirror momentum intact')) $true 'UnbrakedPlanarDrift'
  Check 'wire-noise-copied' ($body.Replace('if(poseAtRest&&haveMotionSample){','if(false){')) $true 'RestPoseVelocityNoise'
  Check 'idle-cadence-capped' ($body.Replace('std::max(kCorrMinDtMs,std::min(rawDt,kRestPoseGapMs))','dtMs')) $true 'IdlePacketCadence'
 }
} finally {[IO.File]::WriteAllText($include,$body,[Text.UTF8Encoding]::new($false))}
