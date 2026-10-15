# Copyright 2026 Aethernet Inc.
# TCP bootstrap -> TCP smoke -> UDP smoke (shared registration state), optional full matrix.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$RunFull = ($args -contains "--full")
$ForceBootstrap = ($args -contains "--force-bootstrap")
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

function Resolve-Exe($BuildDir, $Name) {
  $paths = @(
    (Join-Path $BuildDir "Release\$Name.exe"),
    (Join-Path $BuildDir "examples\benches\stream_matrix\Release\$Name.exe"),
    (Join-Path $BuildDir "$Name.exe")
  )
  foreach ($p in $paths) {
    if (Test-Path $p) { return $p }
  }
  throw "Executable $Name not found under $BuildDir"
}

function Build-Flavor($Flavor, $UserConfigRel, $BuildDir, [switch]$WithBootstrap) {
  Write-Host "=== Configure/build $Flavor ($UserConfigRel) ==="
  cmake -S $Root -B $BuildDir -DCMAKE_BUILD_TYPE=Release `
    "-DUSER_CONFIG:STRING=$UserConfigRel" -DAE_BUILD_TESTS=OFF -DAE_FILTRATION=ON
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  $targets = @(
    "aether_stream_matrix_bench",
    "aether_stream_matrix_logic_test",
    "aether_stream_matrix_schedule_test",
    "aether_stream_matrix_registration_test",
    "aether_stream_matrix_udp_serialization_test",
    "aether_stream_matrix_p2p_safe_loopback_test"
  )
  if ($WithBootstrap) {
    $targets += "aether_stream_matrix_bootstrap"
  }
  cmake --build $BuildDir --config Release --target $targets
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Run-ReleaseTests($BuildDir) {
  & (Resolve-Exe $BuildDir "aether_stream_matrix_schedule_test")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & (Resolve-Exe $BuildDir "aether_stream_matrix_logic_test")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & (Resolve-Exe $BuildDir "aether_stream_matrix_registration_test")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & (Resolve-Exe $BuildDir "aether_stream_matrix_udp_serialization_test")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  & (Resolve-Exe $BuildDir "aether_stream_matrix_p2p_safe_loopback_test")
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Ensure-RegistrationState($TcpBuildDir, $SeedDir) {
  $marker = Join-Path $SeedDir "stream_matrix_bootstrap.ok"
  $stateDir = Join-Path $SeedDir "state"
  if ((-not $ForceBootstrap) -and (Test-Path $marker) -and (Test-Path $stateDir)) {
    Write-Host "Registration seed present: $SeedDir"
    return
  }
  Remove-Item -Recurse -Force $SeedDir -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $SeedDir | Out-Null
  $Bootstrap = Resolve-Exe $TcpBuildDir "aether_stream_matrix_bootstrap"
  Push-Location $SeedDir
  try {
    & $Bootstrap
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $Bootstrap --verify
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  } finally {
    Pop-Location
  }
  if (-not (Test-Path $stateDir)) {
    throw "Bootstrap finished but $stateDir missing"
  }
}

function Copy-RegistrationState($SeedDir, $RunCwd) {
  $dest = Join-Path $RunCwd "state"
  if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
  Copy-Item -Recurse (Join-Path $SeedDir "state") $dest
  $marker = Join-Path $SeedDir "stream_matrix_bootstrap.ok"
  if (Test-Path $marker) {
    Copy-Item $marker (Join-Path $RunCwd "stream_matrix_bootstrap.ok") -Force
  }
}

function Run-Smoke($Flavor, $BuildDir, $SeedDir, $SdkSha, $DirtyTag) {
  $RunCwd = Join-Path $BuildDir "artifacts\run_smoke_$Timestamp"
  New-Item -ItemType Directory -Force -Path $RunCwd | Out-Null
  Copy-RegistrationState $SeedDir $RunCwd
  $Out = Join-Path $RunCwd "bench_out"
  New-Item -ItemType Directory -Force -Path $Out | Out-Null
  $Exe = Resolve-Exe $BuildDir "aether_stream_matrix_bench"
  @(
    "flavor=$Flavor",
    "sdk_sha=$SdkSha$DirtyTag",
    "build_dir=$BuildDir",
    "run_cwd=$RunCwd",
    "exe=$Exe"
  ) | Set-Content -Path (Join-Path $Out "build_meta.txt") -Encoding utf8

  Push-Location $RunCwd
  try {
    & $Exe --smoke --output $Out --sdk-sha "$SdkSha$DirtyTag" --server-version UNKNOWN
    $code = $LASTEXITCODE
  } finally {
    Pop-Location
  }
  Copy-Item (Join-Path $Out "results.csv") (Join-Path $Out "results_smoke.csv") -Force
  return @{ Code = $code; Out = $Out; RunCwd = $RunCwd }
}

Push-Location $Root
try {
  $SdkSha = (git -C $Root rev-parse HEAD).Trim()
  $DirtyTag = if (git -C $Root diff --quiet) { "" } else { "-dirty" }

  $UdpDir = Join-Path $Root "build-stream-matrix-udp"
  $TcpDir = Join-Path $Root "build-stream-matrix-tcp"
  $SeedDir = Join-Path $Root "artifacts\stream_matrix_registration_seed"

  Build-Flavor "tcp" "examples/benches/stream_matrix/config/tcp_only.h" $TcpDir -WithBootstrap
  Build-Flavor "udp" "examples/benches/stream_matrix/config/udp_only.h" $UdpDir

  Run-ReleaseTests $UdpDir
  Run-ReleaseTests $TcpDir

  Ensure-RegistrationState $TcpDir $SeedDir

  $tcpSmoke = Run-Smoke "tcp" $TcpDir $SeedDir $SdkSha $DirtyTag
  if ($tcpSmoke.Code -ne 0) {
    Write-Host "TCP smoke failed: $($tcpSmoke.Out)"
    exit $tcpSmoke.Code
  }

  Start-Sleep -Seconds 6

  $udpSmoke = Run-Smoke "udp" $UdpDir $SeedDir $SdkSha $DirtyTag

  Write-Host "TCP smoke exit: $($tcpSmoke.Code) -> $($tcpSmoke.Out)"
  Write-Host "UDP smoke exit: $($udpSmoke.Code) -> $($udpSmoke.Out)"

  $smokeOk = ($udpSmoke.Code -eq 0) -and ($tcpSmoke.Code -eq 0)
  if (-not $smokeOk) {
    Write-Host "Smoke incomplete (4/4 required for full matrix)."
    exit 2
  }

  if ($RunFull) {
    Start-Sleep -Seconds 6
    foreach ($pair in @(
        @{ Flavor = "tcp"; BuildDir = $TcpDir },
        @{ Flavor = "udp"; BuildDir = $UdpDir }
      )) {
      $RunCwd = Join-Path $pair.BuildDir "artifacts\run_full_$Timestamp"
      New-Item -ItemType Directory -Force -Path $RunCwd | Out-Null
      Copy-RegistrationState $SeedDir $RunCwd
      $Out = Join-Path $RunCwd "bench_out"
      Push-Location $RunCwd
      try {
        $Exe = Resolve-Exe $pair.BuildDir "aether_stream_matrix_bench"
        & $Exe --full --max-rate 128 --output $Out --sdk-sha "$SdkSha$DirtyTag" `
          --server-version UNKNOWN
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
      } finally {
        Pop-Location
      }
      Copy-Item (Join-Path $Out "results.csv") (Join-Path $Out "results_full.csv") -Force
      Write-Host "$($pair.Flavor) full matrix: $Out"
    }
  }

  Write-Host "Built SHA: $SdkSha$DirtyTag"
  Write-Host "Registration seed: $SeedDir"
  exit 0
} finally {
  Pop-Location
}
