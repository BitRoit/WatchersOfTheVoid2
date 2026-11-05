param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/subsystem-cadence-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){. (Join-Path $root '../_toolchain/env.ps1')}
$source=Get-Content (Join-Path $root 'src/votv-coop/src/coop/session/subsystems.cpp') -Raw
$start=$source.IndexOf('    { PP::Scope _s{PP::Bucket::Interactable}; ue_wrap::ScopedWalkTimer _w{"sync:interactable"};')
$end=$source.IndexOf('    { PP::Scope _s{PP::Bucket::Interactable}; ue_wrap::ScopedWalkTimer _w{"sync:movement_ledger"};',$start)
if($start -lt 0 -or $end -le $start){throw 'Production scheduler boundaries changed'}
$body=$source.Substring($start,$end-$start)
$calls=@([regex]::Matches($body,'coop::([a-zA-Z_:]+)::(Tick|Drive|TickDrills|TickSkinConverge)\(') | ForEach-Object {$_.Groups[1].Value+'::'+$_.Groups[2].Value} | Sort-Object -Unique)
$critical=@('time_sync::Tick','drone_sync::Tick','desk_cursor_sync::Tick','desk_sim_sync::Tick')
foreach($name in $critical){if($name -notin $calls){throw "Required continuous pump missing: $name"}}
$prefix=@'
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
std::map<std::string,int> counts;
namespace PP {enum class Bucket{Interactable};struct Scope{Scope(Bucket){}};}
namespace ue_wrap {struct ScopedWalkTimer{ScopedWalkTimer(const char*){}};}
'@
$stubs=foreach($name in $calls){$pos=$name.LastIndexOf('::');$ns=$name.Substring(0,$pos);$fn=$name.Substring($pos+2);'namespace coop::'+$ns+' {template<class... T> void '+$fn+'(T&&...){++counts["'+$name+'"];}}'}
$checks=foreach($name in $calls){$hostOnly=$name -in @('explosion_parking::Tick','prop_drive_host::Tick');$expected=if($hostOnly){'(host?120:0)'}else{'120'};'if(counts["'+$name+'"]!='+$expected+'){std::fprintf(stderr,"FAIL cadence '+$name+' host=%d count=%d\n",int(host),counts["'+$name+'"]);return 3;}'}
function Check([string]$name,[string]$block,[bool]$negative){
 $code=@($prefix,($stubs -join "`n"),'void Frame(bool isHost,int testFrame){int session=0;',$block,'}',
   'int main(){for(bool host:{false,true}){counts.clear();for(int frame=0;frame<120;++frame)Frame(host,frame);',
   ($checks -join "`n"),'}std::puts("PASS actual production scheduler: every continuous/background pump every frame, host/client gates intact");return 0;}') -join "`n"
 $file=Join-Path $out ($name+'.cpp');$exe=Join-Path $out ($name+'.exe')
 [IO.File]::WriteAllText($file,$code,[Text.UTF8Encoding]::new($false))
 & cl /nologo /std:c++20 /EHsc /utf-8 "/Fo$out/$name.obj" "/Fe$exe" $file
 if($LASTEXITCODE){throw "Scheduler fixture compile failed: $name"}
 $saved=$ErrorActionPreference
 try{$ErrorActionPreference='Continue';$result=& $exe 2>&1 | Out-String;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
 [IO.File]::WriteAllText((Join-Path $out ($name+'.log')),$result)
 if($negative){if($code -eq 0 -or $result -notmatch 'FAIL cadence'){throw "Cadence fault control did not fail: $name"};Write-Output "PASS rejected four-frame throttle: $name"}
 else{if($code){throw $result};Write-Output $result.Trim()}
}
Check 'production' $body $false
if($FaultControls){foreach($name in $critical){$needle='coop::'+$name+'();';if(-not $body.Contains($needle)){throw "Missing mutation site $name"};$mutant=$body.Replace($needle,'if((testFrame&3)==0) '+$needle);Check ($name.Replace('::','_')) $mutant $true}}
$census=Get-Content (Join-Path $root 'src/votv-coop/src/coop/props/prop_census.cpp') -Raw
if($census -notmatch 'kMaxItemsPerDrainTick\s*=\s*32' -or $census -notmatch 'processed < kMaxItemsPerDrainTick' -or $census -notmatch 'now.QuadPart - t0.QuadPart'){throw 'Reseed item/time bounds changed'}
Write-Output 'PASS reseed32 candidate cap and elapsed-time budget retained'
