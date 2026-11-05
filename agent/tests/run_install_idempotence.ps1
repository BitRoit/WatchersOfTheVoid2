param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/install-idempotence-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
Push-Location $root
try {
 foreach($name in @('balance','serverbox')){
  $source=if($name -eq 'balance'){'coop/world/balance_sync.cpp'}else{'coop/interactables/serverbox_sync.cpp'}
  $macro=if($name -eq 'balance'){'BALANCE_SYNC_SOURCE'}else{'SERVERBOX_SYNC_SOURCE'}
  $path=Join-Path $root ('src/votv-coop/src/'+$source)
  $body=Get-Content $path -Raw
  foreach($control in @('production','reset-every-install')){
   if($control -ne 'production' -and -not $FaultControls){continue}
   $extra=@()
   if($control -ne 'production'){
    $anchor='g_session.load(std::memory_order_acquire) == session &&'
    if(-not $body.Contains($anchor)){throw "Missing binding control anchor: $name"}
    $mutant=Join-Path $out ($name+'-old.cpp')
    [IO.File]::WriteAllText($mutant,$body.Replace($anchor,'false &&'))
    $header=Join-Path $out ($name+'-old.h')
    [IO.File]::WriteAllText($header,('#define '+$macro+' "'+$mutant.Replace('\','/')+'"'))
    $extra+="/FI$header"
   }
   $exe=Join-Path $out ($name+'-'+$control+'.exe')
   & cl /nologo /std:c++20 /EHsc /Gy /utf-8 "/Iagent/tests/$($name)_stubs" /I src/votv-coop/include @extra "/Fo$exe.obj" "/Fe$exe" "agent/tests/$($name)_sync_test.cpp" /link /OPT:REF
   if($LASTEXITCODE){throw "Fixture compile failed: $name/$control"}
   $saved=$ErrorActionPreference
   try{$ErrorActionPreference='Continue';$report=& $exe 2>&1 | Out-String;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
   [IO.File]::WriteAllText((Join-Path $out ($name+'-'+$control+'.log')),$report)
   if($control -eq 'production'){if($code){throw $report};Write-Output $report.Trim()}
   else{if($code -eq 0 -or $report -notmatch 'FAIL '){throw "Old repeated-install defect survived: $name"};Write-Output "PASS rejected $name reset-every-install defect"}
  }
  & cl /nologo /std:c++20 /EHsc /utf-8 /c /I src/votv-coop/include "/Fo$out/$name.obj" $path
  if($LASTEXITCODE){throw "Production compile failed: $name"}
 }
}finally{Pop-Location}
