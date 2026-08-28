# Copyright 2026 Aethernet Inc.
# Fast UAP acceptance loop. No full rebuild, no 100-cycle characterization,
# no delivery benches, no deadline filtration.

[CmdletBinding()]
param(
    [ValidateSet('tcp', 'udp', 'both')]
    [string]$Transport = 'both',
    [switch]$NoBuild,
    [switch]$BuildProtocol
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ArtifactRoot = Join-Path $Root 'artifacts\uap-fast'
$ReportPath = Join-Path $ArtifactRoot 'report.md'
$ProtocolTimeoutMs = 60000
$PingRetryTimeoutMs = 180000
$CharTimeoutMs = 360000

function Write-Info([string]$Message) {
    Write-Host $Message
}

function Get-SelectedTransports {
    if ($Transport -eq 'both') { return @('tcp', 'udp') }
    return @($Transport)
}

function Find-ProtocolExe {
    $candidates = @(
        (Join-Path $Root 'build-win64\tests\run\Debug\test-api-protocol.exe'),
        (Join-Path $Root 'build-win64\tests\run\Release\test-api-protocol.exe')
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }
    return $null
}

function Find-BuildDir([string]$Kind) {
    if ($Kind -eq 'tcp') {
        return Join-Path $Root 'build-win64-uap-ping-retry-tcp'
    }
    return Join-Path $Root 'build-win64-uap-ping-retry-udp'
}

function Find-TargetExe([string]$BuildDir, [string]$Name) {
    foreach ($cfg in @('Release', 'Debug')) {
        $p = Join-Path $BuildDir "$cfg\$Name.exe"
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Get-RelevantSources([string]$Target) {
    $files = @()
    $files += Get-ChildItem -Path (Join-Path $Root 'aether') -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue
    $files += Get-ChildItem -Path (Join-Path $Root 'examples\aether_uap_ping_retry_window_test') -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue
    if ($Target -eq 'aether_uap_1s_timing_characterization') {
        $files += Get-ChildItem -Path (Join-Path $Root 'examples\aether_uap_1s_timing_characterization') -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue
    }
    return $files
}

function Test-ExeNeedsBuild([string]$ExePath, [string]$Target) {
    if (-not $ExePath -or -not (Test-Path $ExePath)) { return $true }
    $exeTime = (Get-Item $ExePath).LastWriteTimeUtc
    foreach ($src in Get-RelevantSources $Target) {
        if ($src.LastWriteTimeUtc -gt $exeTime) { return $true }
    }
    return $false
}

function Invoke-IncrementalBuild([string]$BuildDir, [string]$Target) {
    if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
        throw "Build directory missing CMake cache: $BuildDir"
    }
    Write-Info "Incremental build: $Target in $BuildDir"
    & cmake --build $BuildDir --config Release --target $Target --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Incremental build failed for $Target (exit $LASTEXITCODE)"
    }
}

function Invoke-TimedProcess {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$LogPath,
        [int]$TimeoutMs,
        [string]$WorkDir
    )
    $dir = Split-Path $LogPath -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $stdout = "$LogPath.stdout.txt"
    $stderr = "$LogPath.stderr.txt"
    $startParams = @{
        FilePath               = $FilePath
        WorkingDirectory       = $WorkDir
        NoNewWindow            = $true
        PassThru               = $true
        RedirectStandardOutput = $stdout
        RedirectStandardError  = $stderr
    }
    if ($null -ne $ArgumentList -and $ArgumentList.Count -gt 0) {
        $startParams.ArgumentList = $ArgumentList
    }
    $proc = Start-Process @startParams
    $finished = $proc.WaitForExit($TimeoutMs)
    if (-not $finished) {
        try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        Get-CimInstance Win32_Process -Filter "ParentProcessId=$($proc.Id)" -ErrorAction SilentlyContinue |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
        @(
            "TIMEOUT after ${TimeoutMs}ms"
        ) + @(Get-Content $stdout, $stderr -ErrorAction SilentlyContinue) |
            Set-Content -Path $LogPath
        return @{ ExitCode = 124; TimedOut = $true; Log = $LogPath }
    }
    $combined = @()
    if (Test-Path $stdout) { $combined += Get-Content $stdout }
    if (Test-Path $stderr) { $combined += Get-Content $stderr }
    $combined | Set-Content -Path $LogPath
    return @{ ExitCode = [int]$proc.ExitCode; TimedOut = $false; Log = $LogPath }
}

function Get-RegexGroup([string]$Text, [string]$Pattern) {
    $m = [regex]::Match($Text, $Pattern)
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

function Parse-CharReport([string]$ReportFile, [string]$Kind) {
    $result = [ordered]@{
        Kind              = $Kind
        Exists            = $false
        NominalCycles     = 'n/a'
        RequestLoss       = 'n/a'
        ResponseLoss      = 'n/a'
        GracefulUnknown   = 'n/a'
        HardStopMissed    = 'n/a'
        Duplicates        = 'n/a'
        LiveFalse         = 'n/a'
        P99Rtt            = 'n/a'
        PreDeadlineRetry  = 'n/a'
        InvalidMetrics    = @()
        Result            = 'FAIL'
        KnownBlocker      = $false
    }
    if (-not (Test-Path $ReportFile)) {
        $result.InvalidMetrics += 'missing report.md'
        return [pscustomobject]$result
    }
    $result.Exists = $true
    $text = Get-Content -Raw $ReportFile

    $result.NominalCycles = Get-RegexGroup $text '- cycles: (\d+)'
    $reqOk = Get-RegexGroup $text '- single_request_loss_recovery: .* \((\d+)/'
    $reqN = Get-RegexGroup $text '- single_request_loss_recovery: .* \(\d+/(\d+)\)'
    if ($reqOk -and $reqN) { $result.RequestLoss = "$reqOk/$reqN" }
    $ignOk = Get-RegexGroup $text '- single_response_loss_recovery: .* \((\d+)/'
    $ignN = Get-RegexGroup $text '- single_response_loss_recovery: .* \(\d+/(\d+)\)'
    if ($ignOk -and $ignN) { $result.ResponseLoss = "$ignOk/$ignN" }
    $graceHit = Get-RegexGroup $text '- graceful_unknown_detection_rate: .* \((\d+)/'
    $graceN = Get-RegexGroup $text '- graceful_unknown_detection_rate: .* \(\d+/(\d+)\)'
    if ($graceHit -and $graceN) { $result.GracefulUnknown = "$graceHit/$graceN" }
    $hardHit = Get-RegexGroup $text '- missed_deadline_detection_rate: .* \((\d+)/'
    $hardN = Get-RegexGroup $text '- missed_deadline_detection_rate: .* \(\d+/(\d+)\)'
    if ($hardHit -and $hardN) { $result.HardStopMissed = "$hardHit/$hardN" }
    $result.Duplicates = Get-RegexGroup $text '- duplicates: (\d+)'
    $liveM = Get-RegexGroup $text '- live_false_MissedDeadline: (\d+)'
    $liveU = Get-RegexGroup $text '- live_false_Unknown: (\d+)'
    if ($liveM -and $liveU) { $result.LiveFalse = "$liveM/$liveU" }
    $result.P99Rtt = Get-RegexGroup $text 'p99_rtt_ms[=: ]+(\d+)'
    $before = Get-RegexGroup $text '- retries_before_nominal: (\d+)'
    $after = Get-RegexGroup $text '- retries_after_nominal: (\d+)'
    if ($before -and $after) {
        $result.PreDeadlineRetry = "$before before / $after after"
    }

    if ($text -match '1e12|1e\+12|1\.0+e\+12') {
        $result.InvalidMetrics += 'INVALID METRIC: timeout_to_retry looks like mixed clocks (1e12)'
    }
    $drift = Get-RegexGroup $text '- phase_drift_max_ms: ([0-9.]+)'
    if ($drift -and [double]$drift -gt 5000) {
        $result.InvalidMetrics += "INVALID METRIC: phase_drift_max_ms=$drift is not physical"
    }
    if ($text -match '(^|[^\d])53000([^\d]|$)') {
        $result.InvalidMetrics += 'INVALID METRIC: ~53000 ms drift marker present'
    }
    $guard = Get-RegexGroup $text '- guard_ms[=:] ?(\d+)'
    if (-not $guard) {
        $guard = Get-RegexGroup $text 'guard_ms=(\d+)'
    }
    if ($guard -and $result.P99Rtt) {
        $g = [int]$guard
        $p = [int]$result.P99Rtt
        if ($g -le 10 -and $p -gt 50) {
            $result.InvalidMetrics += "INVALID METRIC: guard ${g}ms vs p99 RTT ${p}ms"
        }
    }
    if ($text -match 'KNOWN BLOCKER: TCP pre-deadline retry') {
        $result.KnownBlocker = $true
    }

    $fail = $false
    if ($result.Duplicates -and [int]$result.Duplicates -ne 0) { $fail = $true }
    if ($liveM -and [int]$liveM -ne 0) { $fail = $true }
    if ($liveU -and [int]$liveU -ne 0) { $fail = $true }
    if ($hardHit -and $hardN -and [int]$hardN -gt 0 -and [int]$hardHit -lt [int]$hardN) { $fail = $true }
    if ($graceHit -and $graceN -and [int]$graceN -gt 0 -and [int]$graceHit -lt [int]$graceN) { $fail = $true }
    if ($reqOk -and $reqN -and [int]$reqN -gt 0 -and [int]$reqOk -lt [int]$reqN) { $fail = $true }
    if ($ignOk -and $ignN -and [int]$ignN -gt 0 -and [int]$ignOk -lt [int]$ignN) { $fail = $true }
    foreach ($inv in $result.InvalidMetrics) {
        if ($inv -match 'timeout_to_retry|phase_drift|guard ') { $fail = $true }
    }
    if ($Kind -eq 'udp' -and $before -and [int]$before -eq 0 -and $text -notmatch 'transport-specific reason') {
        $fail = $true
        $result.InvalidMetrics += 'UDP did not demonstrate retry-before-nominal'
    }
    if ($Kind -eq 'tcp' -and $before -and [int]$before -eq 0) {
        $result.KnownBlocker = $true
    }
    $result.Result = $(if ($fail) { 'FAIL' } else { 'PASS' })
    return [pscustomobject]$result
}

function Format-TransportSection([string]$Label, $Parsed) {
    if (-not $Parsed) {
        return @(
            "${Label}:",
            '- not run',
            ''
        )
    }
    $invalid = 'none'
    if ($Parsed.InvalidMetrics -and $Parsed.InvalidMetrics.Count -gt 0) {
        $invalid = $Parsed.InvalidMetrics -join '; '
    }
    return @(
        "${Label}:",
        "- nominal cycles: $($Parsed.NominalCycles)",
        "- request-loss recovery: $($Parsed.RequestLoss)",
        "- response-loss recovery: $($Parsed.ResponseLoss)",
        "- graceful Unknown: $($Parsed.GracefulUnknown)",
        "- hard-stop MissedDeadline: $($Parsed.HardStopMissed)",
        "- duplicates: $($Parsed.Duplicates)",
        "- live false MissedDeadline/Unknown: $($Parsed.LiveFalse)",
        "- p99 RTT: $($Parsed.P99Rtt)",
        "- pre-deadline retry: $($Parsed.PreDeadlineRetry)",
        "- invalid metrics: $invalid",
        "- result: $($Parsed.Result)",
        ''
    )
}

New-Item -ItemType Directory -Path $ArtifactRoot -Force | Out-Null
$rebuilt = @()
$protocolStatus = 'SKIPPED'
$overallFail = $false
$tcpResult = $null
$udpResult = $null
$knownBlockers = @()

$protocolExe = Find-ProtocolExe
if ($BuildProtocol -and -not $NoBuild) {
    $protoDir = Join-Path $Root 'build-win64'
    if (Test-Path (Join-Path $protoDir 'CMakeCache.txt')) {
        Invoke-IncrementalBuild $protoDir 'test-api-protocol'
        $rebuilt += 'test-api-protocol'
        $protocolExe = Find-ProtocolExe
    }
}

if ($protocolExe) {
    Write-Info "Protocol smoke: $protocolExe"
    $protoRun = Invoke-TimedProcess -FilePath $protocolExe -ArgumentList @() `
        -LogPath (Join-Path $ArtifactRoot 'protocol.log') `
        -TimeoutMs $ProtocolTimeoutMs -WorkDir (Split-Path $protocolExe)
    if ($protoRun.TimedOut -or $protoRun.ExitCode -ne 0) {
        $protocolStatus = 'FAIL'
        $overallFail = $true
    } else {
        $protocolStatus = 'PASS'
    }
    Write-Info "Protocol: $protocolStatus"
} else {
    Write-Info 'SKIPPED: test-api-protocol executable not found; pass -BuildProtocol to build it.'
}

foreach ($kind in Get-SelectedTransports) {
    $buildDir = Find-BuildDir $kind
    $kindDir = Join-Path $ArtifactRoot $kind
    New-Item -ItemType Directory -Path $kindDir -Force | Out-Null

    $pingExe = Find-TargetExe $buildDir 'aether_uap_ping_retry_window_test'
    $charExe = Find-TargetExe $buildDir 'aether_uap_1s_timing_characterization'

    if ($NoBuild) {
        if (-not $pingExe) { throw "Missing ping-retry exe for $kind and -NoBuild was set" }
        if (-not $charExe) { throw "Missing characterization exe for $kind and -NoBuild was set" }
    } else {
        if (Test-ExeNeedsBuild $pingExe 'aether_uap_ping_retry_window_test') {
            Invoke-IncrementalBuild $buildDir 'aether_uap_ping_retry_window_test'
            $rebuilt += "aether_uap_ping_retry_window_test/$kind"
            $pingExe = Find-TargetExe $buildDir 'aether_uap_ping_retry_window_test'
        }
        if (Test-ExeNeedsBuild $charExe 'aether_uap_1s_timing_characterization') {
            Invoke-IncrementalBuild $buildDir 'aether_uap_1s_timing_characterization'
            $rebuilt += "aether_uap_1s_timing_characterization/$kind"
            $charExe = Find-TargetExe $buildDir 'aether_uap_1s_timing_characterization'
        }
        if (-not $pingExe) { throw "Ping-retry exe still missing for $kind" }
        if (-not $charExe) { throw "Characterization exe still missing for $kind" }
    }

    Write-Info "Ping-retry window smoke ($kind): $pingExe"
    $pingDir = Join-Path $kindDir 'ping-retry'
    New-Item -ItemType Directory -Path $pingDir -Force | Out-Null
    $pingRun = Invoke-TimedProcess -FilePath $pingExe -ArgumentList @(
        '--quick', "--transport=$kind", '--artifact-dir', $pingDir
    ) -LogPath (Join-Path $pingDir 'run.log') -TimeoutMs $PingRetryTimeoutMs -WorkDir $Root
    if ($pingRun.TimedOut -or $pingRun.ExitCode -ne 0) {
        Write-Info "Ping-retry $kind FAILED (exit=$($pingRun.ExitCode) timeout=$($pingRun.TimedOut))"
        $overallFail = $true
    } else {
        Write-Info "Ping-retry $kind PASS"
    }

    Write-Info "1s characterization quick ($kind): $charExe"
    $charDir = Join-Path $kindDir 'characterization'
    New-Item -ItemType Directory -Path $charDir -Force | Out-Null
    $charRun = Invoke-TimedProcess -FilePath $charExe -ArgumentList @(
        '--quick',
        "--transport=$kind",
        '--artifact-dir', $charDir,
        '--cycles', '10',
        '--loss-cases', '2',
        '--graceful-stop-cases', '3',
        '--hard-stop-cases', '3',
        '--no-long-characterization'
    ) -LogPath (Join-Path $charDir 'run.log') -TimeoutMs $CharTimeoutMs -WorkDir $Root
    if ($charRun.TimedOut -or $charRun.ExitCode -ne 0) {
        Write-Info "Characterization $kind FAILED (exit=$($charRun.ExitCode) timeout=$($charRun.TimedOut))"
        $overallFail = $true
    } else {
        Write-Info "Characterization $kind process PASS"
    }

    $parsed = Parse-CharReport (Join-Path $charDir 'report.md') $kind
    if ($pingRun.TimedOut -or $pingRun.ExitCode -ne 0 -or $charRun.TimedOut -or $charRun.ExitCode -ne 0) {
        $parsed.Result = 'FAIL'
    }
    if ($parsed.Result -eq 'FAIL') { $overallFail = $true }
    if ($parsed.KnownBlocker) {
        $knownBlockers += 'KNOWN BLOCKER: TCP pre-deadline retry is limited by RTT/timeout policy; not fixed in this quick loop.'
    }
    if ($kind -eq 'tcp') { $tcpResult = $parsed } else { $udpResult = $parsed }
}

$rebuiltText = 'none'
if ($rebuilt.Count -gt 0) { $rebuiltText = $rebuilt -join ', ' }
$blockerText = '- none'
if ($knownBlockers.Count -gt 0) {
    $blockerText = (($knownBlockers | Select-Object -Unique) | ForEach-Object { "- $_" }) -join "`n"
}

$lines = @()
$lines += '# UAP Fast Acceptance Report'
$lines += 'Build:'
$lines += '- build directory: build-win64 (protocol), build-win64-uap-ping-retry-tcp, build-win64-uap-ping-retry-udp'
$lines += "- rebuilt targets: $rebuiltText"
$lines += "- NoBuild: $NoBuild"
$lines += ''
$lines += 'Protocol:'
$lines += "- test-api-protocol: $protocolStatus"
$lines += ''
$lines += Format-TransportSection 'TCP' $tcpResult
$lines += Format-TransportSection 'UDP' $udpResult
$lines += 'Known blockers:'
$lines += $blockerText
$lines += ''
$lines += 'Not run:'
$lines += '- full 100-cycle characterization;'
$lines += '- deadline filtration;'
$lines += '- TCP delivery bench;'
$lines += '- UDP delivery bench.'
$lines | Set-Content -Path $ReportPath -Encoding utf8
Write-Info "Wrote $ReportPath"
Get-Content $ReportPath | ForEach-Object { Write-Host $_ }

if ($overallFail) { exit 1 }
exit 0
