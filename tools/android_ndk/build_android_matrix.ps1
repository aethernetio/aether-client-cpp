# Copyright 2026 Aethernet Inc.
#
# Build the required Android NDK matrix.

[CmdletBinding()]
param(
  [string]$CMakeVersion = "",

  [string]$NdkVersion = "",

  [switch]$Clean
)

$ErrorActionPreference = "Stop"

$script_dir = Split-Path -Parent $PSCommandPath
$build_script = Join-Path $script_dir "build_android_ndk.ps1"

$matrix = @(
  @{ Abi = "x86_64"; Config = "Debug"; UserConfig = "config/user_config_hydrogen.h"; Smoke = $true },
  @{ Abi = "x86_64"; Config = "Debug"; UserConfig = "config/user_config_sodium.h"; Smoke = $true },
  @{ Abi = "arm64-v8a"; Config = "Debug"; UserConfig = "config/user_config_hydrogen.h"; Smoke = $true },
  @{ Abi = "arm64-v8a"; Config = "Debug"; UserConfig = "config/user_config_sodium.h"; Smoke = $true },
  @{ Abi = "x86_64"; Config = "Release"; UserConfig = "config/user_config_android_smoke.h"; Smoke = $true },
  @{ Abi = "arm64-v8a"; Config = "Release"; UserConfig = "config/user_config_android_smoke.h"; Smoke = $true }
)

foreach ($entry in $matrix) {
  Write-Host "============================================================"
  Write-Host ("Matrix: {0} {1} {2}" -f $entry.Abi, $entry.Config, $entry.UserConfig)
  Write-Host "============================================================"

  $invoke_args = @{
    Abi = $entry.Abi
    Config = $entry.Config
    UserConfig = $entry.UserConfig
  }
  if ($entry.Smoke) {
    $invoke_args.BuildSmoke = $true
  }
  if ($Clean) {
    $invoke_args.Clean = $true
  }
  if ($CMakeVersion) {
    $invoke_args.CMakeVersion = $CMakeVersion
  }
  if ($NdkVersion) {
    $invoke_args.NdkVersion = $NdkVersion
  }

  & $build_script @invoke_args
  if ($LASTEXITCODE -ne 0) {
    throw "Matrix entry failed: $($entry.Abi) $($entry.Config) $($entry.UserConfig)"
  }
}

Write-Host "Android NDK matrix completed successfully."
exit 0
