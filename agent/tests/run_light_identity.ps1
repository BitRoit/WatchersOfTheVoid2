param([switch]$FaultControls)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $root 'build/light-identity-check'
New-Item -ItemType Directory -Force -Path $output | Out-Null
if(-not(Get-Command cl -ErrorAction SilentlyContinue)){
 $legacy=Join-Path $root '../_toolchain/env.ps1'
 if(Test-Path -LiteralPath $legacy){. $legacy}else{
  $vsDev='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat'
  if(Test-Path -LiteralPath $vsDev){
   $lines=& cmd.exe /d /s /c ('call "'+$vsDev+'" -arch=x64 -host_arch=x64 >nul && set')
   foreach($line in $lines){if($line -match '^(.*?)=(.*)$'){Set-Item -Path ('Env:'+$matches[1]) -Value $matches[2]}}
  }
 }
}
Push-Location $root
try {
 $exe=Join-Path $output 'identity.exe'
 & cl /nologo /std:c++20 /EHsc /utf-8 /Gy /I agent/tests/state_2145_stubs /I agent/tests/laptop_2171_stubs /I src/votv-coop/include "/Fo$output/identity.obj" "/Fe$exe" agent/tests/light_identity_test.cpp /link /OPT:REF
 if($LASTEXITCODE){throw 'Light identity fixture compile failed'}
 & $exe
 if($LASTEXITCODE){throw 'Light identity fixture failed'}
 if($FaultControls){
  $header=Join-Path $root 'src/votv-coop/include/coop/interactables/interactable_channel.h'
  $source=[IO.File]::ReadAllText($header)
  $anchor='if (!hasKey && !a_.allowKeylessPortableIdentity)'
  if(-not $source.Contains($anchor)){throw 'Missing old-behavior control anchor'}
  $mutantRoot=Join-Path $output 'old/coop/interactables'
  New-Item -ItemType Directory -Force -Path $mutantRoot | Out-Null
  [IO.File]::WriteAllText((Join-Path $mutantRoot 'interactable_channel.h'),$source.Replace($anchor,'if (!hasKey)'))
  $bad=Join-Path $output 'old.exe'
  & cl /nologo /std:c++20 /EHsc /utf-8 /Gy /I "$output/old" /I agent/tests/state_2145_stubs /I agent/tests/laptop_2171_stubs /I src/votv-coop/include "/Fo$output/old.obj" "/Fe$bad" agent/tests/light_identity_test.cpp /link /OPT:REF
  if($LASTEXITCODE){throw 'Old-behavior fixture compile failed'}
  $saved=$ErrorActionPreference
  try{$ErrorActionPreference='Continue';$report=& $bad 2>&1 | Out-String;$code=$LASTEXITCODE}finally{$ErrorActionPreference=$saved}
  [IO.File]::WriteAllText((Join-Path $output 'old.log'),$report)
  if($code -eq 0 -or $report -notmatch 'FAIL '){throw 'Old behavior was not rejected'}
  Write-Output 'PASS rejected original keyless-identity filter'
 }
}finally{Pop-Location}
