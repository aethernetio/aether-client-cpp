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

  [string]$CMakeVersion = "",

  [string]$NdkVersion = "",

  [switch]$BuildSmoke,

  [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Test-StableVersionLabel([string]$Label) {
  return $Label -notmatch "(?i)(rc|beta|alpha|preview)"
}

function Get-CMakeVersionString([string]$CMakeExe) {
  $text = & $CMakeExe --version 2>&1 | Out-String
  if ($text -match "cmake version\s+(\S+)") {
    return $Matches[1]
  }
  throw "Unable to parse CMake version from $CMakeExe"
}

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

function Resolve-Ndk([string]$SdkRoot, [string]$RequestedVersion) {
  $ndk_root = Join-Path $SdkRoot "ndk"
  if (-not (Test-Path $ndk_root)) {
    throw "No side-by-side NDK under $ndk_root"
  }

  if ($RequestedVersion) {
    $exact = Join-Path $ndk_root $RequestedVersion
    $toolchain = Join-Path $exact "build\cmake\android.toolchain.cmake"
    if (-not (Test-Path $toolchain)) {
      throw "Requested NDK version '$RequestedVersion' not found or incomplete under $ndk_root"
    }
    return $exact
  }

  $versions = Get-ChildItem $ndk_root -Directory |
    Where-Object { Test-StableVersionLabel $_.Name } |
    Sort-Object { [version]($_.Name -replace '[^\d.].*$', '') } -Descending
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

function Resolve-SdkCMakePackage([string]$SdkRoot, [string]$RequestedVersion) {
  $cmake_root = Join-Path $SdkRoot "cmake"
  if (-not (Test-Path $cmake_root)) {
    return $null
  }

  if ($RequestedVersion) {
    $exact_dir = Join-Path $cmake_root $RequestedVersion
    $exact_cmake = Join-Path $exact_dir "bin\cmake.exe"
    if (-not (Test-Path $exact_cmake)) {
      throw "Requested CMake version '$RequestedVersion' not found under $cmake_root"
    }
    if (-not (Test-StableVersionLabel $RequestedVersion)) {
      throw "Requested CMake version '$RequestedVersion' is not a stable release label"
    }
    $ninja = Join-Path $exact_dir "bin\ninja.exe"
    return [pscustomobject]@{
      CMake = $exact_cmake
      Ninja = $(if (Test-Path $ninja) { $ninja } else { $null })
      VersionLabel = $RequestedVersion
      Source = "Android SDK"
    }
  }

  $packages = Get-ChildItem $cmake_root -Directory |
    Where-Object {
      (Test-StableVersionLabel $_.Name) -and
      (Test-Path (Join-Path $_.FullName "bin\cmake.exe"))
    } |
    Sort-Object { [version]($_.Name -replace '[^\d.].*$', '') } -Descending

  if (-not $packages) {
    return $null
  }

  $best = $packages[0]
  $cmake = Join-Path $best.FullName "bin\cmake.exe"
  $ninja = Join-Path $best.FullName "bin\ninja.exe"
  return [pscustomobject]@{
    CMake = $cmake
    Ninja = $(if (Test-Path $ninja) { $ninja } else { $null })
    VersionLabel = $best.Name
    Source = "Android SDK"
  }
}

function Resolve-CMakeAndNinja([string]$SdkRoot, [string]$RequestedCMakeVersion) {
  $sdk_pkg = Resolve-SdkCMakePackage $SdkRoot $RequestedCMakeVersion
  if ($sdk_pkg) {
    $version = Get-CMakeVersionString $sdk_pkg.CMake
    if (-not (Test-StableVersionLabel $version)) {
      throw "Selected SDK CMake reports non-stable version '$version'"
    }
    if (-not $sdk_pkg.Ninja) {
      throw "Ninja.exe missing next to selected SDK CMake $($sdk_pkg.CMake)"
    }
    return [pscustomobject]@{
      CMake = $sdk_pkg.CMake
      Ninja = $sdk_pkg.Ninja
      CMakeVersion = $version
      Source = $sdk_pkg.Source
    }
  }

  if ($RequestedCMakeVersion) {
    throw "Requested CMake version '$RequestedCMakeVersion' was not found in Android SDK cmake packages"
  }

  $path_cmake = Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
  if ($path_cmake -and (Test-Path $path_cmake)) {
    $version = Get-CMakeVersionString $path_cmake
    if (Test-StableVersionLabel $version) {
      $path_ninja = Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
      if (-not $path_ninja) {
        throw "Stable CMake found on PATH ($path_cmake) but Ninja was not found on PATH"
      }
      return [pscustomobject]@{
        CMake = $path_cmake
        Ninja = $path_ninja
        CMakeVersion = $version
        Source = "PATH"
      }
    }
    Write-Host "Ignoring non-stable CMake on PATH: $path_cmake ($version)"
  }

  $pf_cmake = "C:\Program Files\CMake\bin\cmake.exe"
  if (Test-Path $pf_cmake) {
    $version = Get-CMakeVersionString $pf_cmake
    if (Test-StableVersionLabel $version) {
      $path_ninja = Get-Command ninja -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
      if (-not $path_ninja) {
        throw "Stable CMake found in Program Files but Ninja was not found on PATH"
      }
      return [pscustomobject]@{
        CMake = $pf_cmake
        Ninja = $path_ninja
        CMakeVersion = $version
        Source = "Program Files"
      }
    }
    Write-Host "Ignoring non-stable CMake in Program Files: $pf_cmake ($version)"
  }

  throw "No stable CMake found in Android SDK, PATH, or Program Files."
}

function Get-BuildDirName([string]$Abi, [string]$Config, [string]$UserConfig) {
  $cfg_leaf = [System.IO.Path]::GetFileNameWithoutExtension($UserConfig)
  return "build-android-$Abi-$Config-$cfg_leaf"
}

$repo_root = Resolve-RepoRoot
$sdk = Resolve-AndroidSdk
$ndk = Resolve-Ndk $sdk $NdkVersion
$tools = Resolve-CMakeAndNinja $sdk $CMakeVersion
$cmake = $tools.CMake
$ninja = $tools.Ninja
$toolchain = Join-Path $ndk "build\cmake\android.toolchain.cmake"
$build_dir = Join-Path $repo_root (Get-BuildDirName $Abi $Config $UserConfig)

Write-Host "Selected Android SDK : $sdk"
Write-Host "Selected NDK         : $ndk"
Write-Host "Selected CMake       : $cmake"
Write-Host "Selected Ninja       : $ninja"
Write-Host "CMake version        : $($tools.CMakeVersion)"
Write-Host "CMake source         : $($tools.Source)"
Write-Host "Repository root      : $repo_root"
Write-Host "ABI                  : $Abi"
Write-Host "Config               : $Config"
Write-Host "USER_CONFIG          : $UserConfig"
Write-Host "Build directory      : $build_dir"
Write-Host "Build smoke          : $BuildSmoke"

& $cmake --version
$ninja_version = & $ninja --version
Write-Host "Ninja version        : $ninja_version"

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
