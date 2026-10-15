# Copyright 2026 Aethernet Inc.
# Dual UDP/TCP SDK builds, logic tests, smoke (4 modes), optional full matrix.

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$RunFull = ($args -contains "--full")
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

function Build-Flavor($Flavor, $UserConfigRel, $BuildDir) {
  Write-Host "=== Configure/build $Flavor ($UserConfigRel) ==="
  cmake -S $Root -B $BuildDir -DCMAKE_BUILD_TYPE=Release `
    "-DUSER_CONFIG:STRING=$UserConfigRel" -DAE_BUILD_TESTS=OFF
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

  cmake --build $BuildDir --config Release --target aether_stream_matrix_bench `
    --target aether_stream_matrix_logic_test `
    --target aether_stream_matrix_schedule_test
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Run-LogicTest($BuildDir) {
  $ScheduleTest = Resolve-Exe $BuildDir "aether_stream_matrix_schedule_test"
  & $ScheduleTest
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  $LogicTest = Resolve-Exe $BuildDir "aether_stream_matrix_logic_test"
  & $LogicTest
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Run-Smoke($Flavor, $BuildDir, $SdkSha, $DirtyTag) {
  $Out = Join-Path $BuildDir "artifacts\smoke_$Timestamp"
  New-Item -ItemType Directory -Force -Path $Out | Out-Null
  $Exe = Resolve-Exe $BuildDir "aether_stream_matrix_bench"
  $meta = Join-Path $Out "build_meta.txt"
  @(
    "flavor=$Flavor"
    "sdk_sha=$SdkSha$DirtyTag"
    "user_config=$Flavor"
    "build_dir=$BuildDir"
    "exe=$Exe"
  ) | Set-Content -Path $meta -Encoding utf8

  & $Exe --smoke --output $Out --sdk-sha "$SdkSha$DirtyTag" --server-version UNKNOWN
  $code = $LASTEXITCODE
  Copy-Item (Join-Path $Out "results.csv") (Join-Path $Out "results_smoke.csv") -Force
  return @{ Code = $code; Out = $Out; Exe = $Exe }
}

Push-Location $Root
try {
  $SdkSha = (git -C $Root rev-parse HEAD).Trim()
  $DirtyTag = if (git -C $Root diff --quiet) { "" } else { "-dirty" }

  $UdpDir = Join-Path $Root "build-stream-matrix-udp"
  $TcpDir = Join-Path $Root "build-stream-matrix-tcp"

  Build-Flavor "udp" "examples/benches/stream_matrix/config/udp_only.h" $UdpDir
  Build-Flavor "tcp" "examples/benches/stream_matrix/config/tcp_only.h" $TcpDir

  Run-LogicTest $UdpDir
  Run-LogicTest $TcpDir

  Remove-Item -Recurse -Force (Join-Path $Root "state") -ErrorAction SilentlyContinue

  $udpSmoke = Run-Smoke "udp" $UdpDir $SdkSha $DirtyTag
  Start-Sleep -Seconds 6
  $tcpSmoke = Run-Smoke "tcp" $TcpDir $SdkSha $DirtyTag

  Write-Host "UDP smoke exit: $($udpSmoke.Code) -> $($udpSmoke.Out)"
  Write-Host "TCP smoke exit: $($tcpSmoke.Code) -> $($tcpSmoke.Out)"

  $smokeOk = ($udpSmoke.Code -eq 0) -and ($tcpSmoke.Code -eq 0)
  if (-not $smokeOk) {
    Write-Host "Smoke incomplete or failed (partial exit 2 expected per build)."
    if ($udpSmoke.Code -ne 0 -and $tcpSmoke.Code -ne 0) { exit 2 }
    exit 2
  }

  if ($RunFull) {
    Start-Sleep -Seconds 6
    foreach ($pair in @(
        @{ Flavor = "udp"; BuildDir = $UdpDir },
        @{ Flavor = "tcp"; BuildDir = $TcpDir }
      )) {
      $Out = Join-Path $pair.BuildDir "artifacts\full_$Timestamp"
      New-Item -ItemType Directory -Force -Path $Out | Out-Null
      $Exe = Resolve-Exe $pair.BuildDir "aether_stream_matrix_bench"
      & $Exe --full --max-rate 128 --output $Out --sdk-sha "$SdkSha$DirtyTag" `
        --server-version UNKNOWN
      if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
      Copy-Item (Join-Path $Out "results.csv") (Join-Path $Out "results_full.csv") -Force
      Write-Host "$($pair.Flavor) full matrix: $Out"
    }
  }

  Write-Host "Built SHA: $SdkSha$DirtyTag"
  exit 0
} finally {
  Pop-Location
}
