#Requires -Version 5.1
<#
.SYNOPSIS
  Elevated live fault/recovery test for remote-presence-live.

.DESCRIPTION
  Requires Administrator. Builds the example if needed, then runs the
  multi-process Local/Remote Presence + CloudRequest fault harness.
#>
$ErrorActionPreference = 'Stop'

function Test-IsAdmin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $p = New-Object Security.Principal.WindowsPrincipal($id)
  return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $RepoRoot

if (-not (Test-IsAdmin)) {
  Write-Host 'FAIL: Administrator required for Windows Firewall fault test.'
  Write-Host 'Relaunch elevated:'
  Write-Host ('  Start-Process powershell -Verb RunAs -ArgumentList "-ExecutionPolicy Bypass -File `"{0}`""' -f $PSCommandPath)
  Write-Host 'Or open an elevated Developer PowerShell and run this script.'
  exit 3
}

$Exe = Join-Path $RepoRoot 'build-msvc-presence-ex\remote-presence-live.exe'
if (-not (Test-Path $Exe)) {
  Write-Host 'Building remote-presence-live...'
  & cmd /c (Join-Path $RepoRoot '_build_remote_live.bat')
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$Out = Join-Path $RepoRoot 'remote_presence_live_fault_out.txt'
Write-Host "Running elevated fault harness -> $Out"
& $Exe --healthy-sec 60 --fault-cycles 10 *>&1 | Tee-Object -FilePath $Out
exit $LASTEXITCODE
