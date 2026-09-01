import sys, subprocess
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
out = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 """
Get-CimInstance Win32_Process | Where-Object {
  $_.Name -match 'python|ninja|cmake|powershell' -and $_.CommandLine -and (
    $_.CommandLine -match 'adaptive|temperature_receiver|ninja|cmake|watchdog|ppk'
  )
} | ForEach-Object {
  $c = $_.CommandLine
  if ($c.Length -gt 250) { $c = $c.Substring(0,250) }
  Write-Output (\"{0}|{1}|{2}\" -f $_.ProcessId, $_.Name, $c)
}
"""
], text=True, errors="replace")
print(out)
