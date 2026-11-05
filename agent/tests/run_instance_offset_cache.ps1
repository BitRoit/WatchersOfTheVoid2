param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $root 'build/instance-offset-cache-check'
New-Item -ItemType Directory -Force -Path $output | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
$reflection=Get-Content (Join-Path $root 'src/votv-coop/src/ue_wrap/core/reflection_props.cpp') -Raw
$method=[regex]::Match($reflection,'(?ms)^int32_t InstanceOffset::Of\(.*?^\}').Value
if(-not $method){throw 'Production InstanceOffset seam missing'}
Push-Location $root
try {
 function RunCase($name,$body,$negative=$false){
  [IO.File]::WriteAllText((Join-Path $output 'instance_offset_production.inc'),("namespace ue_wrap::reflection {`n"+$body+"`n}"))
  $exe=Join-Path $output ($name+'.exe')
  & cl /nologo /std:c++20 /EHsc /utf-8 /Gy "/I$output" /I src/votv-coop/include "/Fo$output/$name.obj" "/Fe$exe" agent/tests/instance_offset_cache_test.cpp /link /OPT:REF
  if($LASTEXITCODE){throw "Fixture compile failed: $name"}
  $saved=$ErrorActionPreference
  try{$ErrorActionPreference='Continue';$report=& $exe 2>&1 | Out-String;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
  [IO.File]::WriteAllText((Join-Path $output ($name+'.log')),$report)
  if($negative){if($code -eq 0 -or $report -notmatch 'FAIL '){throw "Control survived: $name"};Write-Output "PASS rejected fault: $name"}
  else{if($code){throw $report};Write-Output $report.Trim()}
 }
 RunCase 'production' $method
 if($FaultControls){
  RunCase 'repeated-resolution' ($method.Replace('if (entry.cls == cls &&','if (false &&')) $true
  RunCase 'stale-serial' ($method.Replace('SlotSerial(entry.index) == entry.serial','true')) $true
  RunCase 'success-log' ($method.Replace('    return offset;','    UE_LOGI("resolved"); return offset;')) $true
 }
 & cl /nologo /std:c++20 /EHsc /utf-8 /c /I src/votv-coop/include "/Fo$output/reflection_props.obj" src/votv-coop/src/ue_wrap/core/reflection_props.cpp
 if($LASTEXITCODE){throw 'Production reflection_props compile failed'}
}finally{Pop-Location}
