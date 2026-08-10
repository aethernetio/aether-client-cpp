# Copyright 2026 Aethernet Inc.
#
# Build aether (and optional Android NDK smoke targets) with the Android NDK.

[CmdletBinding()]
param(
  [ValidateSet("x86_64", "arm64-v8a")]
  [string]$Abi = "x86_64",

  [ValidateSet("Debug", "Release")]
  [string]$Config = "Debug",

  [string]$UserConfig = "config/user_config_hydrogen.h",

  [string]$AndroidPlatform = "android-24",

  [string]$AndroidStl = "c++_static",

  [switch]$BuildSmoke,

  [switch]$Clean
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
  throw "Android SDK not found. Set ANDROID_SDK_ROOT or install Android Studio SDK."
}

function Resolve-Ndk([string]$SdkRoot) {
  $ndk_root = Join-Path $SdkRoot "ndk"
  if (-not (Test-Path $ndk_root)) {
    throw "No side-by-side NDK under $ndk_root"
  }
  $versions = Get-ChildItem $ndk_root -Directory |
    Where-Object { $_.Name -notmatch "(?i)(rc|beta|preview)" } |
    Sort-Object { [version]($_.Name -replace '[^\d.].*$','') } -Descending
  if (-not $versions) {
    throw "No stable NDK version found under $ndk_root"
  }
  foreach ($v in $versions) {
    $toolchain = Join-Path $v.FullName "build\cmake\android.toolchain.cmake"
    if (Test-Path $toolchain) {
      return $v.FullName
    }
  }
  throw "No fully installed stable NDK with android.toolchain.cmake found."
}

function Resolve-CMake {
  $candidates = @(
    (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
    "C:\Program Files\CMake\bin\cmake.exe"
  ) | Where-Object { $_ -and (Test-Path $_) }
  if (-not $candidates) {
    throw "CMake not found on PATH or in Program Files."
  }
  return $candidates[0]
}

function Resolve-Ninja([string]$SdkRoot) {
  $from_path = Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
  if ($from_path) {
    return $from_path
  }
  $sdk_cmake = Join-Path $SdkRoot "cmake"
  if (Test-Path $sdk_cmake) {
    $ninja = Get-ChildItem $sdk_cmake -Recurse -Filter ninja.exe -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending |
      Select-Object -First 1
    if ($ninja) {
      return $ninja.FullName
    }
  }
  throw "Ninja not found on PATH or under Android SDK cmake packages."
}

function Get-BuildDirName([string]$Abi, [string]$Config, [string]$UserConfig) {
  $cfg_leaf = [System.IO.Path]::GetFileNameWithoutExtension($UserConfig)
  return "build-android-$Abi-$Config-$cfg_leaf"
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$ndk = Resolve-Ndk $sdk
$cmake = Resolve-CMake
$ninja = Resolve-Ninja $sdk
$toolchain = Join-Path $ndk "build\cmake\android.toolchain.cmake"
$build_dir = Join-Path $repo_root (Get-BuildDirName $Abi $Config $UserConfig)

Write-Host "Repository root : $repo_root"
Write-Host "Android SDK     : $sdk"
Write-Host "Android NDK     : $ndk"
Write-Host "CMake           : $cmake"
Write-Host "Ninja           : $ninja"
Write-Host "ABI             : $Abi"
Write-Host "Config          : $Config"
Write-Host "USER_CONFIG     : $UserConfig"
Write-Host "Build directory : $build_dir"
Write-Host "Build smoke     : $BuildSmoke"

& $cmake --version
& $ninja --version

if ($Clean -and (Test-Path $build_dir)) {
  Write-Host "Cleaning $build_dir"
  Remove-Item -Recurse -Force $build_dir
}

New-Item -ItemType Directory -Force -Path $build_dir | Out-Null

$configure_args = @(
  "-S", $repo_root,
  "-B", $build_dir,
  "-G", "Ninja",
  "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
  "-DANDROID_ABI=$Abi",
  "-DANDROID_PLATFORM=$AndroidPlatform",
  "-DANDROID_STL=$AndroidStl",
  "-DCMAKE_BUILD_TYPE=$Config",
  "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
  "-DCMAKE_MAKE_PROGRAM=$ninja",
  "-DAE_BUILD_TOOLS=OFF",
  "-DAE_BUILD_EXAMPLES=OFF",
  "-DAE_BUILD_TESTS=OFF",
  "-DAE_INSTALL=OFF",
  "-DAE_DISTILLATION=ON",
  "-DAE_FILTRATION=ON",
  "-DUSER_CONFIG=$UserConfig"
)

if ($BuildSmoke) {
  $configure_args += "-DAE_BUILD_ANDROID_SMOKE=ON"
} else {
  $configure_args += "-DAE_BUILD_ANDROID_SMOKE=OFF"
}

Write-Host "Configure command:"
Write-Host ("  {0} {1}" -f $cmake, ($configure_args -join " "))
& $cmake @configure_args
if ($LASTEXITCODE -ne 0) {
  throw "CMake configure failed with exit code $LASTEXITCODE"
}

$targets = @("aether")
if ($BuildSmoke) {
  $targets += @("aether_android_smoke", "aether_android_smoke_runner")
}

foreach ($target in $targets) {
  Write-Host "Build command:"
  Write-Host ("  {0} --build {1} --target {2} --parallel" -f $cmake, $build_dir, $target)
  & $cmake --build $build_dir --target $target --parallel
  if ($LASTEXITCODE -ne 0) {
    throw "Build of target $target failed with exit code $LASTEXITCODE"
  }
}

Write-Host "Android NDK build succeeded."
Write-Host "Artifacts under: $build_dir"
exit 0
