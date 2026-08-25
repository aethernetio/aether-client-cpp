# Copyright 2026 Aethernet Inc.
# Resumable UAP long characterization. Calibration, plan, and later run.
# Does not change production behavior. Sequential TCP then UDP only.

[CmdletBinding()]
param(
    [ValidateSet('tcp', 'udp', 'both')]
    [string]$Transport = 'both',
    [double]$TargetHours = 10,
    [switch]$Calibrate,
    [switch]$PlanOnly,
    [switch]$Run,
    [switch]$DryRun,
    [switch]$NoBuild,
    [switch]$Resume
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not ($Calibrate -or $PlanOnly -or $Run -or $DryRun)) {
    throw 'Specify -Calibrate, -PlanOnly, -DryRun, or -Run.'
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ArtifactRoot = Join-Path $Root 'artifacts\uap-long'
$CalibRoot = Join-Path $ArtifactRoot 'calibration'
$CalibJson = Join-Path $CalibRoot 'calibration.json'
$CalibReport = Join-Path $CalibRoot 'report.md'
$PlanJson = Join-Path $ArtifactRoot 'plan.json'
$PlanMd = Join-Path $ArtifactRoot 'plan.md'
$PlanV2Json = Join-Path $ArtifactRoot 'plan-v2.json'
$PlanV2Md = Join-Path $ArtifactRoot 'plan-v2.md'
$RunsRoot = Join-Path $ArtifactRoot 'runs-v2'
$LegacyRunsRoot = Join-Path $ArtifactRoot 'runs'
$AggRoot = Join-Path $ArtifactRoot 'aggregate'
$ProbeRoot = Join-Path $ArtifactRoot 'timing-probe'
$StartsPerShard = 2
$PingCadenceSeconds = 1.0
$PlanV2MinSeconds = (9 * 3600) + (20 * 60)
$PlanV2MaxSeconds = (9 * 3600) + (45 * 60)
$StatusPath = Join-Path $ArtifactRoot 'status.json'
$RunInfoPath = Join-Path $ArtifactRoot 'run-info.json'
$CharName = 'aether_uap_1s_timing_characterization'
$CalibTimeoutMs = 900000
$ES_CONTINUOUS = [uint32]2147483648
$ES_SYSTEM_REQUIRED = [uint32]1

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class UapNativeSleep {
  [DllImport("kernel32.dll")]
  public static extern uint SetThreadExecutionState(uint esFlags);
}
"@ -ErrorAction SilentlyContinue

function Write-Info([string]$Message) { Write-Host $Message }

function Get-SelectedTransports {
    if ($Transport -eq 'both') { return @('tcp', 'udp') }
    return @($Transport)
}

function Find-BuildDir([string]$Kind) {
    if ($Kind -eq 'tcp') { return Join-Path $Root 'build-win64-uap-ping-retry-tcp' }
    return Join-Path $Root 'build-win64-uap-ping-retry-udp'
}

function Find-TargetExe([string]$BuildDir) {
    foreach ($cfg in @('Release', 'Debug')) {
        $p = Join-Path $BuildDir "$cfg\$CharName.exe"
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Get-CharSources {
    $files = @()
    $files += Get-ChildItem -Path (Join-Path $Root 'aether') -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue
    $files += Get-ChildItem -Path (Join-Path $Root 'examples\aether_uap_ping_retry_window_test') -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue
    $files += Get-ChildItem -Path (Join-Path $Root 'examples\aether_uap_1s_timing_characterization') -Recurse -Include *.cpp,*.h -ErrorAction SilentlyContinue
    return $files
}

function Test-ExeStale([string]$ExePath) {
    if (-not $ExePath -or -not (Test-Path $ExePath)) { return $true }
    $exeTime = (Get-Item $ExePath).LastWriteTimeUtc
    foreach ($src in Get-CharSources) {
        if ($src.LastWriteTimeUtc -gt $exeTime) { return $true }
    }
    return $false
}

function Resolve-CharExe([string]$Kind) {
    $buildDir = Find-BuildDir $Kind
    $exe = Find-TargetExe $buildDir
    if ($NoBuild) {
        if (-not $exe) {
            throw "NoBuild: missing $CharName.exe for $Kind in $buildDir"
        }
        if (Test-ExeStale $exe) {
            throw "NoBuild: stale $CharName.exe for $Kind ($exe is older than sources). Build is forbidden with -NoBuild."
        }
        return $exe
    }
    if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
        throw "Build directory missing CMake cache: $buildDir"
    }
    if (Test-ExeStale $exe) {
        Write-Info "Incremental build: $CharName in $buildDir"
        & cmake --build $buildDir --config Release --target $CharName --parallel
        if ($LASTEXITCODE -ne 0) { throw "Incremental build failed for $Kind" }
        $exe = Find-TargetExe $buildDir
    }
    if (-not $exe) { throw "Missing $CharName.exe for $Kind after build" }
    return $exe
}

function Stop-ProcessTree([int]$ProcessId) {
    Get-CimInstance Win32_Process -Filter "ParentProcessId=$ProcessId" -ErrorAction SilentlyContinue |
        ForEach-Object { Stop-ProcessTree -ProcessId $_.ProcessId }
    try { Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue } catch {}
}

function Invoke-TimedProcess {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$LogPath,
        [int]$TimeoutMs,
        [string]$WorkDir,
        [string]$ProgressPath = '',
        [hashtable]$ProgressBase = $null,
        [double]$SecondsPerCycle = 1.0,
        [int]$ProgressEveryCycles = 100
    )
    $dir = Split-Path $LogPath -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $stdout = "$LogPath.stdout.txt"
    $stderr = "$LogPath.stderr.txt"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
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
    $lastHeartbeat = -1
    $pollMs = 5000
    $finished = $false
    while ($true) {
        $finished = $proc.WaitForExit($pollMs)
        if ($finished) { break }
        if ($sw.ElapsedMilliseconds -ge $TimeoutMs) { break }
        if ($ProgressPath) {
            $stdoutText = ''
            if (Test-Path $stdout) {
                try { $stdoutText = Get-Content -Raw $stdout -ErrorAction SilentlyContinue } catch { $stdoutText = '' }
            }
            $rtt = $null
            if ($stdoutText -match 'min_rtt_ms=(\d+)\s+p99_rtt_ms=(\d+)') {
                $rtt = "min_rtt_ms=$($Matches[1]) p99_rtt_ms=$($Matches[2])"
            }
            $lastLine = ''
            if ($stdoutText) {
                $lines = $stdoutText -split "`r?`n" | Where-Object { $_ -ne '' }
                if ($lines) { $lastLine = $lines[-1] }
            }
            $scenario = 'unknown'
            if ($ProgressBase -and $ProgressBase.ContainsKey('scenario')) { $scenario = [string]$ProgressBase.scenario }
            if ($stdoutText -match 'Hard-stop runs=') { $scenario = 'hard-stop' }
            elseif ($stdoutText -match 'Graceful-close runs=') { $scenario = 'graceful-stop' }
            elseif ($stdoutText -match 'Running (\d+) logical cycles') { $scenario = $(if ($ProgressBase -and $ProgressBase.scenario) { $ProgressBase.scenario } else { 'cycles' }) }
            elseif ($stdoutText -match 'Waiting Bob/Alice warm-up') { $scenario = 'warmup' }
            $target = 0
            if ($ProgressBase -and $ProgressBase.ContainsKey('target')) { $target = [int]$ProgressBase.target }
            $elapsedSec = $sw.Elapsed.TotalSeconds
            $est = $null
            $estValid = $false
            if ($scenario -eq 'nominal' -and $SecondsPerCycle -gt 0) {
                $est = [int][math]::Floor($elapsedSec / $SecondsPerCycle)
                if ($target -gt 0 -and $est -gt $target) { $est = $target }
                $bucket = [int][math]::Floor($est / [double]$ProgressEveryCycles)
                if ($bucket -ne $lastHeartbeat -and $est -ge $ProgressEveryCycles) {
                    $lastHeartbeat = $bucket
                    $estValid = $true
                } elseif ($lastHeartbeat -lt 0) {
                    $estValid = $true
                    $lastHeartbeat = 0
                }
            } else {
                $estValid = $true
            }
            if ($estValid) {
                $obj = [ordered]@{}
                if ($ProgressBase) { foreach ($k in $ProgressBase.Keys) { $obj[$k] = $ProgressBase[$k] } }
                $obj.scenario = $scenario
                $obj.elapsed_seconds = [math]::Round($elapsedSec, 3)
                $obj.completed_count = $(if ($null -ne $est -and $scenario -eq 'nominal') { $est } else { $obj.completed_count })
                $obj.target_count = $target
                $obj.rtt_summary = $rtt
                $obj.last_sample_timestamp = (Get-Date).ToString('o')
                $obj.last_stdout_line = $lastLine
                $obj.estimated = ($scenario -eq 'nominal')
                $obj.note = $(if ($scenario -eq 'nominal') { 'completed_count is wall-clock estimate every 100 cycles; coordinator does not emit per-cycle progress in the current executable' } else { 'stage in progress' })
                Write-JsonFile $ProgressPath $obj
            }
        }
    }
    $sw.Stop()
    if (-not $finished) {
        Stop-ProcessTree -ProcessId $proc.Id
        @(
            "TIMEOUT after ${TimeoutMs}ms"
        ) + @(Get-Content $stdout, $stderr -ErrorAction SilentlyContinue) |
            Set-Content -Path $LogPath
        return @{
            ExitCode     = 124
            TimedOut     = $true
            Log          = $LogPath
            ElapsedSec   = $sw.Elapsed.TotalSeconds
            StdoutPath   = $stdout
            StderrPath   = $stderr
        }
    }
    $combined = @()
    if (Test-Path $stdout) { $combined += Get-Content $stdout }
    if (Test-Path $stderr) { $combined += Get-Content $stderr }
    $combined | Set-Content -Path $LogPath
    return @{
        ExitCode     = [int]$proc.ExitCode
        TimedOut     = $false
        Log          = $LogPath
        ElapsedSec   = $sw.Elapsed.TotalSeconds
        StdoutPath   = $stdout
        StderrPath   = $stderr
    }
}

function Get-RegexGroup([string]$Text, [string]$Pattern) {
    $m = [regex]::Match($Text, $Pattern)
    if ($m.Success) { return $m.Groups[1].Value }
    return $null
}

function Read-Fraction([string]$Text, [string]$Key) {
    $hit = Get-RegexGroup $Text "- ${Key}: .* \((\d+)/"
    $n = Get-RegexGroup $Text "- ${Key}: .* \(\d+/(\d+)\)"
    if ($hit -and $n) {
        return @{ Hit = [int]$hit; N = [int]$n }
    }
    return @{ Hit = 0; N = 0 }
}

function Parse-CharReport([string]$ReportFile) {
    $r = [ordered]@{
        Exists         = $false
        Text           = ''
        Duplicates     = $null
        LiveMissed     = $null
        LiveUnknown    = $null
        Request        = @{ Hit = 0; N = 0 }
        Response       = @{ Hit = 0; N = 0 }
        Graceful       = @{ Hit = 0; N = 0 }
        HardStop       = @{ Hit = 0; N = 0 }
        PhaseDriftMax  = $null
        InvalidCount   = $null
        InvalidReasons = @()
    }
    if (-not (Test-Path $ReportFile)) {
        $r.InvalidReasons += 'missing report.md'
        return $r
    }
    $r.Exists = $true
    $text = Get-Content -Raw $ReportFile
    $r.Text = $text
    $dup = Get-RegexGroup $text '- duplicates: (\d+)'
    if ($dup) { $r.Duplicates = [int]$dup }
    $lm = Get-RegexGroup $text '- live_false_MissedDeadline: (\d+)'
    $lu = Get-RegexGroup $text '- live_false_Unknown: (\d+)'
    if ($lm) { $r.LiveMissed = [int]$lm }
    if ($lu) { $r.LiveUnknown = [int]$lu }
    $r.Request = Read-Fraction $text 'single_request_loss_recovery'
    $r.Response = Read-Fraction $text 'single_response_loss_recovery'
    $r.Graceful = Read-Fraction $text 'graceful_unknown_detection_rate'
    $r.HardStop = Read-Fraction $text 'missed_deadline_detection_rate'
    $drift = Get-RegexGroup $text '- phase_drift_max_ms: ([0-9.]+)'
    if ($drift) { $r.PhaseDriftMax = [double]$drift }
    $inv = Get-RegexGroup $text '- invalid_metric_count: (\d+)'
    if ($inv) { $r.InvalidCount = [int]$inv }
    if ($text -match '1e12|1e\+12|1\.0+e\+12') {
        $r.InvalidReasons += 'INVALID METRIC: mixed-clock 1e12 timestamp in report'
    }
    if ($null -ne $r.PhaseDriftMax -and $r.PhaseDriftMax -gt 5000) {
        $r.InvalidReasons += "INVALID METRIC: phase_drift_max_ms=$($r.PhaseDriftMax) is not physical"
    }
    if ($text -match '(^|[^\d])53000([^\d]|$)') {
        $r.InvalidReasons += 'INVALID METRIC: ~53000 ms drift marker present'
    }
    return $r
}

function Test-ScenarioSemantics {
    param($Parsed, [string]$Expect)
    $fail = @()
    if (-not $Parsed.Exists) { return @('missing report.md') }
    if ($null -ne $Parsed.Duplicates -and $Parsed.Duplicates -ne 0) {
        $fail += "duplicates=$($Parsed.Duplicates)"
    }
    if ($null -ne $Parsed.LiveMissed -and $Parsed.LiveMissed -ne 0) {
        $fail += "live_false_MissedDeadline=$($Parsed.LiveMissed)"
    }
    if ($null -ne $Parsed.LiveUnknown -and $Parsed.LiveUnknown -ne 0) {
        $fail += "live_false_Unknown=$($Parsed.LiveUnknown)"
    }
    foreach ($reason in $Parsed.InvalidReasons) { $fail += $reason }
    switch ($Expect) {
        'request' {
            if ($Parsed.Request.N -le 0 -or $Parsed.Request.Hit -lt $Parsed.Request.N) {
                $fail += "request-loss $($Parsed.Request.Hit)/$($Parsed.Request.N)"
            }
        }
        'response' {
            if ($Parsed.Response.N -le 0 -or $Parsed.Response.Hit -lt $Parsed.Response.N) {
                $fail += "response-loss $($Parsed.Response.Hit)/$($Parsed.Response.N)"
            }
        }
        'graceful' {
            if ($Parsed.Graceful.N -le 0 -or $Parsed.Graceful.Hit -lt $Parsed.Graceful.N) {
                $fail += "graceful Unknown $($Parsed.Graceful.Hit)/$($Parsed.Graceful.N) (want state 2)"
            }
        }
        'hard' {
            if ($Parsed.HardStop.N -le 0 -or $Parsed.HardStop.Hit -lt $Parsed.HardStop.N) {
                $fail += "hard-stop MissedDeadline $($Parsed.HardStop.Hit)/$($Parsed.HardStop.N) (want state 1)"
            }
        }
    }
    return $fail
}

function Format-Duration([double]$Seconds) {
    $ts = [TimeSpan]::FromSeconds([math]::Max(0, $Seconds))
    return ('{0}h {1:00}m {2:00}s' -f [int][math]::Floor($ts.TotalHours), $ts.Minutes, $ts.Seconds)
}

function Round-Down([int]$Value, [int]$Mult) {
    if ($Value -lt 0) { return 0 }
    return [int]([math]::Floor($Value / $Mult) * $Mult)
}

function Split-Even([int]$Total, [int]$N) {
    $out = New-Object int[] $N
    $base = [int][math]::Floor($Total / $N)
    $rem = $Total % $N
    for ($i = 0; $i -lt $N; $i++) {
        $out[$i] = $base + $(if ($i -lt $rem) { 1 } else { 0 })
    }
    return $out
}

function Get-ProfileCost($M, [int]$Nom, [int]$Req, [int]$Resp, [int]$Grace, [int]$Hard, [int]$Shards, [int]$ProcessStarts = 1) {
    $fixed = [double]$M.fixed_process_seconds
    if ($fixed -lt 0) { $fixed = 0 }
    $nomS = [double]$M.nominal_case_seconds
    if ($nomS -lt 0) { $nomS = 0 }
    $reqS = [double]$M.request_loss_case_seconds
    $respS = [double]$M.response_loss_case_seconds
    $graceS = [double]$M.graceful_stop_case_seconds
    $hardS = [double]$M.hard_stop_case_seconds
    if ($reqS -lt $nomS) { $reqS = $nomS }
    if ($respS -lt $nomS) { $respS = $nomS }
    if ($reqS -lt 0) { $reqS = $nomS }
    if ($respS -lt 0) { $respS = $nomS }
    if ($graceS -lt 0) { $graceS = $nomS }
    if ($hardS -lt 0) { $hardS = $nomS }
    $starts = [math]::Max(1, $ProcessStarts)
    return ($Shards * $fixed * $starts) +
        ($Nom * $nomS) +
        ($Req * $reqS) +
        ($Resp * $respS) +
        ($Grace * $graceS) +
        ($Hard * $hardS)
}

function Get-ShardTimeoutSeconds([double]$Predicted) {
    return [math]::Max(($Predicted * 1.25) + 300.0, $Predicted + 600.0)
}

function Read-ProbeElapsed([string]$Kind) {
    $p = Join-Path $ProbeRoot "$Kind\probe.json"
    if (-not (Test-Path $p)) { return $null }
    try {
        return Get-Content -Raw $p | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Convert-MeasurementToEffective($M, $Probe) {
    $fixed = [double]$M.fixed_process_seconds
    if ($fixed -lt 0) { $fixed = 0 }
    $calibNom = [double]$M.nominal_case_seconds
    $observed = $null
    $raw = $null
    if ($Probe -and [int]$Probe.cycles -gt 0) {
        $elapsed = [double]$Probe.elapsed_seconds
        $raw = $elapsed / [double]$Probe.cycles
        $observed = ($elapsed - $fixed) / [double]$Probe.cycles
    }
    $effNom = $calibNom
    if ($null -ne $observed) { $effNom = [math]::Max($effNom, $observed) }
    $effNom = [math]::Max($effNom, $PingCadenceSeconds)
    $req = [math]::Max([double]$M.request_loss_case_seconds, $effNom)
    $resp = [math]::Max([double]$M.response_loss_case_seconds, $effNom)
    if ($req -lt 0) { $req = $effNom }
    if ($resp -lt 0) { $resp = $effNom }
    $grace = [double]$M.graceful_stop_case_seconds
    $hard = [double]$M.hard_stop_case_seconds
    if ($grace -lt 0) { $grace = $effNom }
    if ($hard -lt 0) { $hard = $effNom }
    $out = [ordered]@{}
    foreach ($p in $M.PSObject.Properties) { $out[$p.Name] = $p.Value }
    if ($M -is [System.Collections.IDictionary]) {
        $out = [ordered]@{}
        foreach ($k in $M.Keys) { $out[$k] = $M[$k] }
    }
    $out.fixed_process_seconds = $fixed
    $out.nominal_case_seconds = [math]::Round($effNom, 6)
    $out.request_loss_case_seconds = [math]::Round($req, 6)
    $out.response_loss_case_seconds = [math]::Round($resp, 6)
    $out.graceful_stop_case_seconds = [math]::Round($grace, 6)
    $out.hard_stop_case_seconds = [math]::Round($hard, 6)
    $out.calibration_nominal_case_seconds = [math]::Round($calibNom, 6)
    $out.observed_nominal_case_seconds = $(if ($null -ne $observed) { [math]::Round($observed, 6) } else { $null })
    $out.raw_elapsed_per_cycle_seconds = $(if ($null -ne $raw) { [math]::Round($raw, 6) } else { $null })
    $out.process_starts_per_shard = $StartsPerShard
    return $out
}

function Get-CorrectedCalib($Calib) {
    $out = [ordered]@{}
    foreach ($p in $Calib.PSObject.Properties) { $out[$p.Name] = $p.Value }
    if ($Calib -is [System.Collections.IDictionary]) {
        $out = [ordered]@{}
        foreach ($k in $Calib.Keys) { $out[$k] = $Calib[$k] }
    }
    if ($Calib.tcp) {
        $out.tcp = Convert-MeasurementToEffective $Calib.tcp (Read-ProbeElapsed 'tcp')
    }
    if ($Calib.udp) {
        $out.udp = Convert-MeasurementToEffective $Calib.udp (Read-ProbeElapsed 'udp')
    }
    $out.plan_model = 'v2-cadence-floor-faults-before-nominal'
    return $out
}

function Write-JsonFile([string]$Path, $Object) {
    $dir = Split-Path $Path -Parent
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $tmp = "$Path.tmp"
    ($Object | ConvertTo-Json -Depth 16) | Set-Content -Path $tmp -Encoding utf8
    Move-Item -Force $tmp $Path
}

function Get-ExeSha256([string]$Path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}

function Get-CMakeBuildType([string]$BuildDir) {
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path $cache)) { return 'unknown' }
    foreach ($line in Get-Content -LiteralPath $cache) {
        if ($line -match '^CMAKE_BUILD_TYPE:STRING=(.*)$') {
            $v = $Matches[1].Trim()
            if ($v) { return $v }
        }
    }
    return 'Release (Visual Studio multi-config; using Release executable)'
}

function Get-PowerLineStatus {
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        return [System.Windows.Forms.SystemInformation]::PowerStatus.PowerLineStatus.ToString()
    } catch {
    }
    try {
        $batteries = @(Get-CimInstance Win32_Battery -ErrorAction Stop)
        if ($batteries.Count -eq 0) {
            return 'Online'
        }
        $st = [int]$batteries[0].BatteryStatus
        if ($st -eq 2) { return 'Online' }
        if ($st -eq 1) { return 'Offline' }
        return "BatteryStatus=$st"
    } catch {
        return 'unavailable'
    }
}

function Get-PowerScheme {
    try { return ((powercfg /getactivescheme) | Out-String).Trim() }
    catch { return 'unavailable' }
}

function Get-ActiveAdapters {
    try {
        $rows = Get-NetAdapter -ErrorAction Stop | Where-Object { $_.Status -eq 'Up' } |
            ForEach-Object { '{0} ({1})' -f $_.Name, $_.InterfaceDescription }
        if ($rows) { return @($rows) }
        return @('none')
    } catch {
        return @('unavailable')
    }
}

function Get-TrackedClean {
    Push-Location $Root
    try {
        $out = git status --porcelain --untracked-files=no
        return [string]::IsNullOrWhiteSpace($out)
    } finally {
        Pop-Location
    }
}

function Get-HeadCommit {
    Push-Location $Root
    try { return (git rev-parse HEAD).Trim() }
    finally { Pop-Location }
}

function Get-CharArgs {
    param(
        [string]$Kind,
        [string]$ArtifactDir,
        [int]$Seed,
        [int]$Nominal,
        [int]$Request,
        [int]$Response,
        [int]$Grace,
        [int]$Hard
    )
    return @(
        '--quick',
        '--no-long-characterization',
        "--transport=$Kind",
        '--seed', "$Seed",
        '--artifact-dir', $ArtifactDir,
        '--cycles', "$Nominal",
        '--request-loss-cases', "$Request",
        '--response-loss-cases', "$Response",
        '--graceful-stop-cases', "$Grace",
        '--hard-stop-cases', "$Hard",
        '--loss-cases', '0',
        '--window-samples-main', '0',
        '--window-samples-extra', '0'
    )
}

function New-EmptySampleReason([string]$Why) {
    return [ordered]@{
        run_id                         = ''
        shard_id                       = ''
        transport                      = ''
        seed                           = ''
        scenario                       = ''
        cycle_index                    = ''
        monotonic_query_start_ms       = ''
        monotonic_request_send_ms      = ''
        monotonic_response_receive_ms  = ''
        rtt_ms                         = ''
        nominal_due_ms                 = ''
        retry_start_ms                 = ''
        retry_relative_to_nominal_ms   = ''
        deadline_ms                    = ''
        state_emit_ms                  = ''
        deadline_detection_delay_ms    = ''
        guard_ms                       = ''
        guard_rtt_statistic_ms         = ''
        peer_state                     = ''
        duplicate_count                = ''
        valid                          = $false
        invalid_reason                 = $Why
    }
}

function Write-ShardSamplesFromTrace {
    param(
        [string]$ShardDir,
        [string]$ShardId,
        [string]$Kind,
        [int]$Seed,
        [string]$RunId
    )
    $trace = Join-Path $ShardDir 'bob_ping_trace.csv'
    $out = Join-Path $ShardDir 'samples.jsonl'
    $tmp = "$out.tmp"
    $lines = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path $trace)) {
        $empty = New-EmptySampleReason 'bob_ping_trace.csv was not produced; durations not invented'
        $lines.Add(($empty | ConvertTo-Json -Compress -Depth 6))
    } else {
        $rows = Import-Csv $trace
        foreach ($row in $rows) {
            $rec = [ordered]@{
                run_id                         = $RunId
                shard_id                       = $ShardId
                transport                      = $Kind
                seed                           = $Seed
                scenario                       = 'bob_ping_trace'
                cycle_index                    = $row.cycle
                monotonic_query_start_ms       = ''
                monotonic_request_send_ms      = ''
                monotonic_response_receive_ms  = ''
                rtt_ms                         = ''
                nominal_due_ms                 = ''
                retry_start_ms                 = ''
                retry_relative_to_nominal_ms   = ''
                deadline_ms                    = ''
                state_emit_ms                  = ''
                deadline_detection_delay_ms    = ''
                guard_ms                       = $(if ($row.guard_us) { [math]::Round(([double]$row.guard_us) / 1000.0, 3) } else { '' })
                guard_rtt_statistic_ms         = ''
                peer_state                     = ''
                duplicate_count                = ''
                valid                          = $true
                invalid_reason                 = ''
                event_kind                     = $row.kind
                event_steady_us                = $row.event_steady_us
                event_qpc                      = $row.event_qpc
                notes                          = 'Alice/Bob cross-process timestamps left empty; Bob event_steady_us/event_qpc are same-process monotonic fields'
            }
            $lines.Add(($rec | ConvertTo-Json -Compress -Depth 6))
        }
    }
    $lines -join "`n" | Set-Content -Path $tmp -Encoding utf8
    Move-Item -Force $tmp $out
}

function Get-Wilson95([int]$Hits, [int]$N) {
    if ($N -le 0) { return @{ lo = $null; hi = $null } }
    $z = 1.96
    $p = $Hits / [double]$N
    $z2 = $z * $z
    $den = 1.0 + ($z2 / $N)
    $center = ($p + ($z2 / (2.0 * $N))) / $den
    $margin = ($z * [math]::Sqrt((($p * (1.0 - $p)) + ($z2 / (4.0 * $N))) / $N)) / $den
    return @{ lo = [math]::Max(0, $center - $margin); hi = [math]::Min(1, $center + $margin) }
}

function Get-Percentile([double[]]$Values, [double]$P) {
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $s = $Values | Sort-Object
    $idx = [int][math]::Ceiling($P * ($s.Count - 1))
    if ($idx -lt 0) { $idx = 0 }
    if ($idx -ge $s.Count) { $idx = $s.Count - 1 }
    return $s[$idx]
}

function Get-CalibrationScenarios {
    return @(
        @{ Name = 'nominal_1'; Nominal = 1; Req = 0; Resp = 0; Grace = 0; Hard = 0; Expect = 'nominal' },
        @{ Name = 'nominal_121'; Nominal = 121; Req = 0; Resp = 0; Grace = 0; Hard = 0; Expect = 'nominal' },
        @{ Name = 'request_loss_20'; Nominal = 0; Req = 20; Resp = 0; Grace = 0; Hard = 0; Expect = 'request' },
        @{ Name = 'response_loss_20'; Nominal = 0; Req = 0; Resp = 20; Grace = 0; Hard = 0; Expect = 'response' },
        @{ Name = 'graceful_10'; Nominal = 0; Req = 0; Resp = 0; Grace = 10; Hard = 0; Expect = 'graceful' },
        @{ Name = 'hard_stop_8'; Nominal = 0; Req = 0; Resp = 0; Grace = 0; Hard = 8; Expect = 'hard' }
    )
}

function Invoke-CalibrationKind {
    param([string]$Kind, [string]$Exe)
    $kindDir = Join-Path $CalibRoot $Kind
    New-Item -ItemType Directory -Path $kindDir -Force | Out-Null
    $elapsed = [ordered]@{}
    $semantic = [ordered]@{}
    $invalid = @()
    $allPass = $true
    foreach ($sc in Get-CalibrationScenarios) {
        $dir = Join-Path $kindDir $sc.Name
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        Write-Info "Calibrate $Kind $($sc.Name)"
        $args = Get-CharArgs -Kind $Kind -ArtifactDir $dir -Seed 1 `
            -Nominal $sc.Nominal -Request $sc.Req -Response $sc.Resp -Grace $sc.Grace -Hard $sc.Hard
        $run = Invoke-TimedProcess -FilePath $Exe -ArgumentList $args `
            -LogPath (Join-Path $dir 'run.log') -TimeoutMs $CalibTimeoutMs -WorkDir $Root
        $elapsed[$sc.Name] = [math]::Round($run.ElapsedSec, 3)
        $parsed = Parse-CharReport (Join-Path $dir 'report.md')
        $fail = @()
        if ($run.TimedOut -or $run.ExitCode -ne 0) {
            $fail += "process exit=$($run.ExitCode) timeout=$($run.TimedOut)"
        }
        $fail += Test-ScenarioSemantics -Parsed $parsed -Expect $sc.Expect
        if ($fail.Count -gt 0) {
            $allPass = $false
            $semantic[$sc.Name] = 'FAIL: ' + ($fail -join '; ')
            $invalid += $fail
            Write-Info "Calibrate $Kind $($sc.Name) FAIL elapsed=$($elapsed[$sc.Name])s $($semantic[$sc.Name])"
            throw "Calibration semantic/process failure for $Kind $($sc.Name): $($semantic[$sc.Name])"
        }
        $semantic[$sc.Name] = 'PASS'
        Write-Info "Calibrate $Kind $($sc.Name) PASS elapsed=$($elapsed[$sc.Name])s"
    }
    $nom1 = [double]$elapsed.nominal_1
    $nom121 = [double]$elapsed.nominal_121
    $nominalCase = ($nom121 - $nom1) / 120.0
    $fixed = $nom1 - $nominalCase
    $fixedSuspicious = $false
    if ($fixed -lt 0) {
        $fixed = 0
        $fixedSuspicious = $true
    }
    $reqCase = ([double]$elapsed.request_loss_20 - $fixed) / 20.0
    $respCase = ([double]$elapsed.response_loss_20 - $fixed) / 20.0
    $graceCase = ([double]$elapsed.graceful_10 - $fixed) / 10.0
    $hardCase = ([double]$elapsed.hard_stop_8 - $fixed) / 8.0
    foreach ($n in @('request_loss_case_seconds', 'response_loss_case_seconds', 'graceful_stop_case_seconds', 'hard_stop_case_seconds', 'nominal_case_seconds')) {
        # keep measured values even if small; negative per-case is suspicious
    }
    return [ordered]@{
        fixed_process_seconds          = [math]::Round($fixed, 6)
        fixed_suspicious               = $fixedSuspicious
        nominal_case_seconds           = [math]::Round($nominalCase, 6)
        request_loss_case_seconds      = [math]::Round($reqCase, 6)
        response_loss_case_seconds     = [math]::Round($respCase, 6)
        graceful_stop_case_seconds     = [math]::Round($graceCase, 6)
        hard_stop_case_seconds         = [math]::Round($hardCase, 6)
        elapsed                        = $elapsed
        semantic                       = $semantic
        invalid_metrics                = $invalid
        result                         = $(if ($allPass) { 'PASS' } else { 'FAIL' })
    }
}

function New-TransportPlan($M, [int]$Nom, [int]$Req, [int]$Resp, [int]$Grace, [int]$Hard, [int]$Shards, [string]$Kind) {
    $noms = Split-Even ([int]$Nom) ([int]$Shards)
    $reqs = Split-Even ([int]$Req) ([int]$Shards)
    $resps = Split-Even ([int]$Resp) ([int]$Shards)
    $graces = Split-Even ([int]$Grace) ([int]$Shards)
    $hards = Split-Even ([int]$Hard) ([int]$Shards)
    $shardsOut = @()
    for ($i = 0; $i -lt $Shards; $i++) {
        $pred = Get-ProfileCost $M ([int]$noms[$i]) ([int]$reqs[$i]) ([int]$resps[$i]) ([int]$graces[$i]) ([int]$hards[$i]) 1 $StartsPerShard
        $timeout = Get-ShardTimeoutSeconds $pred
        $faultPred = Get-ProfileCost $M 0 ([int]$reqs[$i]) ([int]$resps[$i]) ([int]$graces[$i]) ([int]$hards[$i]) 1 1
        $nominalPred = Get-ProfileCost $M ([int]$noms[$i]) 0 0 0 0 1 1
        $id = '{0}-{1:00}' -f $Kind, ($i + 1)
        $seed = $(if ($Kind -eq 'udp') { 2000 } else { 1000 }) + $i + 1
        $shardsOut += [ordered]@{
            shard_id                 = $id
            transport                = $Kind
            seed                     = $seed
            nominal                  = [int]$noms[$i]
            request_loss             = [int]$reqs[$i]
            response_loss            = [int]$resps[$i]
            graceful_stop            = [int]$graces[$i]
            hard_stop                = [int]$hards[$i]
            predicted_seconds        = [math]::Round($pred, 3)
            timeout_seconds          = [math]::Round($timeout, 3)
            faults_predicted_seconds = [math]::Round($faultPred, 3)
            faults_timeout_seconds   = [math]::Round((Get-ShardTimeoutSeconds $faultPred), 3)
            nominal_predicted_seconds = [math]::Round($nominalPred, 3)
            nominal_timeout_seconds  = [math]::Round((Get-ShardTimeoutSeconds $nominalPred), 3)
            process_starts           = $StartsPerShard
        }
    }
    return [ordered]@{
        totals = [ordered]@{
            nominal        = $Nom
            request_loss   = $Req
            response_loss  = $Resp
            graceful_stop  = $Grace
            hard_stop      = $Hard
        }
        shards = $shardsOut
        predicted_seconds = [math]::Round((Get-ProfileCost $M $Nom $Req $Resp $Grace $Hard $Shards $StartsPerShard), 3)
    }
}

function New-LongPlan($Calib, [double]$Hours) {
    $Calib = Get-CorrectedCalib $Calib
    $requested = $Hours * 3600.0
    $reserve = 20.0 * 60.0
    $plannedTarget = $requested - $reserve
    $minBound = $PlanV2MinSeconds
    $maxBound = $PlanV2MaxSeconds
    $shards = 10
    $minNom = 12000
    $startNom = 13000
    $maxNom = 14000
    $fault = [ordered]@{ req = 300; resp = 300; grace = 200; hard = 250 }
    if (-not $Calib.tcp -and -not $Calib.udp) { throw 'Calibration is missing transport measurements.' }

    function Cost-All([int]$Nom) {
        $c = 0.0
        if ($Calib.tcp) {
            $c += Get-ProfileCost $Calib.tcp $Nom $fault.req $fault.resp $fault.grace $fault.hard $shards $StartsPerShard
        }
        if ($Calib.udp) {
            $c += Get-ProfileCost $Calib.udp $Nom $fault.req $fault.resp $fault.grace $fault.hard $shards $StartsPerShard
        }
        return $c
    }

    $nom = $startNom
    $pred = Cost-All $nom
    $assumptions = @()
    $belowMin = $false
    if ($pred -gt $maxBound) {
        while ($pred -gt $maxBound -and $nom -gt $minNom) {
            $nom = $nom - 100
            $pred = Cost-All $nom
        }
        if ($pred -gt $maxBound -and $nom -le $minNom) {
            $assumptions += "Keeping at least $minNom nominal per transport was not possible inside 9h45m with $StartsPerShard process starts/shard; using $nom."
        }
    } elseif ($pred -lt $minBound) {
        while ($pred -lt $minBound -and $nom -lt $maxNom) {
            $nom = $nom + 100
            $pred = Cost-All $nom
            if ($pred -gt $maxBound) {
                $nom = $nom - 100
                $pred = Cost-All $nom
                break
            }
        }
    }
    $nom = [int](Round-Down $nom 100)
    $pred = Cost-All $nom
    if ($pred -gt $maxBound -and $nom -ge ($minNom + 100)) {
        while ($pred -gt $maxBound -and $nom -ge ($minNom + 100)) {
            $nom = $nom - 100
            $pred = Cost-All $nom
        }
    }
    if ($nom -gt $maxNom) {
        $assumptions += "Nominal $nom exceeds $maxNom; not used."
        $nom = $maxNom
        $pred = Cost-All $nom
    }
    if ($pred -gt $maxBound) { $belowMin = $true }

    $assumptions += 'effective_nominal_case_seconds = max(observed_long_run_or_probe, calibration_nominal, 1.0s ping cadence).'
    $assumptions += 'Request/response planning uses max(calibrated loss cost, effective nominal); negative/near-zero loss estimates are rejected.'
    $assumptions += 'Graceful/hard-stop keep calibration measurements.'
    $assumptions += "Each shard runs faults then the nominal block ($StartsPerShard process starts; startup counted each time)."
    $assumptions += 'Shard timeout = max(predicted*1.25 + 5 minutes, predicted + 10 minutes).'
    $assumptions += 'TCP pre-deadline retry is measured, not a pass/fail gate.'

    $tcpPlan = $null
    $udpPlan = $null
    if ($Calib.tcp) {
        $tcpPlan = New-TransportPlan $Calib.tcp $nom $fault.req $fault.resp $fault.grace $fault.hard $shards 'tcp'
    }
    if ($Calib.udp) {
        $udpPlan = New-TransportPlan $Calib.udp $nom $fault.req $fault.resp $fault.grace $fault.hard $shards 'udp'
    }
    $order = @()
    for ($i = 1; $i -le $shards; $i++) {
        if ($tcpPlan) { $order += ('tcp-{0:00}' -f $i) }
        if ($udpPlan) { $order += ('udp-{0:00}' -f $i) }
    }
    $pred = 0.0
    if ($tcpPlan) { $pred += $tcpPlan.predicted_seconds }
    if ($udpPlan) { $pred += $udpPlan.predicted_seconds }
    return [ordered]@{
        plan_version           = 2
        requested_seconds      = $requested
        planned_target_seconds = $plannedTarget
        safety_reserve_seconds = $reserve
        min_bound_seconds      = $minBound
        max_bound_seconds      = $maxBound
        predicted_seconds      = [math]::Round($pred, 3)
        predicted_duration     = (Format-Duration $pred)
        predicted_in_range     = ($pred -ge $minBound -and $pred -le $maxBound)
        minimum_exceeds_target = $belowMin
        shard_count_per_transport = $shards
        process_starts_per_shard = $StartsPerShard
        corrected_tcp          = $(if ($Calib.tcp) { $Calib.tcp } else { $null })
        corrected_udp          = $(if ($Calib.udp) { $Calib.udp } else { $null })
        shard_order            = $order
        tcp                    = $tcpPlan
        udp                    = $udpPlan
        assumptions            = $assumptions
        later_command          = 'scripts/run_uap_long_characterization.ps1 -Run -Transport both -TargetHours 10 -NoBuild -Resume'
        runs_root              = $RunsRoot
    }
}

function New-DryPlan($Calib) {
    $Calib = Get-CorrectedCalib $Calib
    if (-not $Calib.tcp) { throw 'DryRun requires TCP calibration measurements.' }
    $m = $Calib.tcp
    $nom = 20
    $req = 2
    $resp = 2
    $grace = 2
    $hard = 2
    $pred = Get-ProfileCost $m $nom $req $resp $grace $hard 1 $StartsPerShard
    $timeout = Get-ShardTimeoutSeconds $pred
    $faultPred = Get-ProfileCost $m 0 $req $resp $grace $hard 1 1
    $nominalPred = Get-ProfileCost $m $nom 0 0 0 0 1 1
    $shard = [ordered]@{
        shard_id                  = 'tcp-test'
        transport                 = 'tcp'
        seed                      = 9101
        nominal                   = $nom
        request_loss              = $req
        response_loss             = $resp
        graceful_stop             = $grace
        hard_stop                 = $hard
        predicted_seconds         = [math]::Round($pred, 3)
        timeout_seconds           = [math]::Round($timeout, 3)
        faults_predicted_seconds  = [math]::Round($faultPred, 3)
        faults_timeout_seconds    = [math]::Round((Get-ShardTimeoutSeconds $faultPred), 3)
        nominal_predicted_seconds = [math]::Round($nominalPred, 3)
        nominal_timeout_seconds   = [math]::Round((Get-ShardTimeoutSeconds $nominalPred), 3)
        process_starts            = $StartsPerShard
    }
    return [ordered]@{
        plan_version           = 'dry-v2'
        predicted_seconds      = $shard.predicted_seconds
        predicted_duration     = (Format-Duration $pred)
        process_starts_per_shard = $StartsPerShard
        corrected_tcp          = $m
        corrected_udp          = $null
        shard_order            = @('tcp-test')
        tcp                    = [ordered]@{
            totals = [ordered]@{
                nominal = $nom; request_loss = $req; response_loss = $resp
                graceful_stop = $grace; hard_stop = $hard
            }
            shards = @($shard)
            predicted_seconds = $shard.predicted_seconds
        }
        udp                    = $null
        later_command          = 'dry-run only'
        runs_root              = (Join-Path $ArtifactRoot 'dry-run-v2')
    }
}

function Write-PlanMarkdown($Plan, [string]$Path) {
    $lines = @(
        '# UAP long characterization plan',
        "- predicted duration: $($Plan.predicted_duration) ($($Plan.predicted_seconds)s)",
        "- safety reserve: $(Format-Duration $Plan.safety_reserve_seconds)",
        "- shard count per transport: $($Plan.shard_count_per_transport)",
        "- predicted in 9h20m-9h45m window: $($Plan.predicted_in_range)",
        "- minimum profile exceeds target: $($Plan.minimum_exceeds_target)",
        ''
    )
    if ($Plan.tcp) {
        $t = $Plan.tcp.totals
        $lines += 'TCP totals:'
        $lines += "- nominal: $($t.nominal)"
        $lines += "- request-loss: $($t.request_loss)"
        $lines += "- response-loss: $($t.response_loss)"
        $lines += "- graceful-stop: $($t.graceful_stop)"
        $lines += "- hard-stop: $($t.hard_stop)"
        $lines += "- predicted: $(Format-Duration $Plan.tcp.predicted_seconds)"
        $lines += 'TCP per shard:'
        foreach ($s in $Plan.tcp.shards) {
            $lines += "- $($s.shard_id): nominal=$($s.nominal) req=$($s.request_loss) resp=$($s.response_loss) grace=$($s.graceful_stop) hard=$($s.hard_stop) seed=$($s.seed) pred=$(Format-Duration $s.predicted_seconds) timeout=$(Format-Duration $s.timeout_seconds) faults_pred=$(Format-Duration $s.faults_predicted_seconds) faults_timeout=$(Format-Duration $s.faults_timeout_seconds) nominal_pred=$(Format-Duration $s.nominal_predicted_seconds) nominal_timeout=$(Format-Duration $s.nominal_timeout_seconds)"
        }
        $lines += ''
    }
    if ($Plan.udp) {
        $t = $Plan.udp.totals
        $lines += 'UDP totals:'
        $lines += "- nominal: $($t.nominal)"
        $lines += "- request-loss: $($t.request_loss)"
        $lines += "- response-loss: $($t.response_loss)"
        $lines += "- graceful-stop: $($t.graceful_stop)"
        $lines += "- hard-stop: $($t.hard_stop)"
        $lines += "- predicted: $(Format-Duration $Plan.udp.predicted_seconds)"
        $lines += 'UDP per shard:'
        foreach ($s in $Plan.udp.shards) {
            $lines += "- $($s.shard_id): nominal=$($s.nominal) req=$($s.request_loss) resp=$($s.response_loss) grace=$($s.graceful_stop) hard=$($s.hard_stop) seed=$($s.seed) pred=$(Format-Duration $s.predicted_seconds) timeout=$(Format-Duration $s.timeout_seconds) faults_pred=$(Format-Duration $s.faults_predicted_seconds) faults_timeout=$(Format-Duration $s.faults_timeout_seconds) nominal_pred=$(Format-Duration $s.nominal_predicted_seconds) nominal_timeout=$(Format-Duration $s.nominal_timeout_seconds)"
        }
        $lines += ''
    }
    $lines += 'Shard order:'
    $lines += '- ' + ($Plan.shard_order -join ', ')
    $lines += ''
    $lines += 'Assumptions:'
    foreach ($a in $Plan.assumptions) { $lines += "- $a" }
    $lines += ''
    $lines += 'Later command (not executed by calibration):'
    $lines += $Plan.later_command
    $lines | Set-Content -Path $Path -Encoding utf8
}

function Write-PlanV2Documents($Plan, $Calib) {
    Write-JsonFile $PlanV2Json $Plan
    $lines = @()
    $lines += '# UAP long characterization plan v2'
    $lines += ''
    $lines += '## Why the original plan failed'
    $lines += 'The v1 planner used a 1-vs-121 calibration slope of ~0.55s (TCP) / ~0.43s (UDP) per nominal cycle. A 1-second ping cadence cannot be cheaper than 1.0s once startup is excluded, and 2650 nominal cycles alone need ~44m10s before fault/stop cases. v1 TCP shard timeout was 43m54s and UDP 35m37s, both below that floor, so every shard died in the mixed cycle loop. stdout never reached `Hard-stop runs=` / `Graceful-close runs=`. Timed-out shards were not marked completed.'
    $lines += ''
    $lines += '## Observed timeout durations (v1 run)'
    $stop = $null
    $stopPath = Join-Path $ArtifactRoot 'stop-record.json'
    if (Test-Path $stopPath) {
        $stop = Get-Content -Raw $stopPath | ConvertFrom-Json
        $lines += "- stop_time: $($stop.stop_time)"
        $lines += "- active_shard_at_stop: $($stop.active_shard)"
        $lines += "- wall_elapsed: $($stop.wall_elapsed_seconds)s ($(Format-Duration $stop.wall_elapsed_seconds))"
        $lines += "- parent_powershell_pid: $($stop.parent_powershell_pid) already_exited=$($stop.parent_powershell_already_exited)"
        $lines += "- coordinator_pid_tcp04: $($stop.coordinator_pid_tcp04) already_exited=$($stop.coordinator_already_exited)"
        $lines += '- killed_pids: ' + (($stop.killed_pids | ForEach-Object { '{0}({1})' -f $_.pid, $_.role }) -join ', ')
    }
    foreach ($id in @('tcp-01','udp-01','tcp-02','udp-02','tcp-03','udp-03','tcp-04')) {
        $fj = Join-Path $LegacyRunsRoot "$id\FAILED.json"
        $stdout = Join-Path $LegacyRunsRoot "$id\run.log.stdout.txt"
        $elapsed = 'n/a'
        $reasons = 'n/a'
        if (Test-Path $fj) {
            $j = Get-Content -Raw $fj | ConvertFrom-Json
            $elapsed = '{0}s ({1})' -f $j.elapsed_seconds, (Format-Duration $j.elapsed_seconds)
            $reasons = ($j.reasons -join '; ')
        }
        $last = ''
        $lastWrite = ''
        if (Test-Path $stdout) {
            $last = ((Get-Content $stdout | Where-Object { $_ }) | Select-Object -Last 1)
            $lastWrite = (Get-Item $stdout).LastWriteTime.ToString('o')
        }
        $lines += "- ${id}: elapsed=$elapsed last_stdout='$last' last_stdout_write=$lastWrite reasons=$reasons"
        $lines += "  completed nominal: not recoverable (no per-cycle output; no report.md; no bob_ping_trace.csv). last scenario: warmup complete, entered 2710 mixed logical cycles. request/response/graceful/hard-stop: no evidence they completed; graceful/hard-stop headers were never printed."
    }
    $lines += ''
    $lines += '## Timing probes (required because failed logs did not expose cycle progress)'
    $tcpP = Read-ProbeElapsed 'tcp'
    $udpP = Read-ProbeElapsed 'udp'
    $lines += "- TCP probe: elapsed=$($tcpP.elapsed_seconds)s cycles=600 raw=$($tcpP.raw_seconds_per_cycle)s/cycle exit=$($tcpP.exit_code)"
    $lines += "- UDP probe: elapsed=$($udpP.elapsed_seconds)s cycles=600 raw=$($udpP.raw_seconds_per_cycle)s/cycle exit=$($udpP.exit_code)"
    if ($Plan.corrected_tcp) {
        $c = $Plan.corrected_tcp
        $lines += "- TCP observed=(elapsed-startup)/600=$($c.observed_nominal_case_seconds)s calibration_nominal=$($c.calibration_nominal_case_seconds)s raw=$($c.raw_elapsed_per_cycle_seconds)s **effective_nominal=$($c.nominal_case_seconds)s**"
        $lines += "- TCP request/response effective=$($c.request_loss_case_seconds)/$($c.response_loss_case_seconds) (max(calibrated, effective nominal))"
        $lines += "- TCP graceful/hard-stop kept from calibration: $($c.graceful_stop_case_seconds)/$($c.hard_stop_case_seconds)"
        $lines += "- TCP startup/shard-process: $($c.fixed_process_seconds)s"
    }
    if ($Plan.corrected_udp) {
        $c = $Plan.corrected_udp
        $lines += "- UDP observed=(elapsed-startup)/600=$($c.observed_nominal_case_seconds)s calibration_nominal=$($c.calibration_nominal_case_seconds)s raw=$($c.raw_elapsed_per_cycle_seconds)s **effective_nominal=$($c.nominal_case_seconds)s**"
        $lines += "- UDP request/response effective=$($c.request_loss_case_seconds)/$($c.response_loss_case_seconds) (max(calibrated, effective nominal); negatives rejected)"
        $lines += "- UDP graceful/hard-stop kept from calibration: $($c.graceful_stop_case_seconds)/$($c.hard_stop_case_seconds)"
        $lines += "- UDP startup/shard-process: $($c.fixed_process_seconds)s"
    }
    $lines += ''
    $lines += '## Old plan under the corrected model'
    if ($Calib.tcp -and $Calib.udp -and $Plan.corrected_tcp -and $Plan.corrected_udp) {
        $oldTcp = Get-ProfileCost $Plan.corrected_tcp 26500 300 300 200 250 10 1
        $oldUdp = Get-ProfileCost $Plan.corrected_udp 26500 300 300 200 250 10 1
        $oldSum = $oldTcp + $oldUdp
        $oldTcpShard = Get-ProfileCost $Plan.corrected_tcp 2650 30 30 20 25 1 1
        $oldUdpShard = Get-ProfileCost $Plan.corrected_udp 2650 30 30 20 25 1 1
        $lines += "- TCP total: $(Format-Duration $oldTcp) ($([math]::Round($oldTcp,3))s)"
        $lines += "- UDP total: $(Format-Duration $oldUdp) ($([math]::Round($oldUdp,3))s)"
        $lines += "- combined: $(Format-Duration $oldSum) ($([math]::Round($oldSum,3))s) vs v1 predicted 9h 41m 22s"
        $lines += "- TCP shard predicted: $(Format-Duration $oldTcpShard); v1 timeout 43m54s; corrected timeout would be $(Format-Duration (Get-ShardTimeoutSeconds $oldTcpShard))"
        $lines += "- UDP shard predicted: $(Format-Duration $oldUdpShard); v1 timeout 35m37s; corrected timeout would be $(Format-Duration (Get-ShardTimeoutSeconds $oldUdpShard))"
    }
    $lines += ''
    $lines += '## New counts'
    $lines += "- process starts per shard: $($Plan.process_starts_per_shard) (faults process, then nominal process)"
    if ($Plan.tcp) {
        $t = $Plan.tcp.totals
        $s0 = $Plan.tcp.shards[0]
        $lines += "- TCP: nominal=$($t.nominal) req=$($t.request_loss) resp=$($t.response_loss) grace=$($t.graceful_stop) hard=$($t.hard_stop) shards=10"
        $lines += "- TCP shard predicted: $(Format-Duration $s0.predicted_seconds) ($($s0.predicted_seconds)s)"
        $lines += "- TCP shard timeout: $(Format-Duration $s0.timeout_seconds) ($($s0.timeout_seconds)s)"
        $lines += "- TCP faults stage predicted/timeout: $(Format-Duration $s0.faults_predicted_seconds) / $(Format-Duration $s0.faults_timeout_seconds)"
        $lines += "- TCP nominal stage predicted/timeout: $(Format-Duration $s0.nominal_predicted_seconds) / $(Format-Duration $s0.nominal_timeout_seconds)"
        $lines += "- TCP transport predicted: $(Format-Duration $Plan.tcp.predicted_seconds)"
    }
    if ($Plan.udp) {
        $t = $Plan.udp.totals
        $s0 = $Plan.udp.shards[0]
        $lines += "- UDP: nominal=$($t.nominal) req=$($t.request_loss) resp=$($t.response_loss) grace=$($t.graceful_stop) hard=$($t.hard_stop) shards=10"
        $lines += "- UDP shard predicted: $(Format-Duration $s0.predicted_seconds) ($($s0.predicted_seconds)s)"
        $lines += "- UDP shard timeout: $(Format-Duration $s0.timeout_seconds) ($($s0.timeout_seconds)s)"
        $lines += "- UDP faults stage predicted/timeout: $(Format-Duration $s0.faults_predicted_seconds) / $(Format-Duration $s0.faults_timeout_seconds)"
        $lines += "- UDP nominal stage predicted/timeout: $(Format-Duration $s0.nominal_predicted_seconds) / $(Format-Duration $s0.nominal_timeout_seconds)"
        $lines += "- UDP transport predicted: $(Format-Duration $Plan.udp.predicted_seconds)"
    }
    $lines += "- total predicted runtime: $($Plan.predicted_duration) ($($Plan.predicted_seconds)s)"
    $lines += "- safety margin / window: $(Format-Duration $Plan.min_bound_seconds) to $(Format-Duration $Plan.max_bound_seconds); reserve=$(Format-Duration $Plan.safety_reserve_seconds); in_range=$($Plan.predicted_in_range)"
    $lines += "- future artifacts: $($Plan.runs_root) (does not overwrite artifacts/uap-long/runs)"
    $lines += ''
    $lines += '## Scenario ordering'
    $lines += 'Each shard runs warmup+request-loss+response-loss+graceful-stop+hard-stop in the faults process (existing executable order: mixed loss cycles, then hard-stop, then graceful), then the long nominal block in a second process. Fault semantics are collected before the long nominal section. Aggregate reports keep nominal and fault distributions separate.'
    $lines += ''
    $lines += '## Future 10-hour command (not started)'
    $lines += $Plan.later_command
    $lines | Set-Content -Path $PlanV2Md -Encoding utf8
    Write-Info "Wrote $PlanV2Md"
    Write-Info "Wrote $PlanV2Json"
}

function Write-CalibrationReport($Calib, $Plan, [string]$Path) {
    $tcpInv = 'none'
    $udpInv = 'none'
    if ($Calib.tcp -and $Calib.tcp.invalid_metrics -and $Calib.tcp.invalid_metrics.Count -gt 0) {
        $tcpInv = $Calib.tcp.invalid_metrics -join '; '
    }
    if ($Calib.udp -and $Calib.udp.invalid_metrics -and $Calib.udp.invalid_metrics.Count -gt 0) {
        $udpInv = $Calib.udp.invalid_metrics -join '; '
    }
    $overall = 'PASS'
    if ($Calib.overall -ne 'PASS') { $overall = $Calib.overall }
    $lines = @(
        '# UAP Long Characterization Calibration',
        'Source:',
        "- commit: $($Calib.commit)",
        "- tracked working tree clean: $($Calib.tracked_working_tree_clean)",
        "- TCP executable: $($Calib.tcp_exe)",
        "- UDP executable: $($Calib.udp_exe)",
        "- build type: $($Calib.build_type)",
        "- build performed: $($Calib.build_performed)",
        'Environment:',
        "- timestamp: $($Calib.timestamp)",
        "- Windows power scheme: $($Calib.power_scheme)",
        "- active network adapters: $($Calib.active_network_adapters -join '; ')",
        "- system sleep prevention used: $($Calib.system_sleep_prevention_used)",
        'TCP measured duration:'
    )
    if ($Calib.tcp) {
        $lines += "- fixed process cost: $($Calib.tcp.fixed_process_seconds)s$(if ($Calib.tcp.fixed_suspicious) { ' (clamped from negative; suspicious)' } else { '' })"
        $lines += "- nominal per case: $($Calib.tcp.nominal_case_seconds)s"
        $lines += "- request-loss per case: $($Calib.tcp.request_loss_case_seconds)s"
        $lines += "- response-loss per case: $($Calib.tcp.response_loss_case_seconds)s"
        $lines += "- graceful-stop per case: $($Calib.tcp.graceful_stop_case_seconds)s"
        $lines += "- hard-stop per case: $($Calib.tcp.hard_stop_case_seconds)s"
    } else {
        $lines += '- not run'
    }
    $lines += 'UDP measured duration:'
    if ($Calib.udp) {
        $lines += "- fixed process cost: $($Calib.udp.fixed_process_seconds)s$(if ($Calib.udp.fixed_suspicious) { ' (clamped from negative; suspicious)' } else { '' })"
        $lines += "- nominal per case: $($Calib.udp.nominal_case_seconds)s"
        $lines += "- request-loss per case: $($Calib.udp.request_loss_case_seconds)s"
        $lines += "- response-loss per case: $($Calib.udp.response_loss_case_seconds)s"
        $lines += "- graceful-stop per case: $($Calib.udp.graceful_stop_case_seconds)s"
        $lines += "- hard-stop per case: $($Calib.udp.hard_stop_case_seconds)s"
    } else {
        $lines += '- not run'
    }
    $lines += 'Calculated 10-hour plan:'
    $lines += "- predicted duration: $($Plan.predicted_duration)"
    $lines += "- safety reserve: $(Format-Duration $Plan.safety_reserve_seconds)"
    $lines += "- shard count: $($Plan.shard_count_per_transport) per transport"
    if ($Plan.tcp) {
        $t = $Plan.tcp.totals
        $lines += "- total and per-shard counts for TCP: total nominal=$($t.nominal) req=$($t.request_loss) resp=$($t.response_loss) grace=$($t.graceful_stop) hard=$($t.hard_stop); shards=$($Plan.shard_count_per_transport)"
    }
    if ($Plan.udp) {
        $t = $Plan.udp.totals
        $lines += "- total and per-shard counts for UDP: total nominal=$($t.nominal) req=$($t.request_loss) resp=$($t.response_loss) grace=$($t.graceful_stop) hard=$($t.hard_stop); shards=$($Plan.shard_count_per_transport)"
    }
    $lines += "- predicted duration range: $(Format-Duration $Plan.min_bound_seconds) to $(Format-Duration $Plan.max_bound_seconds) (in range: $($Plan.predicted_in_range))"
    $lines += '- assumptions: ' + ($Plan.assumptions -join ' ')
    $lines += 'Calibration semantic result:'
    $lines += "- TCP: $(if ($Calib.tcp) { $Calib.tcp.result } else { 'n/a' })"
    $lines += "- UDP: $(if ($Calib.udp) { $Calib.udp.result } else { 'n/a' })"
    $lines += "- invalid metrics: TCP=$tcpInv; UDP=$udpInv"
    $lines += "- overall: $overall"
    $lines | Set-Content -Path $Path -Encoding utf8
}

function Test-ShardComplete([string]$Dir) {
    $marker = Join-Path $Dir 'COMPLETED.json'
    $samples = Join-Path $Dir 'samples.jsonl'
    $report = Join-Path $Dir 'report.md'
    if (-not ((Test-Path $marker) -and (Test-Path $samples) -and (Test-Path $report))) {
        return $false
    }
    try {
        $j = Get-Content -Raw $marker | ConvertFrom-Json
        return ($j.ok -eq $true)
    } catch {
        return $false
    }
}

function Assert-LongPlanGates($Plan) {
    $fail = @()
    $minSec = $PlanV2MinSeconds
    $maxSec = $PlanV2MaxSeconds
    $pred = [double]$Plan.predicted_seconds
    if ($pred -lt $minSec -or $pred -gt $maxSec) {
        $fail += "predicted duration $($Plan.predicted_duration) ($pred s) is outside 9h20m-9h45m"
    }
    if ([int]$Plan.shard_count_per_transport -ne 10) {
        $fail += "shard_count_per_transport=$($Plan.shard_count_per_transport) want 10"
    }
    $wantFault = @{
        request_loss   = 300
        response_loss  = 300
        graceful_stop  = 200
        hard_stop      = 250
    }
    $allDirs = @()
    foreach ($kind in @('tcp', 'udp')) {
        $t = $Plan.$kind
        if (-not $t) {
            $fail += "missing $kind plan"
            continue
        }
        $shards = @($t.shards)
        if ($shards.Count -ne 10) {
            $fail += "$kind shard count $($shards.Count) want 10"
        }
        foreach ($k in @('request_loss', 'response_loss', 'graceful_stop', 'hard_stop')) {
            if ([int]$t.totals.$k -ne $wantFault[$k]) {
                $fail += "$kind total ${k}=$($t.totals.$k) want $($wantFault[$k])"
            }
        }
        $nom = [int]$t.totals.nominal
        if ($nom -lt 12000 -or $nom -gt 14000) {
            $fail += "$kind nominal $nom is outside 12000-14000"
        }
        if (($nom % 100) -ne 0) {
            $fail += "$kind nominal $nom is not a multiple of 100"
        }
        $base = $(if ($kind -eq 'tcp') { 1000 } else { 2000 })
        for ($i = 0; $i -lt $shards.Count; $i++) {
            $s = $shards[$i]
            $expectId = '{0}-{1:00}' -f $kind, ($i + 1)
            if ($s.shard_id -ne $expectId) {
                $fail += "$kind shard id $($s.shard_id) want $expectId"
            }
            if ([int]$s.seed -ne ($base + $i + 1)) {
                $fail += "$($s.shard_id) seed $($s.seed) want $($base + $i + 1)"
            }
            if (([int]$s.nominal % 10) -ne 0) {
                $fail += "$($s.shard_id) per-shard nominal $($s.nominal) is not a multiple of 10"
            }
            $expectTo = Get-ShardTimeoutSeconds ([double]$s.predicted_seconds)
            if ([math]::Abs([double]$s.timeout_seconds - $expectTo) -gt 1.5) {
                $fail += "$($s.shard_id) timeout $($s.timeout_seconds) != $([math]::Round($expectTo,3))"
            }
            if ([double]$s.timeout_seconds -le [double]$s.predicted_seconds) {
                $fail += "$($s.shard_id) timeout $($s.timeout_seconds) is not greater than predicted $($s.predicted_seconds)"
            }
            $dir = Join-Path $RunsRoot $s.shard_id
            $allDirs += $dir
            if ($dir.StartsWith(($CalibRoot.TrimEnd('\') + '\'), [System.StringComparison]::OrdinalIgnoreCase) -or
                $dir.Equals($CalibRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                $fail += "$($s.shard_id) output directory is under calibration"
            }
        }
    }
    $expectOrder = @()
    for ($i = 1; $i -le 10; $i++) {
        $expectOrder += ('tcp-{0:00}' -f $i)
        $expectOrder += ('udp-{0:00}' -f $i)
    }
    $gotOrder = @($Plan.shard_order)
    if (($gotOrder -join ',') -ne ($expectOrder -join ',')) {
        $fail += "shard_order mismatch: $($gotOrder -join ',')"
    }
    if ((@($allDirs | Select-Object -Unique)).Count -ne $allDirs.Count) {
        $fail += 'shard output directories are not distinct'
    }
    if ($RunsRoot.Equals($CalibRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $fail += 'runs root equals calibration root'
    }
    $probe = Join-Path ([System.IO.Path]::GetTempPath()) ('uap-complete-check-' + [guid]::NewGuid().ToString('n'))
    New-Item -ItemType Directory -Path $probe | Out-Null
    try {
        if (Test-ShardComplete $probe) {
            $fail += 'Test-ShardComplete true on empty directory'
        }
        Set-Content -Path (Join-Path $probe 'COMPLETED.json') -Value '{"ok":true}' -Encoding utf8
        if (Test-ShardComplete $probe) {
            $fail += 'Test-ShardComplete true without samples.jsonl and report.md'
        }
        Set-Content -Path (Join-Path $probe 'samples.jsonl') -Value '{}' -Encoding utf8
        Set-Content -Path (Join-Path $probe 'report.md') -Value '# x' -Encoding utf8
        if (-not (Test-ShardComplete $probe)) {
            $fail += 'Test-ShardComplete false when COMPLETED.json ok=true plus samples.jsonl and report.md'
        }
        Set-Content -Path (Join-Path $probe 'COMPLETED.json') -Value '{"ok":false}' -Encoding utf8
        if (Test-ShardComplete $probe) {
            $fail += 'Test-ShardComplete true when COMPLETED.json ok=false'
        }
    } finally {
        Remove-Item -Recurse -Force $probe -ErrorAction SilentlyContinue
    }
    if ($fail.Count -gt 0) {
        throw ('Plan gates failed: ' + ($fail -join '; '))
    }
    Write-Info 'Plan gates passed'
}

function Invoke-LongRun($Plan, $Exes) {
    $powerLine = Get-PowerLineStatus
    if ($powerLine -ne 'Online') {
        throw "Refusing to launch: power source is $powerLine (AC power required)."
    }
    New-Item -ItemType Directory -Path $RunsRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $AggRoot -Force | Out-Null
    $shardMap = @{}
    if ($Plan.tcp) { foreach ($s in $Plan.tcp.shards) { $shardMap[$s.shard_id] = $s } }
    if ($Plan.udp) { foreach ($s in $Plan.udp.shards) { $shardMap[$s.shard_id] = $s } }
    $order = @($Plan.shard_order)
    $started = Get-Date
    $plannedFinish = $started.AddSeconds([double]$Plan.predicted_seconds)
    $tcpExe = $(if ($Exes.ContainsKey('tcp')) { $Exes['tcp'] } else { '' })
    $udpExe = $(if ($Exes.ContainsKey('udp')) { $Exes['udp'] } else { '' })
    $tcpHash = $(if ($tcpExe) { Get-ExeSha256 $tcpExe } else { '' })
    $udpHash = $(if ($udpExe) { Get-ExeSha256 $udpExe } else { '' })
    $tcpMtime = $(if ($tcpExe) { (Get-Item -LiteralPath $tcpExe).LastWriteTime.ToString('o') } else { '' })
    $udpMtime = $(if ($udpExe) { (Get-Item -LiteralPath $udpExe).LastWriteTime.ToString('o') } else { '' })
    $calibCommit = ''
    if (Test-Path $CalibJson) {
        try { $calibCommit = [string]((Get-Content -Raw $CalibJson | ConvertFrom-Json).commit) } catch { $calibCommit = '' }
    }
    $runInfo = [ordered]@{
        source_commit = Get-HeadCommit
        calibration_commit = $calibCommit
        calibration_report = $CalibReport
        runner_pid = $PID
        started_at = $started.ToString('o')
        planned_finish_at = $plannedFinish.ToString('o')
        predicted_duration = $Plan.predicted_duration
        first_shard = $(if ($order.Count -gt 0) { $order[0] } else { '' })
        shard_order = $order
        tcp_exe = $tcpExe
        udp_exe = $udpExe
        tcp_exe_sha256 = $tcpHash
        udp_exe_sha256 = $udpHash
        tcp_exe_mtime = $tcpMtime
        udp_exe_mtime = $udpMtime
        cmake_build_type_tcp = $(if ($tcpExe) { Get-CMakeBuildType (Find-BuildDir 'tcp') } else { '' })
        cmake_build_type_udp = $(if ($udpExe) { Get-CMakeBuildType (Find-BuildDir 'udp') } else { '' })
        power_scheme = Get-PowerScheme
        power_line_status = $powerLine
        system_sleep_prevention = 'SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED); display sleep is not prevented'
        status_path = $StatusPath
    }
    Write-JsonFile $RunInfoPath $runInfo
    $status = [ordered]@{
        state = 'running'
        runner_pid = $PID
        source_commit = $runInfo.source_commit
        current_shard = $null
        last_completed_shard = $null
        shards_total = $order.Count
        shards_completed = 0
        shards_failed = 0
        shards_skipped_resume = 0
        started_at = $runInfo.started_at
        planned_finish_at = $runInfo.planned_finish_at
        predicted_duration = $Plan.predicted_duration
        first_shard = $runInfo.first_shard
        tcp_exe_sha256 = $tcpHash
        udp_exe_sha256 = $udpHash
        note = 'Resume skips a shard only when COMPLETED.json has ok=true and samples.jsonl and report.md exist.'
    }
    Write-JsonFile $StatusPath $status
    $anyFail = $false
    foreach ($id in $order) {
        $s = $shardMap[$id]
        if (-not $s) { continue }
        $dir = Join-Path $RunsRoot $id
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        if ($Resume -and (Test-ShardComplete $dir)) {
            Write-Info "Resume skip $id"
            $status.shards_skipped_resume = [int]$status.shards_skipped_resume + 1
            $status.shards_completed = [int]$status.shards_completed + 1
            $status.last_completed_shard = $id
            $status.current_shard = $null
            Write-JsonFile $StatusPath $status
            continue
        }
        $status.current_shard = $id
        $status.state = 'running'
        Write-JsonFile $StatusPath $status
        $kind = $s.transport
        $exe = $Exes[$kind]
        $progressPath = Join-Path $dir 'progress.json'
        $nomS = $PingCadenceSeconds
        if ($Plan.corrected_tcp -and $kind -eq 'tcp') { $nomS = [double]$Plan.corrected_tcp.nominal_case_seconds }
        if ($Plan.corrected_udp -and $kind -eq 'udp') { $nomS = [double]$Plan.corrected_udp.nominal_case_seconds }
        $stages = @(
            @{
                Name = 'faults'
                Dir = Join-Path $dir 'stages\faults'
                Nominal = 0
                Request = [int]$s.request_loss
                Response = [int]$s.response_loss
                Grace = [int]$s.graceful_stop
                Hard = [int]$s.hard_stop
                TimeoutSec = $(if ($s.faults_timeout_seconds) { [double]$s.faults_timeout_seconds } else { Get-ShardTimeoutSeconds ([double]$s.predicted_seconds) })
                Scenario = 'faults'
                Target = [int]$s.request_loss + [int]$s.response_loss + [int]$s.graceful_stop + [int]$s.hard_stop
            },
            @{
                Name = 'nominal'
                Dir = Join-Path $dir 'stages\nominal'
                Nominal = [int]$s.nominal
                Request = 0
                Response = 0
                Grace = 0
                Hard = 0
                TimeoutSec = $(if ($s.nominal_timeout_seconds) { [double]$s.nominal_timeout_seconds } else { Get-ShardTimeoutSeconds ([double]$s.predicted_seconds) })
                Scenario = 'nominal'
                Target = [int]$s.nominal
            }
        )
        Write-Info "Run shard $id faults-then-nominal shard_timeout=$(Format-Duration $s.timeout_seconds)"
        $fail = @()
        $stageElapsed = 0.0
        $timedOut = $false
        $exitCode = 0
        foreach ($st in $stages) {
            New-Item -ItemType Directory -Path $st.Dir -Force | Out-Null
            if ($Resume -and (Test-ShardComplete $st.Dir)) {
                Write-Info "Resume skip $id stage=$($st.Name)"
                continue
            }
            $args = Get-CharArgs -Kind $kind -ArtifactDir $st.Dir -Seed ([int]$s.seed) `
                -Nominal ([int]$st.Nominal) -Request ([int]$st.Request) `
                -Response ([int]$st.Response) -Grace ([int]$st.Grace) `
                -Hard ([int]$st.Hard)
            Write-JsonFile (Join-Path $st.Dir 'args.json') ([ordered]@{
                shard = $s
                stage = $st.Name
                arguments = $args
            })
            $pb = @{
                shard_id = $id
                scenario = [string]$st.Scenario
                target = [int]$st.Target
                completed_count = 0
            }
            Write-JsonFile $progressPath ([ordered]@{
                shard_id = $id
                scenario = [string]$st.Scenario
                completed_count = 0
                target_count = [int]$st.Target
                elapsed_seconds = 0
                rtt_summary = $null
                last_sample_timestamp = (Get-Date).ToString('o')
                estimated = $false
                note = "starting $($st.Name)"
            })
            $timeoutMs = [int]([double]$st.TimeoutSec * 1000.0)
            Write-Info "Run shard $id stage=$($st.Name) timeout=$(Format-Duration $st.TimeoutSec)"
            $run = Invoke-TimedProcess -FilePath $exe -ArgumentList $args `
                -LogPath (Join-Path $st.Dir 'run.log') -TimeoutMs $timeoutMs -WorkDir $Root `
                -ProgressPath $progressPath -ProgressBase $pb -SecondsPerCycle $nomS -ProgressEveryCycles 100
            $stageElapsed += [double]$run.ElapsedSec
            $parsed = Parse-CharReport (Join-Path $st.Dir 'report.md')
            $stageFail = @()
            if ($run.TimedOut) { $stageFail += "$($st.Name) timeout"; $timedOut = $true }
            if ($run.ExitCode -ne 0 -and -not $run.TimedOut) { $stageFail += "$($st.Name) exit $($run.ExitCode)" }
            $exitCode = $run.ExitCode
            if ($null -ne $parsed.Duplicates -and $parsed.Duplicates -ne 0) { $stageFail += "duplicates=$($parsed.Duplicates)" }
            if ($null -ne $parsed.LiveMissed -and $parsed.LiveMissed -ne 0) { $stageFail += "live_false_MissedDeadline=$($parsed.LiveMissed)" }
            if ($null -ne $parsed.LiveUnknown -and $parsed.LiveUnknown -ne 0) { $stageFail += "live_false_Unknown=$($parsed.LiveUnknown)" }
            foreach ($reason in $parsed.InvalidReasons) { $stageFail += $reason }
            if ([int]$st.Request -gt 0 -and ($parsed.Request.N -le 0 -or $parsed.Request.Hit -lt $parsed.Request.N)) {
                $stageFail += "request-loss $($parsed.Request.Hit)/$($parsed.Request.N)"
            }
            if ([int]$st.Response -gt 0 -and ($parsed.Response.N -le 0 -or $parsed.Response.Hit -lt $parsed.Response.N)) {
                $stageFail += "response-loss $($parsed.Response.Hit)/$($parsed.Response.N)"
            }
            if ([int]$st.Grace -gt 0 -and ($parsed.Graceful.N -le 0 -or $parsed.Graceful.Hit -lt $parsed.Graceful.N)) {
                $stageFail += "graceful $($parsed.Graceful.Hit)/$($parsed.Graceful.N)"
            }
            if ([int]$st.Hard -gt 0 -and ($parsed.HardStop.N -le 0 -or $parsed.HardStop.Hit -lt $parsed.HardStop.N)) {
                $stageFail += "hard-stop $($parsed.HardStop.Hit)/$($parsed.HardStop.N)"
            }
            if ($stageFail.Count -eq 0) {
                Write-ShardSamplesFromTrace -ShardDir $st.Dir -ShardId $id -Kind $kind -Seed ([int]$s.seed) -RunId "$id-$($st.Name)"
                Write-JsonFile (Join-Path $st.Dir 'COMPLETED.json') ([ordered]@{
                    ok = $true
                    shard_id = $id
                    stage = $st.Name
                    elapsed_seconds = $run.ElapsedSec
                    exit_code = $run.ExitCode
                })
                Write-JsonFile $progressPath ([ordered]@{
                    shard_id = $id
                    scenario = [string]$st.Scenario
                    completed_count = [int]$st.Target
                    target_count = [int]$st.Target
                    elapsed_seconds = [math]::Round($run.ElapsedSec, 3)
                    last_sample_timestamp = (Get-Date).ToString('o')
                    estimated = $false
                    note = "$($st.Name) completed"
                })
                Write-Info "Shard $id stage=$($st.Name) PASS"
            } else {
                $fail += $stageFail
                Write-JsonFile (Join-Path $st.Dir 'FAILED.json') ([ordered]@{
                    ok = $false
                    shard_id = $id
                    stage = $st.Name
                    elapsed_seconds = $run.ElapsedSec
                    exit_code = $run.ExitCode
                    timed_out = $run.TimedOut
                    reasons = $stageFail
                })
                Write-Info "Shard $id stage=$($st.Name) FAIL $($stageFail -join '; ')"
                break
            }
        }
        $faultParsed = Parse-CharReport (Join-Path $dir 'stages\faults\report.md')
        $nomParsed = Parse-CharReport (Join-Path $dir 'stages\nominal\report.md')
        $reportLines = @(
            "# UAP shard $id",
            "Stages: faults then nominal. Calibration logs are not mixed in.",
            "- single_request_loss_recovery: $($faultParsed.Request.Hit) ($($faultParsed.Request.Hit)/$($faultParsed.Request.N))",
            "- single_response_loss_recovery: $($faultParsed.Response.Hit) ($($faultParsed.Response.Hit)/$($faultParsed.Response.N))",
            "- graceful_unknown_detection_rate: $($faultParsed.Graceful.Hit) ($($faultParsed.Graceful.Hit)/$($faultParsed.Graceful.N))",
            "- missed_deadline_detection_rate: $($faultParsed.HardStop.Hit) ($($faultParsed.HardStop.Hit)/$($faultParsed.HardStop.N))",
            "- live_false_MissedDeadline: $([int]$faultParsed.LiveMissed + [int]$nomParsed.LiveMissed)",
            "- live_false_Unknown: $([int]$faultParsed.LiveUnknown + [int]$nomParsed.LiveUnknown)",
            "- duplicates: $([int]$faultParsed.Duplicates + [int]$nomParsed.Duplicates)",
            "- nominal_report_exists: $($nomParsed.Exists)",
            "Hard-stop timing is Test-harness MissedDeadline detection latency, not a production SLA.",
            "Semantic results: hard-stop MissedDeadline/state 1; graceful-stop Unknown/state 2."
        )
        $reportLines | Set-Content -Path (Join-Path $dir 'report.md') -Encoding utf8
        $sampleOut = Join-Path $dir 'samples.jsonl'
        $sampleParts = @()
        foreach ($sn in @('faults', 'nominal')) {
            $p = Join-Path $dir "stages\$sn\samples.jsonl"
            if (Test-Path $p) { $sampleParts += Get-Content $p }
        }
        if ($sampleParts.Count -gt 0) {
            $tmp = "$sampleOut.tmp"
            $sampleParts | Set-Content -Path $tmp -Encoding utf8
            Move-Item -Force $tmp $sampleOut
        }
        $ok = ($fail.Count -eq 0)
        if ($ok) {
            Write-JsonFile (Join-Path $dir 'COMPLETED.json') ([ordered]@{
                ok = $true
                shard_id = $id
                elapsed_seconds = $stageElapsed
                exit_code = $exitCode
            })
            Write-Info "Shard $id PASS"
            $status.shards_completed = [int]$status.shards_completed + 1
            $status.last_completed_shard = $id
        } else {
            $anyFail = $true
            Write-JsonFile (Join-Path $dir 'FAILED.json') ([ordered]@{
                ok = $false
                shard_id = $id
                elapsed_seconds = $stageElapsed
                exit_code = $exitCode
                timed_out = $timedOut
                reasons = $fail
            })
            Write-Info "Shard $id FAIL $($fail -join '; ')"
            $status.shards_failed = [int]$status.shards_failed + 1
        }
        $status.current_shard = $null
        Write-JsonFile $StatusPath $status
    }
    Write-LongAggregate $Plan
    $status.state = $(if ($anyFail) { 'failed' } else { 'completed' })
    $status.ended_at = (Get-Date).ToString('o')
    $status.exit_code = $(if ($anyFail) { 1 } else { 0 })
    Write-JsonFile $StatusPath $status
    if ($anyFail) { exit 1 }
    exit 0
}

function Write-LongAggregate($Plan) {
    New-Item -ItemType Directory -Path $AggRoot -Force | Out-Null
    $summary = [ordered]@{
        note = 'Long-run aggregate. Calibration logs are not mixed in.'
        tcp = $null
        udp = $null
    }
    foreach ($kind in @('tcp', 'udp')) {
        $planT = $Plan.$kind
        if (-not $planT) { continue }
        $dup = 0; $liveM = 0; $liveU = 0
        $reqHit = 0; $reqN = 0; $respHit = 0; $respN = 0
        $graceHit = 0; $graceN = 0; $hardHit = 0; $hardN = 0
        $timeouts = 0; $crashes = 0; $incomplete = 0
        $shardRows = @()
        foreach ($s in $planT.shards) {
            $dir = Join-Path $RunsRoot $s.shard_id
            $faultReport = Join-Path $dir 'stages\faults\report.md'
            $parsed = Parse-CharReport $(if (Test-Path $faultReport) { $faultReport } else { Join-Path $dir 'report.md' })
            $complete = Test-ShardComplete $dir
            if (-not $complete) { $incomplete++ }
            if (Test-Path (Join-Path $dir 'FAILED.json')) {
                $fj = Get-Content -Raw (Join-Path $dir 'FAILED.json') | ConvertFrom-Json
                if ($fj.timed_out) { $timeouts++ } elseif ($fj.exit_code -ne 0) { $crashes++ }
            }
            if ($parsed.Duplicates) { $dup += $parsed.Duplicates }
            if ($parsed.LiveMissed) { $liveM += $parsed.LiveMissed }
            if ($parsed.LiveUnknown) { $liveU += $parsed.LiveUnknown }
            $reqHit += $parsed.Request.Hit; $reqN += $parsed.Request.N
            $respHit += $parsed.Response.Hit; $respN += $parsed.Response.N
            $graceHit += $parsed.Graceful.Hit; $graceN += $parsed.Graceful.N
            $hardHit += $parsed.HardStop.Hit; $hardN += $parsed.HardStop.N
            $shardRows += "- $($s.shard_id) complete=$complete grace=$($parsed.Graceful.Hit)/$($parsed.Graceful.N) hard=$($parsed.HardStop.Hit)/$($parsed.HardStop.N)"
        }
        $wReq = Get-Wilson95 $reqHit $reqN
        $wResp = Get-Wilson95 $respHit $respN
        $wGrace = Get-Wilson95 $graceHit $graceN
        $wHard = Get-Wilson95 $hardHit $hardN
        $md = @(
            "# UAP long characterization $kind",
            '',
            '## Measurement distinctions',
            '- Nominal steady-state RTT: warmup min/p99 in each shard report.md (characterization clocks). Not a production SLA and not treated as a long-run p99.9 distribution.',
            '- Request-loss recovery latency: shard report single_request_loss_recovery and Offline drop_request row.',
            '- Response-loss recovery latency: shard report single_response_loss_recovery and Offline ignore_response row.',
            '- Graceful-stop detection: expected semantic result Unknown, state 2.',
            '- Hard-stop detection: expected semantic result MissedDeadline, state 1. Timing is Test-harness MissedDeadline detection latency from the existing --quick poll/restart path. It is not a general production SLA or protocol deadline guarantee.',
            '- Invalid samples are retained in shard artifacts. Durations are not invented from timestamps in different processes or clock domains. Calibration logs are excluded.',
            '',
            'Retry timing: retries_before_nominal / retries_after_nominal are summed from shard reports. TCP pre-deadline retry is reported, not a fail gate.',
            "Request-loss recovery: $reqHit/$reqN Wilson95=[$($wReq.lo),$($wReq.hi)]",
            "Response-loss recovery: $respHit/$respN Wilson95=[$($wResp.lo),$($wResp.hi)]",
            "Graceful-stop detection (Unknown, state 2): $graceHit/$graceN Wilson95=[$($wGrace.lo),$($wGrace.hi)]",
            "Test-harness MissedDeadline detection latency / hard-stop (MissedDeadline, state 1): $hardHit/$hardN Wilson95=[$($wHard.lo),$($wHard.hi)]",
            "Safety: duplicates=$dup live_false_MissedDeadline=$liveM live_false_Unknown=$liveU timeouts=$timeouts crashes=$crashes incomplete=$incomplete",
            'Shards:'
        ) + $shardRows
        $md | Set-Content -Path (Join-Path $AggRoot "$kind-report.md") -Encoding utf8
        $summary[$kind] = [ordered]@{
            request_loss = "$reqHit/$reqN"
            response_loss = "$respHit/$respN"
            graceful = "$graceHit/$graceN"
            hard_stop = "$hardHit/$hardN"
            duplicates = $dup
            live_false_missed = $liveM
            live_false_unknown = $liveU
            timeouts = $timeouts
            crashes = $crashes
            incomplete = $incomplete
        }
    }
    @(
        '# TCP vs UDP comparison',
        'Calibration logs are excluded.',
        'Hard-stop timing is Test-harness MissedDeadline detection latency, not a production SLA.',
        'Semantic results: hard-stop MissedDeadline/state 1; graceful-stop Unknown/state 2.',
        "- TCP: $($summary.tcp | ConvertTo-Json -Compress)",
        "- UDP: $($summary.udp | ConvertTo-Json -Compress)"
    ) | Set-Content -Path (Join-Path $AggRoot 'comparison.md') -Encoding utf8
    Write-JsonFile (Join-Path $AggRoot 'summary.json') $summary
}

# --- main ---
$sleepOn = $false
try {
    [void][UapNativeSleep]::SetThreadExecutionState($ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED)
    $sleepOn = $true

    New-Item -ItemType Directory -Path $CalibRoot -Force | Out-Null
    $exes = @{}
    foreach ($kind in Get-SelectedTransports) {
        $exes[$kind] = Resolve-CharExe $kind
        Write-Info "$kind exe: $($exes[$kind])"
    }

    if ($Calibrate) {
        $calibSw = [System.Diagnostics.Stopwatch]::StartNew()
        $calib = [ordered]@{
            commit = Get-HeadCommit
            tracked_working_tree_clean = Get-TrackedClean
            tcp_exe = $(if ($exes.ContainsKey('tcp')) { $exes['tcp'] } else { '' })
            udp_exe = $(if ($exes.ContainsKey('udp')) { $exes['udp'] } else { '' })
            build_type = 'Release'
            build_performed = $false
            timestamp = (Get-Date).ToString('o')
            power_scheme = Get-PowerScheme
            active_network_adapters = @(Get-ActiveAdapters)
            system_sleep_prevention_used = $true
            tcp = $null
            udp = $null
            overall = 'PASS'
        }
        foreach ($kind in Get-SelectedTransports) {
            $meas = Invoke-CalibrationKind -Kind $kind -Exe $exes[$kind]
            $calib[$kind] = $meas
            if ($meas.result -ne 'PASS') { $calib.overall = 'FAIL' }
        }
        $calibSw.Stop()
        $calib.elapsed_calibration_seconds = [math]::Round($calibSw.Elapsed.TotalSeconds, 3)
        Write-JsonFile $CalibJson $calib
        $plan = New-LongPlan $calib $TargetHours
        Write-JsonFile $PlanJson $plan
        Write-PlanMarkdown $plan $PlanMd
        Write-CalibrationReport $calib $plan $CalibReport
        Write-Info "Calibration elapsed $(Format-Duration $calib.elapsed_calibration_seconds)"
        Write-Info "Wrote $CalibReport"
        Write-Info "Wrote $PlanMd"
        if ($calib.overall -ne 'PASS') { exit 1 }
        exit 0
    }

    if (-not (Test-Path $CalibJson)) {
        throw 'plan/run requires artifacts/uap-long/calibration/calibration.json; run -Calibrate first.'
    }
    $calib = Get-Content -Raw $CalibJson | ConvertFrom-Json

    if ($DryRun) {
        $RunsRoot = Join-Path $ArtifactRoot 'dry-run-v2'
        $StatusPath = Join-Path $RunsRoot 'status.json'
        $RunInfoPath = Join-Path $RunsRoot 'run-info.json'
        $AggRoot = Join-Path $RunsRoot 'aggregate'
        $dry = New-DryPlan $calib
        Write-Info "DryRun root $RunsRoot shard=tcp-test"
        Invoke-LongRun $dry $exes
    }

    $plan = New-LongPlan $calib $TargetHours
    Write-JsonFile $PlanJson $plan
    Write-PlanMarkdown $plan $PlanMd
    Write-PlanV2Documents $plan $calib
    Write-Info "Wrote $PlanMd predicted=$($plan.predicted_duration)"
    Assert-LongPlanGates $plan

    if ($PlanOnly) { exit 0 }
    if ($Run) { Invoke-LongRun $plan $exes }
} finally {
    if ($sleepOn) {
        [void][UapNativeSleep]::SetThreadExecutionState($ES_CONTINUOUS)
    }
}
