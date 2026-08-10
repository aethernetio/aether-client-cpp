# Copyright 2026 Aethernet Inc.
#
# Build (if needed) and run the x86_64 Android NDK smoke on an emulator.

[CmdletBinding()]
param(
  [string]$AvdName = "",
  [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
  $script_dir = Split-Path -Parent $PSCommandPath
  return (Resolve-Path (Join-Path $script_dir "..\..")).Path
}

function Resolve-AndroidSdk {
  if ($env:ANDROID_SDK_ROOT -and (Test-Path $env:ANDROID_SDK_ROOT)) {
    return (Resolve-Path $env:ANDROID_SDK_ROOT).Path
  }
  if ($env:ANDROID_HOME -and (Test-Path $env:ANDROID_HOME)) {
    return (Resolve-Path $env:ANDROID_HOME).Path
  }
  $default = Join-Path $env:LOCALAPPDATA "Android\Sdk"
  if (Test-Path $default) {
    return (Resolve-Path $default).Path
  }
  throw "Android SDK not found."
}

function Get-Tool([string]$Sdk, [string]$Relative) {
  $path = Join-Path $Sdk $Relative
  if (-not (Test-Path $path)) {
    throw "Required tool not found: $path"
  }
  return $path
}

function Get-AdbDevices([string]$Adb) {
  $lines = & $Adb devices | Select-Object -Skip 1
  $devices = @()
  foreach ($line in $lines) {
    if ($line -match "^\s*$") { continue }
    if ($line -match "^(\S+)\s+device\s*$") {
      $devices += $Matches[1]
    }
  }
  return $devices
}

function Wait-BootCompleted([string]$Adb, [string]$Serial, [int]$TimeoutSec = 300) {
  & $Adb -s $Serial wait-for-device
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $boot = (& $Adb -s $Serial shell getprop sys.boot_completed).Trim()
    if ($boot -eq "1") {
      return
    }
    Start-Sleep -Seconds 2
  }
  throw "Timed out waiting for sys.boot_completed on $Serial"
}

function Ensure-X86_64Avd([string]$Emulator, [string]$AvdManager, [string]$SdkManager, [string]$PreferredName) {
  $avds = & $Emulator -list-avds
  if ($PreferredName -and ($avds -contains $PreferredName)) {
    return $PreferredName
  }
  foreach ($avd in $avds) {
    if ($avd -match "(?i)x86_64|Pixel.*API") {
      return $avd
    }
  }
  if ($avds.Count -gt 0 -and -not $PreferredName) {
    # Prefer any existing AVD; ABI is verified after boot.
    return $avds[0]
  }

  $name = if ($PreferredName) { $PreferredName } else { "Aether_NDK_Smoke_x86_64" }
  Write-Host "No suitable AVD found; creating $name if x86_64 system image is installed."

  $packages = & $SdkManager --list_installed 2>&1 | Out-String
  if ($packages -notmatch "system-images;android-\d+;google_apis;x86_64|system-images;android-\d+;default;x86_64") {
    throw "No x86_64 system image installed; cannot create AVD $name"
  }

  $image_line = ($packages -split "`n" | Where-Object {
    $_ -match "system-images;android-(\d+);(google_apis|default);x86_64"
  } | Select-Object -Last 1)
  if (-not $image_line) {
    throw "Failed to parse installed x86_64 system image package."
  }
  if ($image_line -notmatch "(system-images;android-\d+;(google_apis|default);x86_64)") {
    throw "Failed to extract x86_64 system image package name."
  }
  $package = $Matches[1]
  Write-Host "Creating AVD $name from $package"
  $null | & $AvdManager create avd -n $name -k $package --device "pixel_6" --force
  if ($LASTEXITCODE -ne 0) {
    throw "avdmanager create failed with exit code $LASTEXITCODE"
  }
  return $name
}

function Invoke-SmokeOnce([string]$Adb, [string]$Serial, [string]$RemoteDir) {
  $exit_echo = 'echo __EXIT_CODE__:$?'
  $cmd = "cd $RemoteDir && chmod 755 aether_android_smoke_runner && LD_LIBRARY_PATH=$RemoteDir ./aether_android_smoke_runner $RemoteDir/libaether_android_smoke.so; $exit_echo"
  $output = & $Adb -s $Serial shell $cmd 2>&1 | Out-String
  Write-Host '----- smoke stdout/stderr -----'
  Write-Host $output
  Write-Host '----- end smoke output -----'

  if ($output -notmatch "AETHER_ANDROID_SMOKE_START") {
    throw "Missing marker AETHER_ANDROID_SMOKE_START"
  }
  if ($output -notmatch "AETHER_ANDROID_APP_CONSTRUCTED") {
    throw "Missing marker AETHER_ANDROID_APP_CONSTRUCTED"
  }
  if ($output -notmatch "AETHER_ANDROID_UPDATE_OK") {
    throw "Missing marker AETHER_ANDROID_UPDATE_OK"
  }
  if ($output -notmatch "AETHER_ANDROID_SMOKE_OK") {
    throw "Missing marker AETHER_ANDROID_SMOKE_OK"
  }
  if ($output -notmatch "__EXIT_CODE__:0") {
    throw "Smoke runner exit code was not 0"
  }
  return $output
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$adb = Get-Tool $sdk "platform-tools\adb.exe"
$emulator = Get-Tool $sdk "emulator\emulator.exe"
$avdmanager = Join-Path $sdk "cmdline-tools\latest\bin\avdmanager.bat"
$sdkmanager = Join-Path $sdk "cmdline-tools\latest\bin\sdkmanager.bat"
if (-not (Test-Path $avdmanager)) {
  $avdmanager = Get-ChildItem (Join-Path $sdk "cmdline-tools") -Recurse -Filter avdmanager.bat |
    Select-Object -First 1 -ExpandProperty FullName
}
if (-not (Test-Path $sdkmanager)) {
  $sdkmanager = Get-ChildItem (Join-Path $sdk "cmdline-tools") -Recurse -Filter sdkmanager.bat |
    Select-Object -First 1 -ExpandProperty FullName
}

Write-Host "adb version:"
& $adb version

Write-Host "adb devices:"
& $adb devices

$serial = $null
$devices = Get-AdbDevices $adb
foreach ($d in $devices) {
  if ($d -match "^emulator-") {
    $abi = (& $adb -s $d shell getprop ro.product.cpu.abi).Trim()
    Write-Host "Found emulator $d abi=$abi"
    if ($abi -eq "x86_64") {
      $serial = $d
      break
    }
  }
}

if (-not $serial) {
  $avd = Ensure-X86_64Avd $emulator $avdmanager $sdkmanager $AvdName
  Write-Host "Starting AVD $avd (no wipe-data)"
  Start-Process -FilePath $emulator -ArgumentList @("-avd", $avd) -WindowStyle Minimized | Out-Null

  $deadline = (Get-Date).AddSeconds(120)
  while ((Get-Date) -lt $deadline -and -not $serial) {
    Start-Sleep -Seconds 2
    foreach ($d in (Get-AdbDevices $adb)) {
      if ($d -match "^emulator-") {
        $serial = $d
        break
      }
    }
  }
  if (-not $serial) {
    throw "Failed to see emulator device via adb"
  }
}

Wait-BootCompleted $adb $serial
$abi = (& $adb -s $serial shell getprop ro.product.cpu.abi).Trim()
$api = (& $adb -s $serial shell getprop ro.build.version.sdk).Trim()
Write-Host "Using device $serial abi=$abi api=$api"
if ($abi -ne "x86_64") {
  throw "Emulator ABI must be x86_64 for this smoke; got $abi"
}

$build_dir = Join-Path $repo_root "build-android-x86_64-Release-user_config_android_smoke"
$so = Join-Path $build_dir "android_ndk_smoke\libaether_android_smoke.so"
$runner = Join-Path $build_dir "android_ndk_smoke\aether_android_smoke_runner"

if (-not $SkipBuild -or -not (Test-Path $so) -or -not (Test-Path $runner)) {
  $build_script = Join-Path $repo_root "tools\android_ndk\build_android_ndk.ps1"
  & $build_script -Abi x86_64 -Config Release -UserConfig "config/user_config_android_smoke.h" -BuildSmoke
  if ($LASTEXITCODE -ne 0) {
    throw "Smoke build failed"
  }
}

if (-not (Test-Path $so)) {
  # Fallback to common ninja output locations.
  $so = Get-ChildItem $build_dir -Recurse -Filter libaether_android_smoke.so | Select-Object -First 1 -ExpandProperty FullName
  $runner = Get-ChildItem $build_dir -Recurse -Filter aether_android_smoke_runner | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $so -or -not $runner -or -not (Test-Path $so) -or -not (Test-Path $runner)) {
  throw "Smoke artifacts not found under $build_dir"
}

$remote_dir = "/data/local/tmp/aether-android-smoke"
& $adb -s $serial shell "mkdir -p $remote_dir"
& $adb -s $serial push $so "$remote_dir/libaether_android_smoke.so"
if ($LASTEXITCODE -ne 0) { throw "adb push .so failed" }
& $adb -s $serial push $runner "$remote_dir/aether_android_smoke_runner"
if ($LASTEXITCODE -ne 0) { throw "adb push runner failed" }

Write-Host "Running smoke #1"
$out1 = Invoke-SmokeOnce $adb $serial $remote_dir
Write-Host "Running smoke #2"
$out2 = Invoke-SmokeOnce $adb $serial $remote_dir

Write-Host "Android emulator smoke passed twice."
Write-Host "AVD/device: $serial abi=$abi api=$api"
exit 0
