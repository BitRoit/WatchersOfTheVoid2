$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output=Join-Path $root 'build/flashlight-absolute-check'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $root
try {
    if(-not(Get-Command cl -ErrorAction SilentlyContinue)) {
        $legacy=Join-Path $root '../_toolchain/env.ps1'
        if(Test-Path -LiteralPath $legacy) {
            . $legacy
        } else {
            $vsDev='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat'
            if(-not(Test-Path -LiteralPath $vsDev)){throw 'No C++ compiler bootstrap was found.'}
            $lines=& cmd.exe /d /s /c ('call "'+$vsDev+'" -arch=x64 -host_arch=x64 >nul && set')
            foreach($line in $lines){if($line -match '^(.*?)=(.*)$'){Set-Item -Path ('Env:'+$matches[1]) -Value $matches[2]}}
        }
    }
    $exe=Join-Path $output 'flashlight_absolute_component.exe'
    & cl /nologo /std:c++20 /EHsc /Gy /utf-8 /I src/votv-coop/include "/Fo$output/flashlight_absolute_component.obj" "/Fe$exe" agent/tests/flashlight_absolute_component_test.cpp /link /OPT:REF
    if($LASTEXITCODE){throw 'Flashlight absolute wrapper fixture compilation failed.'}
    foreach($case in @('valid','unresolved-class','unresolved-function','dead','missing-location','missing-rotation','missing-scale','call-failure')) {
        & $exe $case
        if($LASTEXITCODE){throw "Flashlight absolute wrapper regression failed: $case"}
    }
    foreach($unit in @('ue_wrap/engine/engine_component','ue_wrap/actors/puppet_spawn','coop/player/item_activate')) {
        $source=Join-Path 'src/votv-coop/src' ($unit+'.cpp')
        $name=$unit.Replace('/','_')
        & cl /nologo /std:c++20 /EHsc /utf-8 /c /I src/votv-coop/include "/Fo$output/$name.obj" $source
        if($LASTEXITCODE){throw "Flashlight absolute production compilation failed: $unit"}
    }
} finally { Pop-Location }
