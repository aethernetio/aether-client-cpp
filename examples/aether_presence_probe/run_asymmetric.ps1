param(
  [int]$DurationSec = 60,
  [int]$QueriesPerSec = 3,
  [int]$ObserverPingMs = 60000,
  [string]$SubjectPeriodsSec = "1,2,3,5,10",
  [switch]$ServerDiag,
  [string]$ArtifactRoot = ""
)

$ErrorActionPreference = "Stop"
$AetherRoot = "C:\Users\nickc\Projects\aether-client-cpp-peer-uap"
$Exe = Join-Path $AetherRoot "build-win64\Debug\aether_presence_probe.exe"
if (-not (Test-Path $Exe)) {
  throw "Missing exe: $Exe"
}

if (-not $ArtifactRoot) {
  $ArtifactRoot = Join-Path $AetherRoot "artifacts\presence-runtime-ce69b9cc"
}
$Batch = Join-Path $ArtifactRoot ("normal_asymmetric\" + [DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
New-Item -ItemType Directory -Force -Path $Batch | Out-Null
$Summary = Join-Path $Batch "SUMMARY.txt"

$SubjectSec = @()
foreach ($part in ($SubjectPeriodsSec -split ",")) {
  $t = $part.Trim()
  if ($t) { $SubjectSec += [int]$t }
}

@(
  "head=ce69b9cc (+ local first_expected instrumentation if present)",
  "observer ping=window=${ObserverPingMs}ms",
  "subject periods_s=$($SubjectSec -join ',')",
  "duration_sec=$DurationSec queries_per_sec~$QueriesPerSec",
  "exe=$Exe"
) | Tee-Object -FilePath $Summary

foreach ($sec in $SubjectSec) {
  $SubjectMs = $sec * 1000
  $run = Join-Path $Batch ("subject_${SubjectMs}ms")
  $exch = Join-Path $run "exchange"
  $obsState = Join-Path $run "observer"
  $subState = Join-Path $run "subject"
  New-Item -ItemType Directory -Force -Path $exch, $obsState, $subState | Out-Null

  $obsOut = Join-Path $run "observer.out.log"
  $obsErr = Join-Path $run "observer.err.log"
  $subOut = Join-Path $run "subject.out.log"
  $subErr = Join-Path $run "subject.err.log"

  Write-Host "`n===== subject=${SubjectMs}ms observer=${ObserverPingMs}ms ====="
  "" | Tee-Object -FilePath $Summary -Append
  "===== subject=${SubjectMs}ms =====" | Tee-Object -FilePath $Summary -Append

  $subArgs = @(
    "--role","subject","--state-dir",$subState,"--exchange-dir",$exch,
    "--ping-ms","$SubjectMs","--window-ms","$SubjectMs",
    "--duration-sec","$DurationSec"
  )
  $obsArgs = @(
    "--role","observer","--state-dir",$obsState,"--exchange-dir",$exch,
    "--ping-ms","$ObserverPingMs","--window-ms","$ObserverPingMs",
    "--queries-per-sec","$QueriesPerSec","--duration-sec","$DurationSec",
    "--csv",(Join-Path $exch "observer_queries.csv"),
    "--jsonl",(Join-Path $exch "observer_queries.jsonl")
  )
  if ($ServerDiag -and $sec -eq $SubjectSec[0]) {
    $obsArgs += "--server-diag"
  }

  $sub = Start-Process -FilePath $Exe -ArgumentList $subArgs -WorkingDirectory $AetherRoot `
    -RedirectStandardOutput $subOut -RedirectStandardError $subErr -PassThru -NoNewWindow
  Start-Sleep -Seconds 2
  $obs = Start-Process -FilePath $Exe -ArgumentList $obsArgs -WorkingDirectory $AetherRoot `
    -RedirectStandardOutput $obsOut -RedirectStandardError $obsErr -PassThru -NoNewWindow

  $timeoutMs = ($DurationSec + 180) * 1000
  $null = $obs.WaitForExit($timeoutMs)
  $null = $sub.WaitForExit($timeoutMs)
  if (-not $obs.HasExited) { Stop-Process -Id $obs.Id -Force }
  if (-not $sub.HasExited) { Stop-Process -Id $sub.Id -Force }

  Get-Content $obsOut, $obsErr -ErrorAction SilentlyContinue |
    Select-String -Pattern "SUMMARY|queries=|Online=|latency_ms|early_online|transitions|SERVER_SCOPED|SERVER_DIAG|FAIL" |
    ForEach-Object { $_.Line } |
    Tee-Object -FilePath $Summary -Append
}

Write-Host "`nartifacts: $Batch"
Get-Content $Summary
