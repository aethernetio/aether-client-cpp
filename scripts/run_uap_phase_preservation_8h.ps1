# Independent 8-hour UAP phase-preservation characterization runner.
# Enforces wall-clock budget. Alternates TCP/UDP shards. Supports resume.
# Does not commit, push, clean, or reconfigure CMake.

param(
  [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
  [string]$ArtifactRoot = "",
  [int]$ActiveBudgetSec = 28500,   # 7h55m
  [int]$ShardBudgetSec = 1500,     # 25m target
  [int]$ReportReserveSec = 300,    # 5m
  [uint32]$BaseSeed = 20260826,
  [switch]$Resume
)

$ErrorActionPreference = "Stop"
if (-not $ArtifactRoot) {
  $ArtifactRoot = Join-Path $RepoRoot "artifacts\uap-phase-preservation\8h"
}

$TcpExe = Join-Path $RepoRoot "build-win64-uap-ping-retry-tcp\Release\aether_uap_1s_timing_characterization.exe"
$UdpExe = Join-Path $RepoRoot "build-win64-uap-ping-retry-udp\Release\aether_uap_1s_timing_characterization.exe"
$StatusPath = Join-Path $ArtifactRoot "status.json"
$PlanPath = Join-Path $ArtifactRoot "plan.md"
$LogPath = Join-Path $ArtifactRoot "runner.log"
$RunsDir = Join-Path $ArtifactRoot "runs"
$AggDir = Join-Path $ArtifactRoot "aggregate"
$FailDir = Join-Path $ArtifactRoot "failure-cases"
$AggScript = Join-Path $RepoRoot "scripts\aggregate_uap_phase_preservation_8h.py"

New-Item -ItemType Directory -Force -Path $RunsDir, $AggDir, $FailDir | Out-Null

function Write-AtomicJson($Path, $Object) {
  $tmp = "$Path.tmp"
  $json = $Object | ConvertTo-Json -Depth 12
  [System.IO.File]::WriteAllText($tmp, $json)
  Move-Item -Force $tmp $Path
}

function Write-Log([string]$Msg) {
  $line = "[{0}] {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $Msg
  Add-Content -Path $LogPath -Value $line
  Write-Host $line
}

function Stop-OrphanCharacterization {
  Get-CimInstance Win32_Process -Filter "Name='aether_uap_1s_timing_characterization.exe'" -ErrorAction SilentlyContinue |
    ForEach-Object {
      Write-Log "Killing leftover characterization pid=$($_.ProcessId)"
      Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
}

# Prevent system sleep while active; do not prevent display sleep.
Add-Type -Namespace Native -Name Power -MemberDefinition @"
[DllImport("kernel32.dll", CharSet=CharSet.Auto, SetLastError=true)]
public static extern uint SetThreadExecutionState(uint esFlags);
"@
$ES_CONTINUOUS = [Convert]::ToUInt32("80000000", 16)
$ES_SYSTEM_REQUIRED = [Convert]::ToUInt32("1", 16)
[void][Native.Power]::SetThreadExecutionState($ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED)

$started = Get-Date
$status = $null
if ($Resume -and (Test-Path $StatusPath)) {
  $status = Get-Content $StatusPath -Raw | ConvertFrom-Json
  if ($status.started_utc) {
    $started = [datetime]::Parse($status.started_utc, $null, [System.Globalization.DateTimeStyles]::RoundtripKind)
  }
  Write-Log "Resuming from status.json started=$($status.started_utc) next_shard=$($status.next_shard_index)"
} else {
  $status = [ordered]@{
    started_utc = $started.ToUniversalTime().ToString("o")
    active_budget_sec = $ActiveBudgetSec
    shard_budget_sec = $ShardBudgetSec
    report_reserve_sec = $ReportReserveSec
    base_seed = $BaseSeed
    next_shard_index = 1
    shards = @()
    state = "running"
    last_update_utc = (Get-Date).ToUniversalTime().ToString("o")
  }
  Write-AtomicJson $StatusPath $status
}

@"
# UAP 8h phase-preservation plan

- started_utc: $($status.started_utc)
- active_test_budget_sec: $ActiveBudgetSec (7h55m)
- report_reserve_sec: $ReportReserveSec (5m)
- shard_budget_sec: $ShardBudgetSec (target 20-30m)
- alternate: tcp-01, udp-01, tcp-02, udp-02, ...
- tcp_exe: $TcpExe
- udp_exe: $UdpExe
- continue on semantic failures; preserve and proceed
- restart next shard after process crash
- runner enforces wall-clock; does not estimate from case counts
"@ | Set-Content -Path $PlanPath -Encoding UTF8

function ElapsedSec {
  return [int]((Get-Date) - $started).TotalSeconds
}

function RemainingActiveSec {
  return $ActiveBudgetSec - (ElapsedSec)
}

function Persist-Status([string]$State = "running") {
  $status.state = $State
  $status.last_update_utc = (Get-Date).ToUniversalTime().ToString("o")
  $status.elapsed_sec = (ElapsedSec)
  $status.remaining_active_sec = (RemainingActiveSec)
  Write-AtomicJson $StatusPath $status
}

# Heartbeat every ~60s while a shard runs.
$heartbeatJob = $null

try {
  Write-Log "8h runner start. active_budget=${ActiveBudgetSec}s shard=${ShardBudgetSec}s"
  if (-not (Test-Path $TcpExe)) { throw "Missing TCP exe: $TcpExe" }
  if (-not (Test-Path $UdpExe)) { throw "Missing UDP exe: $UdpExe" }

  $shardIndex = [int]$status.next_shard_index
  if ($shardIndex -lt 1) { $shardIndex = 1 }

  while ((RemainingActiveSec) -gt 90) {
    $transport = if (($shardIndex % 2) -eq 1) { "tcp" } else { "udp" }
    $pair = [int][Math]::Ceiling($shardIndex / 2.0)
    $shardName = $transport + "-" + ("{0:00}" -f $pair)
    $exe = if ($transport -eq "tcp") { $TcpExe } else { $UdpExe }
    $seed = [uint32]($BaseSeed + $shardIndex)
    $shardDir = Join-Path $RunsDir $shardName
    New-Item -ItemType Directory -Force -Path $shardDir | Out-Null

    $remain = RemainingActiveSec
    $thisBudget = [Math]::Min($ShardBudgetSec, [Math]::Max(120, $remain - 30))
    if ($thisBudget -lt 120) {
      Write-Log "Budget too small for another shard ($remain s). Stopping scheduling."
      break
    }

    Write-Log "Starting shard $shardName transport=$transport budget=${thisBudget}s seed=$seed remain=${remain}s"
    Stop-OrphanCharacterization

    $stdout = Join-Path $shardDir "stdout.log"
    $stderr = Join-Path $shardDir "stderr.log"
    $meta = [ordered]@{
      shard = $shardName
      transport = $transport
      seed = $seed
      budget_sec = $thisBudget
      started_utc = (Get-Date).ToUniversalTime().ToString("o")
      exe = $exe
      state = "running"
    }
    Write-AtomicJson (Join-Path $shardDir "shard-status.json") $meta

    $argList = @(
      "--phase-preservation",
      "--phase-preservation-budget-sec", "$thisBudget",
      "--no-long-characterization",
      "--transport", $transport,
      "--seed", "$seed",
      "--artifact-dir", $shardDir,
      "--run-id", $shardName,
      "--exe", $exe
    )

    $proc = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $RepoRoot `
      -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
      -PassThru -WindowStyle Hidden
    if ($null -eq $proc) {
      throw "Failed to start characterization exe for $shardName"
    }
    Write-Log "Shard $shardName pid=$($proc.Id)"

    $hardDeadline = (Get-Date).AddSeconds($thisBudget + 180)  # allow finish + controls
    $lastBeat = Get-Date
    while (-not $proc.HasExited) {
      Start-Sleep -Seconds 5
      try { $proc.Refresh() } catch {}
      if (((Get-Date) - $lastBeat).TotalSeconds -ge 60) {
        Persist-Status "running"
        $lastBeat = Get-Date
        Write-Log "heartbeat shard=$shardName elapsed_total=$(ElapsedSec)s pid=$($proc.Id)"
      }
      if ((Get-Date) -gt $hardDeadline) {
        Write-Log "Shard $shardName exceeded hard deadline; terminating process tree"
        try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
        Stop-OrphanCharacterization
        break
      }
    }

    $exitCode = -1
    try {
      if ($proc.HasExited) { $exitCode = $proc.ExitCode }
    } catch {
      $exitCode = -1
    }
    $crashed = ($exitCode -lt 0) -or ($exitCode -ge 200)
    # Semantic fail exit 7 is expected and recorded; continue.

    $meta.finished_utc = (Get-Date).ToUniversalTime().ToString("o")
    $meta.exit_code = $exitCode
    $meta.crashed = [bool]$crashed
    $meta.state = if ($crashed) { "crashed" } elseif ($exitCode -eq 0) { "pass" } else { "failed_semantic" }
    Write-AtomicJson (Join-Path $shardDir "shard-status.json") $meta

    $status.shards = @($status.shards) + @([pscustomobject]$meta)
    $shardIndex++
    $status.next_shard_index = $shardIndex
    Persist-Status "running"
    Write-Log "Finished shard $shardName exit=$exitCode state=$($meta.state)"

    if ($crashed) {
      Write-Log "Preserved crash outputs under $shardDir; continuing next shard"
      Start-Sleep -Seconds 3
    }
  }

  Write-Log "Active budget exhausted or remaining too small. Aggregating..."
  Persist-Status "aggregating"
  Stop-OrphanCharacterization

  if (Test-Path $AggScript) {
    python $AggScript --root $ArtifactRoot 2>&1 | Tee-Object -FilePath (Join-Path $ArtifactRoot "aggregate.log")
  } else {
    Write-Log "WARN missing aggregator $AggScript"
  }

  Persist-Status "completed"
  $elapsed = ElapsedSec
  Write-Log "8h runner completed. wall_sec=$elapsed"
  Write-Host ""
  Write-Host "==== FINAL CONSOLE SUMMARY ===="
  if (Test-Path (Join-Path $AggDir "console-summary.txt")) {
    Get-Content (Join-Path $AggDir "console-summary.txt")
  } else {
    Write-Host "Aggregate summary not found; see $ArtifactRoot"
  }
}
catch {
  Write-Log "FATAL: $_"
  Persist-Status "error"
  throw
}
finally {
  [void][Native.Power]::SetThreadExecutionState($ES_CONTINUOUS)
  Stop-OrphanCharacterization
}
