import sys, subprocess
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
out = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 """
$ids = 62152,33752,59072,42436,48064,68888,69164,69180
Get-CimInstance Win32_Process | Where-Object { $ids -contains $_.ProcessId -or ($_.ParentProcessId -and ($ids -contains $_.ParentProcessId)) } |
  Select-Object ProcessId, ParentProcessId, Name, CreationDate, @{N='Cmd';E={ if ($_.CommandLine.Length -gt 180) { $_.CommandLine.Substring(0,180) } else { $_.CommandLine } }} |
  Format-List
"""
], text=True, errors="replace")
print(out)
# also check cmake
out2 = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 "Get-Process cmake,ninja -ErrorAction SilentlyContinue | Format-List Id,StartTime,CPU"
], text=True, errors="replace")
print(out2)
