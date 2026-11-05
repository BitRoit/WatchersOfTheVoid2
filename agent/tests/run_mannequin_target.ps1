param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/mannequin-target-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$controller=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/mannequin_sync.cpp') -Raw
$index=$controller.IndexOf('namespace coop::mannequin_sync {');if($index -lt 0){throw 'Controller boundary changed.'}
[IO.File]::WriteAllText((Join-Path $out 'mannequin_controller.inc'),$controller.Substring($index))
$source=Get-Content -LiteralPath (Join-Path $root 'src/votv-coop/src/coop/creatures/mannequin_target.cpp') -Raw
$index=$source.IndexOf('namespace coop::mannequin_target {');if($index -lt 0){throw 'Target boundary changed.'}
$body=$source.Substring($index);$include=Join-Path $out 'mannequin_target.inc'
function Check([string]$name,[string]$code,[bool]$negative){
 [IO.File]::WriteAllText($include,$code)
 $exe=Join-Path $out ($name+'.exe')
 & cl /nologo /std:c++20 /EHsc /utf-8 "/I$out" "/Fo$out/$name.obj" "/Fe$exe" (Join-Path $PSScriptRoot 'mannequin_target_test.cpp')
 if($LASTEXITCODE){throw 'Target fixture compile failed.'}
 $result=& $exe | Out-String;$status=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($status -eq 0 -or $result -notmatch 'FAIL '){throw "Fault control did not fail: $name"};Write-Output "PASS rejected target fault: $name"}
 else{if($status){throw $result};Write-Output $result.Trim()}
}
try{
 Check 'production' $body $false
 if($FaultControls){
  Check 'host-only' ($body.Replace('for(uint8_t slot=1;slot<players::kMaxPeers;++slot){','for(uint8_t slot=players::kMaxPeers;slot<players::kMaxPeers;++slot){')) $true
  Check 'no-hysteresis' ($body.Replace('if(current>=0&&(Angry(actor)||','if(false&&(Angry(actor)||')) $true
  Check 'unscoped-navigation' ($body.Replace('if(!frame.function||!R::NameEquals(R::NameOf(frame.function),L"ExecuteUbergraph_prop_wMannequin"))return nullptr;','// broken source-frame guard')) $true
  Check 'departed-target' ($body.Replace('if(!s->IsSlotConnected(slot)||!s->IsSlotReady(slot)||!s->IsSlotWorldReady(slot))continue;','if(!s->IsSlotWorldReady(slot))continue;')) $true
 }
}finally{[IO.File]::WriteAllText($include,$body)}
