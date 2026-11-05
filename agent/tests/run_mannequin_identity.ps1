param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath((Join-Path $root '../mannequin-native-sight/identity'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/mannequin_identity.cpp') -Raw
$start=$source.IndexOf('namespace coop::mannequin_identity {')
if($start -lt 0){throw 'Production namespace boundary changed.'}
$body=$source.Substring($start)
$include=Join-Path $out 'mannequin_identity_controller.inc'
$streams=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/player/local_streams.cpp') -Raw
$edgeStart=$streams.IndexOf('uint32_t canonicalWalkerEid=0;std::wstring canonicalWalkerKey;')
$edgeEnd=$streams.IndexOf('// The host''s grab adoption:',$edgeStart)
$eidStart=$streams.IndexOf('g_lastHeldEid = canonicalWalker ?',$edgeEnd)
$eidEnd=$streams.IndexOf(';',$eidStart)+1
$poseStart=$streams.IndexOf('std::wstring keyW = ue_wrap::prop::GetInteractableKeyString(heldActor);',$eidEnd)
$poseEnd=$streams.IndexOf('coop::net::PropPoseSnapshot pp{};',$poseStart)
if($edgeStart -lt 0 -or $edgeEnd -lt 0 -or $eidStart -lt 0 -or $poseStart -lt 0 -or $poseEnd -lt 0){throw 'Production held integration boundary changed.'}
[IO.File]::WriteAllText((Join-Path $out 'mannequin_held_edge.inc'),$streams.Substring($edgeStart,$edgeEnd-$edgeStart)+$streams.Substring($eidStart,$eidEnd-$eidStart),[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $out 'mannequin_held_pose.inc'),$streams.Substring($poseStart,$poseEnd-$poseStart),[Text.UTF8Encoding]::new($false))
function Check([string]$name,[string]$code,[bool]$negative){
 [IO.File]::WriteAllText($include,$code,[Text.UTF8Encoding]::new($false))
 $exe=Join-Path $out ($name+'.exe')
 $compile=& cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'mannequin_identity_test.cpp') 2>&1 | Out-String
 [IO.File]::WriteAllText((Join-Path $out ($name+'-compile.log')),$compile)
 if($LASTEXITCODE){throw $compile}
 $result=& $exe | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($status -eq 0 -or $result -notmatch 'FAIL '){throw "Fault control did not fail: $name"};Write-Output "PASS rejected fault: $name"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'production' $body $false
 if($FaultControls){
  $mutations=@{
   'same-key-twin-kept'=@('remote_prop::ConsumeLocalActor(actor);','');
   'held-twin-deleted'=@('if(Held(actor)){','if(false){');
   'different-key-twin-retired'=@('P::GetInteractableKeyString(actor)!=key','false');
   'different-class-twin-retired'=@('R::ClassOf(actor)!=R::ClassOf(row.actor.Raw())','false');
   'frozen-twin-retired'=@('P::IsFrozen(actor)||','');
   'held-alias-local-eid'=@('eid=row.eid;key=canonicalKey;return true;','eid=33887;key=canonicalKey;return true;');
   'canonical-flags-not-restored'=@('Restore(it->second);it=g_hidden.erase(it);','it=g_hidden.erase(it);');
   'unsafe-live-unmark'=@('if(!attempt.actor.Alive()){','if(true){')
  }
  foreach($name in $mutations.Keys){$pair=$mutations[$name];if(-not $body.Contains($pair[0])){throw "Fault site changed: $name"};Check $name ($body.Replace($pair[0],$pair[1])) $true}
 }
}finally{[IO.File]::WriteAllText($include,$body,[Text.UTF8Encoding]::new($false))}
