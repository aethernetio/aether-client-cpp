# Copyright 2026 Aethernet Inc.
# Configure, build, run smoke then optional full matrix.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $Root "build-stream-matrix"
$SmokeOut = Join-Path $BuildDir "stream_matrix_out_smoke"
$FullOut = Join-Path $BuildDir "stream_matrix_out_full"
$RunFull = ($args -contains "--full")

Push-Location $Root
try {
  cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=Release -DAE_BUILD_TESTS=OFF
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  cmake --build $BuildDir --config Release --target aether_stream_matrix_bench `
    --target aether_stream_matrix_logic_test
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $LogicTest = Join-Path $BuildDir "Release\aether_stream_matrix_logic_test.exe"
  if (-not (Test-Path $LogicTest)) {
    $LogicTest = Join-Path $BuildDir "aether_stream_matrix_logic_test.exe"
  }
  & $LogicTest
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $Exe = Join-Path $BuildDir "Release\aether_stream_matrix_bench.exe"
  if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $BuildDir "aether_stream_matrix_bench.exe"
  }
  if (-not (Test-Path $Exe)) {
    Write-Error "Benchmark executable not found after build"
  }

  $SdkSha = (git -C $Root rev-parse HEAD).Trim()
  $Dirty = (git -C $Root diff --quiet; if ($LASTEXITCODE -ne 0) { "dirty" } else { "clean" })

  Remove-Item -Recurse -Force (Join-Path $BuildDir "state") -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $SmokeOut | Out-Null

  & $Exe --smoke --output $SmokeOut --sdk-sha $SdkSha --server-version UNKNOWN
  $SmokeCode = $LASTEXITCODE
  Copy-Item (Join-Path $SmokeOut "results.csv") (Join-Path $SmokeOut "results_smoke.csv") -Force

  if ($SmokeCode -ne 0) {
    Write-Host "Smoke failed with exit code $SmokeCode; skipping full matrix."
    exit $SmokeCode
  }

  if ($RunFull) {
    Start-Sleep -Seconds 6
    New-Item -ItemType Directory -Force -Path $FullOut | Out-Null
    & $Exe --full --max-rate 128 --output $FullOut --sdk-sha $SdkSha --server-version UNKNOWN
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Copy-Item (Join-Path $FullOut "results.csv") (Join-Path $FullOut "results_full.csv") -Force
  }

  Write-Host "Built SHA: $SdkSha ($Dirty)"
  Write-Host "Executable: $Exe"
  Write-Host "Smoke results: $SmokeOut"
  if ($RunFull) { Write-Host "Full results: $FullOut" }
} finally {
  Pop-Location
}
