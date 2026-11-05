param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/entity-combined-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/piramid_sync.cpp') -Raw
$begin=$source.IndexOf('struct PendingGather {');$end=$source.IndexOf('PendingGather g_pending;',$begin)
[IO.File]::WriteAllText((Join-Path $out 'piramid_pending.inc'),$source.Substring($begin,$end-$begin))
$begin=$source.IndexOf('void* MemberObject(');$end=$source.IndexOf('// Host, 1 Hz:',$begin)
$body=$source.Substring($begin,$end-$begin)
function Check([string]$name,[string]$code,[bool]$fault){
 [IO.File]::WriteAllText((Join-Path $out 'piramid_playback.inc'),$code)
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/I$root/src/votv-coop/include" "/Fo$out/$name.obj" "/Fe$out/$name.exe" (Join-Path $PSScriptRoot 'piramid_playback_test.cpp')
 if($LASTEXITCODE){throw 'Playback fixture compile failed'}
 $result=& (Join-Path $out "$name.exe") | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out "$name.log"),$result)
 if($fault){if(!$status -or $result -notmatch 'FAIL '){throw "Fault was not rejected: $name"};Write-Output "PASS rejected fault: $name"}
 else {if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'piramid-playback' $body $false
 if($FaultControls){
  Check 'piramid-playback-rate' ($body.Replace('!rate.Set(L"NewPlayRate",pending.state.playRate)','!rate.Set(L"NewPlayRate",1.f)')) $true
  Check 'piramid-playback-target' ($body.Replace('if (tail) *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(actor) + g_offWispTarget) = nullptr;','')) $true
  Check 'piramid-playback-child' ($body.Replace('if (!timelines[i].Get() || MemberObject(actor,kTimelines[i])!=timelines[i].Get()) return false;','if (false) return false;')) $true
  Check 'piramid-playback-binding' ($body.Replace('currentAnimation!=animation.Get()','false')) $true
  Check 'piramid-playback-query-context' ($body.Replace('if (!contextValid() || currentAnimation!=animation.Get())','if (currentAnimation!=animation.Get())')) $true
  Check 'piramid-playback-active' ($body.Replace('if (g_pending.hasState) {','if (false) {')) $true
  Check 'piramid-playback-active-notifies' ($body.Replace('RestoreGatherState(pyr,snapshot,false,false)','RestoreGatherState(pyr,snapshot,false,true)')) $true
  Check 'piramid-playback-active-target' ($body.Replace('if (target!=wisp) return;','')) $true
  Check 'piramid-playback-duplicate' ($body.Replace('const bool reuseTail = tail && g_tailActor.Is(actor) && g_tailAnimation.Is(anim) && g_tailMontage.Get();','const bool reuseTail = false;')) $true
  Check 'piramid-playback-animation' ($body.Replace('asset.Get() && animation.Get() &&','asset.Get() && true &&')) $true
 }
}finally{[IO.File]::WriteAllText((Join-Path $out 'piramid_playback.inc'),$body)}
