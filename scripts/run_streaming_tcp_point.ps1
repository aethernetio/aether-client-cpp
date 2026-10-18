param(
  [Parameter(Mandatory)] [string]$BenchExe,
  [Parameter(Mandatory)] [string]$SeedDir,
  [Parameter(Mandatory)] [string]$ArtifactRoot,
  [Parameter(Mandatory)] [int]$Chunk,
  [Parameter(Mandatory)] [int]$Outstanding,
  [Parameter(Mandatory)] [ValidateSet("burst","backpressure")] [string]$Mode
)

$runDir = Join-Path $ArtifactRoot ("pt_{0}_{1}_{2}" -f $Chunk, $Outstanding, $Mode)
New-Item -ItemType Directory -Force -Path (Join-Path $runDir "bench_out") | Out-Null
Copy-Item -Recurse (Join-Path $SeedDir "state") $runDir -Force
Copy-Item (Join-Path $SeedDir "stream_matrix_bootstrap.ok") $runDir -Force
Push-Location $runDir
$args = @(
  "--streaming-repro-chunk", $Chunk,
  "--streaming-repro-outstanding", $Outstanding,
  "--streaming-repro-mode", $Mode,
  "--streaming-repro-prefix", "0",
  "--output", "bench_out"
)
$p = Start-Process -FilePath $BenchExe -ArgumentList $args -Wait -PassThru -NoNewWindow `
  -RedirectStandardOutput "bench_out\console.log" -RedirectStandardError "bench_out\console.err"
Pop-Location
exit $p.ExitCode
