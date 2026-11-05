param()
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $root 'build/returning-pre-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){throw 'Run from an MSVC developer shell.'}
Push-Location $root
try {
 foreach($test in @('returning_pre_hook_test','native_result_2126_test')) {
  & cl /nologo /std:c++20 /EHsc /Gy /utf-8 /I src/votv-coop/include "/Fo$out/$test.obj" "/Fe$out/$test.exe" "agent/tests/$test.cpp" /link /OPT:REF
  if($LASTEXITCODE){throw "Compile failed: $test"}
  & "$out/$test.exe"
  if($LASTEXITCODE){throw "Regression failed: $test"}
 }
} finally {Pop-Location}
