# Copyright 2026 Aethernet Inc.
# Runs stream matrix benchmark (smoke then full matrix) and writes summary.md

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build-stream-matrix"
$OutDir = Join-Path $BuildDir "stream_matrix_out"
$Exe = Join-Path $BuildDir "Release\aether_stream_matrix_bench.exe"
if (-not (Test-Path $Exe)) {
  $Exe = Join-Path $BuildDir "aether_stream_matrix_bench.exe"
}

Push-Location $Root
try {
  if (-not (Test-Path $BuildDir)) {
    cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release -DAE_BUILD_TESTS=OFF
    cmake --build $BuildDir --config Release --target aether_stream_matrix_bench
  }

  $SdkSha = (git -C $Root rev-parse HEAD).Trim()
  Remove-Item -Recurse -Force (Join-Path $BuildDir "state") -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

  & $Exe --smoke --output $OutDir --sdk-sha $SdkSha --server-version UNKNOWN
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  Start-Sleep -Seconds 6

  & $Exe --full --max-rate 128 --output $OutDir --sdk-sha $SdkSha --server-version UNKNOWN
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $Csv = Join-Path $OutDir "results.csv"
  $Summary = Join-Path $OutDir "summary.md"
  @(
    "# Stream matrix summary"
    ""
    "SDK: ``$SdkSha``"
    ""
    "Server deployed config: UNKNOWN (see README; source defaults 65536 / 1000 / 60 s)."
    ""
    "| transport | stream | payload | period_ms | outcome | goodput | p50_ms |"
    "|---|---|---:|---:|---|---:|---:|"
  ) | Set-Content -Encoding utf8 $Summary

  Import-Csv $Csv | ForEach-Object {
    $p50 = $_.latency_p50_ms
    if ($p50 -eq "N/A") { $p50 = "-" }
    "| $($_.transport) | $($_.stream) | $($_.payload_bytes) | $($_.period_ms) | $($_.outcome) | $($_.goodput_bytes_s) | $p50 |"
  } | Add-Content -Encoding utf8 $Summary

  Write-Host "Results: $Csv"
  Write-Host "Summary: $Summary"
} finally {
  Pop-Location
}
