param([switch]$FaultControls)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $root 'build/garage-snap-check'
New-Item -ItemType Directory -Force -Path $out | Out-Null
if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { throw 'Run from an MSVC developer shell.' }

$testCpp = Join-Path $PSScriptRoot 'garage_snap_test.cpp'
$exe = Join-Path $out 'garage_snap_test.exe'

function RunCheck($name, $negative = $false) {
    & cl /nologo /std:c++20 /EHsc /utf-8 /I (Join-Path $root 'src/votv-coop/include') "/Fo$out/$name.obj" "/Fe$exe" $testCpp
    if ($LASTEXITCODE) { throw "Compilation failed for $name" }
    $result = & $exe | Out-String
    $status = $LASTEXITCODE
    if ($negative) {
        if ($status -eq 0 -or $result -notmatch 'FAIL ') { throw "Mutation survived: $name" }
        Write-Output "PASS rejected fault: $name"
    } else {
        if ($status -ne 0) { throw $result }
        Write-Output $result.Trim()
    }
}

RunCheck 'production' $false
