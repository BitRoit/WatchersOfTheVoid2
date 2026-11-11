param([switch]$FaultControls)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$output = Join-Path $root 'build/breaker-regression'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Push-Location $root
try {
    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) { . (Join-Path $root '../_toolchain/env.ps1') }
    $source = [IO.File]::ReadAllText((Join-Path $root 'src/votv-coop/src/coop/world/power_panel.cpp')).Replace("`r`n", "`n")
    $wrapper = [IO.File]::ReadAllText((Join-Path $root 'src/votv-coop/src/ue_wrap/devices/power_control.cpp')).Replace("`r`n", "`n")
    function Get-FunctionBody([string]$text, [string]$start) {
        $singleLine = [regex]::Match($text, '(?m)^' + [regex]::Escape($start) + '[^\n]*\}[^\n]*$')
        if ($singleLine.Success) { return $singleLine.Value }
        $match = [regex]::Match($text, '(?ms)^' + [regex]::Escape($start) + '.*?^\}')
        if (-not $match.Success) { throw "Production function not found: $start" }
        return $match.Value
    }
    function Write-Includes([string]$text) {
        $scope = [regex]::Match($text, '(?ms)^struct ScopedPagePress \{.*?^\};')
        if (-not $scope.Success) { throw 'ScopedPagePress not found' }
        $start = $text.IndexOf('constexpr int kBreakerCompCount = 5;')
        $end = $text.IndexOf('// ---- a client', $start)
        $clientEnd = $text.IndexOf('// ---- the watches', $end)
        if ($start -lt 0 -or $end -le $start -or $clientEnd -le $end) { throw 'Breaker source sections not found' }
        $watches = @('sg::Verdict OnPagePre(', 'sg::Verdict OnBreakerButtonClickPre(',
                     'sg::Verdict OnBreakerUpdPre(', 'sg::Verdict OnSeamPre(', 'sg::Verdict OnOwnEdgePre(')
        $watchText = ($watches | ForEach-Object { Get-FunctionBody $text $_ }) -join "`n`n"
        $helpers = @('bool SeqAfter(', 'coop::net::Session* Connected(') | ForEach-Object { Get-FunctionBody $text $_ }
        $layoutEntry = [regex]::Match($wrapper, '(?m)^constexpr int32_t kBreakerFinishEntry = \d+;$').Value
        if (-not $layoutEntry) { throw 'Breaker continuation entry not found' }
        $parts = @{
            'breaker_scope.inc' = $scope.Value
            'breaker_helpers.inc' = $helpers -join "`n`n"
            'breaker_restores.inc' = $text.Substring($start, $end - $start)
            'breaker_clients.inc' = $text.Substring($end, $clientEnd - $end)
            'breaker_watches.inc' = $watchText
            'breaker_mapping.inc' = Get-FunctionBody $wrapper 'uint8_t BreakerBitForCompIndex('
            'breaker_layout.inc' = $layoutEntry + "`n" + (Get-FunctionBody $wrapper 'bool BreakerFinishLayoutMatches(const uint8_t*')
        }
        foreach ($name in $parts.Keys) { [IO.File]::WriteAllText((Join-Path $output $name), $parts[$name]) }
    }
    function Compile-Test([string]$name) {
        & cl /nologo /std:c++20 /EHsc /Gy /utf-8 "/I$output" "/Fo$output/$name.obj" "/Fe$output/$name.exe" agent/tests/power_panel_regression_test.cpp /link /OPT:REF
        if ($LASTEXITCODE) { throw "Breaker fixture compile failed: $name" }
    }
    Write-Includes $source
    Compile-Test 'breaker'
    & "$output/breaker.exe"
    if ($LASTEXITCODE) { throw 'Breaker fixture failed' }
    foreach ($file in @('coop/world/power_panel', 'ue_wrap/devices/power_control')) {
        $name = $file.Replace('/', '_')
        & cl /nologo /std:c++20 /EHsc /utf-8 /c /I src/votv-coop/include "/Fo$output/$name.obj" "src/votv-coop/src/$file.cpp"
        if ($LASTEXITCODE) { throw "Breaker production compile failed: $file" }
    }
    if ($FaultControls) {
        $controls = @(
            @{ Name='offline'; Before="if (!Connected()) {`n        ResetAllBreakerRestores();`n        ForgetBreakerWidgetCleanup(call.object);`n        return sg::Verdict::Run;`n    }"; After=''; Before2='if (!Connected() || PC::Panel() != panel ||'; After2='if (PC::Panel() != panel ||' },
            @{ Name='race'; Before='((mask & r.bit) != 0) != r.startOn'; After='false' },
            @{ Name='fallback-seam'; Before='(g_pagePressActive || (pageFn && sg::IsBodyActive(pageFn)))'; After='g_pagePressActive' },
            @{ Name='timing'; Before='std::pow(std::clamp(randomUnit, 0.0f, 1.0f), exponent)'; After='randomUnit' },
            @{ Name='terminal'; Before='r.terminal = terminal;'; After='r.terminal = EL::kInvalidId;' },
            @{ Name='native-spinner'; Before="    TryBreakerWidgetCleanup(call.object, true, false);`n    return sg::Verdict::Run;`n}`n`n// The seam"; After="    SetBreakerCompSwitcher(call.object, 0);`n    return sg::Verdict::Run;`n}`n`n// The seam" },
            @{ Name='cleanup-retry'; Before='RetryBreakerWidgetCleanup();'; After='' },
            @{ Name='orphan-cleanup'; Before="    TryBreakerWidgetCleanup(call.object, true, false);`n    return sg::Verdict::Run;`n}`n`n// The seam"; After="    return sg::Verdict::Run;`n}`n`n// The seam" },
            @{ Name='native-reentry'; Before="    for (auto it = g_breakerCleanupInFlight.begin(); it != g_breakerCleanupInFlight.end();) {`n        if (it->Is(widget)) it = g_breakerCleanupInFlight.erase(it);`n        else ++it;`n    }"; After='' },
            @{ Name='native-completion'; Before='result = PC::FinishBreakerComp(widget, panel);'; After='result = PC::BreakerCompFinish::Unavailable;' }
        )
        foreach ($control in $controls) {
            if (-not $source.Contains($control.Before)) { throw "Breaker fault anchor missing: $($control.Name)" }
            $mutant = $source.Replace($control.Before, $control.After)
            if ($control.Before2) {
                if (-not $mutant.Contains($control.Before2)) { throw "Breaker fault anchor missing: $($control.Name) second" }
                $mutant = $mutant.Replace($control.Before2, $control.After2)
            }
            Write-Includes $mutant
            $name = 'fault_' + $control.Name
            Compile-Test $name
            $saved = $ErrorActionPreference
            try { $ErrorActionPreference = 'Continue'; $report = & "$output/$name.exe" 2>&1 | Out-String; $code = $LASTEXITCODE }
            finally { $ErrorActionPreference = $saved }
            if ($code -eq 0 -or $report -notmatch 'FAIL \d+:') { throw "Breaker fault control did not fail its assertion: $name" }
            Write-Output ('PASS breaker fault ' + $control.Name + ': ' + [regex]::Match($report, 'FAIL [^\r\n]+').Value)
        }
        Write-Includes $source
    }
} finally { Pop-Location }
