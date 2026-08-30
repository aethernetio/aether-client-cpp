param(
  [int]$SubjectPingMs = 1000,
  [int]$ObserverPingMs = 60000,
  [int]$QueriesPerSec = 3,
  [int]$SettleSec = 20,
  [int]$BlockedSec = 20,
  [int]$UnblockedSec = 25,
  [int]$Cycles = 3,
  [int]$NeedConsecutiveOnline = 5
)

$ErrorActionPreference = "Stop"
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
  [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
  throw "Elevated admin shell required for firewall rules (admin=$isAdmin)."
}

$AetherRoot = "C:\Users\nickc\Projects\aether-client-cpp-peer-uap"
$BaseExe = Join-Path $AetherRoot "build-win64\Debug\aether_presence_probe.exe"
if (-not (Test-Path $BaseExe)) { throw "Missing $BaseExe" }

$ArtifactRoot = Join-Path $AetherRoot "artifacts\presence-runtime-ce69b9cc\firewall"
$run = Join-Path $ArtifactRoot ([DateTimeOffset]::UtcNow.ToUnixTimeSeconds().ToString())
$exch = Join-Path $run "exchange"
$obsState = Join-Path $run "observer"
$subState = Join-Path $run "subject"
$subjectExe = Join-Path $run "aether_presence_subject.exe"
New-Item -ItemType Directory -Force -Path $exch, $obsState, $subState | Out-Null
Copy-Item -Force $BaseExe $subjectExe

$ruleOut = "AetherPresenceSubjectBlock-Out"
$ruleIn = "AetherPresenceSubjectBlock-In"
$eventsPath = Join-Path $run "events.csv"
$summaryPath = Join-Path $run "SUMMARY.txt"
$obsOut = Join-Path $run "observer.out.log"
$obsErr = Join-Path $run "observer.err.log"
$subOut = Join-Path $run "subject.out.log"
$subErr = Join-Path $run "subject.err.log"
"utc_ms,event" | Set-Content -Encoding ascii $eventsPath

function Write-Event([string]$name) {
  $utc = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
  Add-Content -Encoding ascii $eventsPath -Value "$utc,$name"
  Write-Host ("EVENT {0} utc_ms={1}" -f $name, $utc)
  return $utc
}

function Clear-SubjectFirewall {
  Get-NetFirewallRule -DisplayName $ruleOut -ErrorAction SilentlyContinue | Remove-NetFirewallRule
  Get-NetFirewallRule -DisplayName $ruleIn -ErrorAction SilentlyContinue | Remove-NetFirewallRule
}

function Block-SubjectFirewall {
  Clear-SubjectFirewall
  New-NetFirewallRule -DisplayName $ruleOut -Direction Outbound -Action Block -Program $subjectExe -Profile Any | Out-Null
  New-NetFirewallRule -DisplayName $ruleIn -Direction Inbound -Action Block -Program $subjectExe -Profile Any | Out-Null
}

function Get-PresenceLines {
  if (-not (Test-Path $obsOut)) { return @() }
  Get-Content $obsOut | Where-Object { $_ -match 'presence=|TRANSITION|Q#' }
}

function Wait-ConsecutiveOnline([int]$Need, [int]$TimeoutSec) {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
  while ([DateTime]::UtcNow -lt $deadline) {
    $lines = Get-PresenceLines | Where-Object { $_ -match 'presence=Online' -or $_ -match 'presence=Offline' -or $_ -match 'presence=Unknown' -or $_ -match 'TRANSITION' }
    $streak = 0
    $ok = $false
    foreach ($ln in ($lines | Select-Object -Last 40)) {
      if ($ln -match 'presence=Online' -or ($ln -match 'TRANSITION .* -> online')) {
        if ($ln -match 'TRANSITION') {
          if ($ln -match '-> online') { $streak = 1 } else { $streak = 0 }
        } else {
          $streak++
        }
      } elseif ($ln -match 'presence=Offline|presence=Unknown|FAIL') {
        $streak = 0
      }
      if ($streak -ge $Need) { $ok = $true }
    }
    # Also count recent Online Q lines from jsonl if present
    $jsonl = Join-Path $exch "observer_queries.jsonl"
    if (Test-Path $jsonl) {
      $recent = Get-Content $jsonl | Select-Object -Last ($Need + 5)
      $onlineStreak = 0
      foreach ($j in $recent) {
        if ($j -match '"presence":"Online"') { $onlineStreak++ } else { $onlineStreak = 0 }
      }
      if ($onlineStreak -ge $Need) { return $true }
    }
    if ($ok) { return $true }
    Start-Sleep -Milliseconds 200
  }
  return $false
}

function Wait-NonOnline([int64]$AfterUtcMs, [int]$TimeoutSec) {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
  $jsonl = Join-Path $exch "observer_queries.jsonl"
  while ([DateTime]::UtcNow -lt $deadline) {
    if (Test-Path $jsonl) {
      foreach ($line in (Get-Content $jsonl)) {
        if ($line -notmatch '"callback_utc_ms":(\d+)') { continue }
        $utc = [int64]$Matches[1]
        if ($utc -lt $AfterUtcMs) { continue }
        $state = $null
        if ($line -match '"presence":"(Offline|Unknown)"') {
          $state = $Matches[1]
        } elseif ($line -match '"success":false') {
          $state = "Fail"
        }
        if ($null -ne $state) {
          return [pscustomobject]@{ Line = $line; Utc = $utc; State = $state }
        }
      }
    }
    Start-Sleep -Milliseconds 100
  }
  return $null
}

function Wait-OnlineAfter([int64]$AfterUtcMs, [int]$TimeoutSec) {
  $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSec)
  $jsonl = Join-Path $exch "observer_queries.jsonl"
  while ([DateTime]::UtcNow -lt $deadline) {
    if (Test-Path $jsonl) {
      foreach ($line in (Get-Content $jsonl)) {
        if ($line -notmatch '"presence":"Online"') { continue }
        if ($line -notmatch '"callback_utc_ms":(\d+)') { continue }
        $utc = [int64]$Matches[1]
        if ($utc -lt $AfterUtcMs) { continue }
        $early = -1
        if ($line -match '"early_online_delay_ms":(-?\d+)') {
          $early = [int64]$Matches[1]
        }
        $afterLast = -1
        if ($line -match '"presence_online_after_last_server_ms":(-?\d+)') {
          $afterLast = [int64]$Matches[1]
        }
        $first = -1
        if ($line -match '"first_expected_delta_ms":(-?\d+)') {
          $first = [int64]$Matches[1]
        }
        return [pscustomobject]@{
          Utc = $utc
          EarlyDelay = $early
          AfterLastServer = $afterLast
          FirstExpectedDelta = $first
          Line = $line
        }
      }
    }
    Start-Sleep -Milliseconds 100
  }
  return $null
}

$durationSec = $SettleSec + ($Cycles * ($BlockedSec + $UnblockedSec + 30)) + 30
@(
  "method=firewall block subject exe copy",
  "subject ping=window=${SubjectPingMs}ms",
  "observer ping=window=${ObserverPingMs}ms qps~$QueriesPerSec",
  "need_consecutive_online=$NeedConsecutiveOnline",
  "run=$run"
) | Tee-Object -FilePath $summaryPath

Clear-SubjectFirewall
$sub = Start-Process -FilePath $subjectExe -ArgumentList @(
  "--role","subject","--state-dir",$subState,"--exchange-dir",$exch,
  "--ping-ms","$SubjectPingMs","--window-ms","$SubjectPingMs","--duration-sec","$durationSec"
) -WorkingDirectory $AetherRoot -RedirectStandardOutput $subOut -RedirectStandardError $subErr -PassThru -NoNewWindow
Start-Sleep -Seconds 2
$obs = Start-Process -FilePath $BaseExe -ArgumentList @(
  "--role","observer","--state-dir",$obsState,"--exchange-dir",$exch,
  "--ping-ms","$ObserverPingMs","--window-ms","$ObserverPingMs",
  "--queries-per-sec","$QueriesPerSec","--duration-sec","$durationSec",
  "--csv",(Join-Path $exch "observer_queries.csv"),
  "--jsonl",(Join-Path $exch "observer_queries.jsonl")
) -WorkingDirectory $AetherRoot -RedirectStandardOutput $obsOut -RedirectStandardError $obsErr -PassThru -NoNewWindow

Write-Host "Waiting settle + $NeedConsecutiveOnline consecutive Online..."
Start-Sleep -Seconds $SettleSec
if (-not (Wait-ConsecutiveOnline -Need $NeedConsecutiveOnline -TimeoutSec 60)) {
  "WARN: did not see $NeedConsecutiveOnline consecutive Online before first block" | Tee-Object -FilePath $summaryPath -Append
}
$null = Write-Event "settle_done"

try {
  for ($i = 1; $i -le $Cycles; $i++) {
    if ($i -gt 1) {
      if (-not (Wait-ConsecutiveOnline -Need $NeedConsecutiveOnline -TimeoutSec ($UnblockedSec + 40))) {
        "cycle=$i WARN: not stable Online before block" | Tee-Object -FilePath $summaryPath -Append
      }
    }
    Write-Host "`n=== cycle $i/$Cycles BLOCK ==="
    Block-SubjectFirewall
    $blockUtc = Write-Event "block_cycle_$i"

    $off = Wait-NonOnline -AfterUtcMs $blockUtc -TimeoutSec ($BlockedSec + 30)
    if ($off) {
      $ms = $off.Utc - $blockUtc
      "cycle=$i block_to_non_online_ms=$ms state=$($off.State)" | Tee-Object -FilePath $summaryPath -Append
    } else {
      "cycle=$i block_to_non_online_ms=TIMEOUT" | Tee-Object -FilePath $summaryPath -Append
    }

    $remain = $BlockedSec - [int](([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $blockUtc) / 1000)
    if ($remain -gt 0) { Start-Sleep -Seconds $remain }

    Write-Host "=== cycle $i/$Cycles UNBLOCK ==="
    Clear-SubjectFirewall
    $unblockUtc = Write-Event "unblock_cycle_$i"

    $on = Wait-OnlineAfter -AfterUtcMs $unblockUtc -TimeoutSec ($UnblockedSec + 40)
    if ($on) {
      $ms = $on.Utc - $unblockUtc
      "cycle=$i unblock_to_online_ms=$ms presence_online_after_last_server_ms=$($on.AfterLastServer) early_online_delay_ms=$($on.EarlyDelay) first_expected_delta_ms=$($on.FirstExpectedDelta)" |
        Tee-Object -FilePath $summaryPath -Append
    } else {
      "cycle=$i unblock_to_online_ms=TIMEOUT" | Tee-Object -FilePath $summaryPath -Append
    }

    $remainU = $UnblockedSec - [int](([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() - $unblockUtc) / 1000)
    if ($remainU -gt 0) { Start-Sleep -Seconds $remainU }
  }
}
finally {
  Clear-SubjectFirewall
}

$null = $obs.WaitForExit(30000)
$null = $sub.WaitForExit(30000)
if (-not $obs.HasExited) { Stop-Process -Id $obs.Id -Force }
if (-not $sub.HasExited) { Stop-Process -Id $sub.Id -Force }

"" | Tee-Object -FilePath $summaryPath -Append
"artifacts=$run" | Tee-Object -FilePath $summaryPath -Append
Get-Content $summaryPath
