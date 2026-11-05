param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath((Join-Path $root '../mannequin-native-sight/interaction'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/mannequin_interaction.cpp') -Raw
$start=$source.IndexOf('namespace coop::mannequin_interaction {')
if($start -lt 0){throw 'Production namespace boundary changed.'}
$body=$source.Substring($start)
$include=Join-Path $out 'mannequin_interaction_controller.inc'
function Check([string]$name,[string]$code,[bool]$negative){
 [IO.File]::WriteAllText($include,$code,[Text.UTF8Encoding]::new($false))
 $exe=Join-Path $out ($name+'.exe')
 $compile=& cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'mannequin_interaction_test.cpp') 2>&1 | Out-String
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
   'physics-admission-missing'=@('if(void* result=H::CurrentResult())*static_cast<bool*>(result)=true;','');
   'native-body-wake-missing'=@('prop_park::Unpark(actor,component,true);','');
   'ordinary-mannequin-admitted'=@('L"prop_wMannequin_C"','L"prop_mannequin_C"');
   'mod-replay-admitted'=@('!R::InCoopDispatch()&&','');
   'foreign-handle-admitted'=@('||R::OuterOf(context)!=g_player.Get()','');
   'frozen-admitted'=@('||P::IsFrozen(actor)','');
   'remote-hand-admitted'=@('||remote_prop::IsActorUnderAnyDrive(actor)','');
   'source-guard-missing'=@('if(!propHover&&!PlayerCall(source,frame.function))return;','')
  }
  foreach($name in $mutations.Keys){$pair=$mutations[$name];if(-not $body.Contains($pair[0])){throw "Fault site changed: $name"};Check $name ($body.Replace($pair[0],$pair[1])) $true}
 }
}finally{[IO.File]::WriteAllText($include,$body,[Text.UTF8Encoding]::new($false))}
