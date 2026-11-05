param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath((Join-Path $root '../mannequin-native-sight/network'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/mannequin_sight_sync.cpp') -Raw
$start=$source.IndexOf('namespace coop::mannequin_sight_sync {')
if($start -lt 0){throw 'Production namespace boundary changed.'}
$body=$source.Substring($start)
$include=Join-Path $out 'mannequin_sight_controller.inc'
function Check([string]$name,[string]$code,[bool]$negative){
 [IO.File]::WriteAllText($include,$code,[Text.UTF8Encoding]::new($false))
 $exe=Join-Path $out ($name+'.exe')
 $compile=& cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/I$root/src/votv-coop/include" "/I$root/src/votv-coop/src/coop/net" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'mannequin_sight_sync_test.cpp') 2>&1 | Out-String
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
   'client-load-gate-removed'=@('(slot==0&&net_pump::IsWorldSettled())','g_session->IsSlotWorldReady(slot)');
   'challenge-binding-removed'=@('p.challenge!=peer.challenge','false');
   'expiry-removed'=@('now-s.at<=limit','true');
   'generation-binding-removed'=@('g_peers[slot].generation==gen','true');
   'duplicate-key-check-removed'=@('||it->second.duplicate)','||false)');
   'native-key-check-removed'=@('current==key','true');
   'registry-binding-check-removed'=@('if(Host()){uint32_t eid=0;if(!HostBinding(actor,eid)||eid!=it->second.eid)return nullptr;}','');
   'unknown-is-false'=@('if(!row||!Fresh(row->remote[slot]))return false;','if(!row||!Fresh(row->remote[slot])){seen=false;return true;}')
   'fuzzy-native-key-assumed-canonical'=@('if(!ClientBinding(p.eid,key,actor))return;','if(!ClientBinding(p.eid,key,actor)||Unique(key)!=actor)return;');
   'client-mirror-requirement-removed'=@('entity&&entity->IsMirror()&&','entity&&true&&');
   'client-reverse-binding-removed'=@('registry.EidForActor(actor)==eid&&','true&&');
  }
  foreach($name in $mutations.Keys){$pair=$mutations[$name];if(-not $body.Contains($pair[0])){throw "Fault site changed: $name"};Check $name ($body.Replace($pair[0],$pair[1])) $true}
 }
}finally{[IO.File]::WriteAllText($include,$body,[Text.UTF8Encoding]::new($false))}
