param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/mannequin-sync-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/mannequin_sync.cpp') -Raw
$start=$source.IndexOf('namespace coop::mannequin_sync {')
if($start -lt 0){throw 'Production namespace boundary changed.'}
$body=$source.Substring($start)
$include=Join-Path $out 'mannequin_controller.inc'
function Check([string]$name,[string]$code,[bool]$negative){
 [IO.File]::WriteAllText($include,$code,[Text.UTF8Encoding]::new($false))
 $exe=Join-Path $out ($name+'.exe')
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'mannequin_sync_test.cpp')
 if($LASTEXITCODE){throw 'Mannequin fixture compile failed.'}
 $result=& $exe | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($status -eq 0 -or $result -notmatch 'FAIL '){throw "Fault control did not fail: $name"};Write-Output "PASS rejected fault: $name"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'production' $body $false
 if($FaultControls){
  Check 'invalid-native-destroy-watch' ($body.Replace('void Install(net::Session* session) {','void Install(net::Session* session) { SG::WatchClassName(L"prop_wMannequin_C", L"K2_DestroyActor", 0, &OnClientWalkerTick, nullptr);')) $true
  $carry='if (coop::local_prop_hold::IsHeld(walker) || coop::prop_drive_host::InAHand(walker)) {'
  if(-not $body.Contains($carry)){throw 'Carry control site changed.'}
  Check 'carry-ignored' ($body.Replace($carry,'if (false) {')) $true
  $raw=$body.Replace('if (!state.actor.Is(walker)) {','if (!state.haveSample) {').Replace('if (!it->second.actor.Alive())','if (!it->first || !R::IsLive(it->first))')
  Check 'raw-pointer-history' $raw $true
  Check 'origin-as-uninitialized' ($body.Replace('if (state.haveSample) {','if (state.lastPos.X != 0 || state.lastPos.Y != 0 || state.lastPos.Z != 0) {')) $true
  Check 'borrowed-sight-sticks' ($body.Replace('SetWalkerBool(actor, L"saw", false);','// broken: borrowed sight is never restored')) $true
  Check 'native-hesitation-cleared' ($body.Replace('EnsureWalkerEnrolled(walker, session);','EnsureWalkerEnrolled(walker, session); SetWalkerBool(walker, L"saw", false); SetWalkerBool(walker, L"looking", false);')) $true
  Check 'client-native-sight-ignored' ($body.Replace('if (coop::mannequin_sight_sync::TryGetRemoteSight(slot, walker, seen)) {','if (false) {')) $true
  Check 'host-native-stand-ignored' ($body.Replace('if (coop::mannequin_sight_sync::TryGetHostStand(walker, stand)) {','if (false) {')) $true
 }
} finally {[IO.File]::WriteAllText($include,$body,[Text.UTF8Encoding]::new($false))}
