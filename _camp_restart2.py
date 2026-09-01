import sys, json, time, subprocess
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path

out = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 """
Get-CimInstance Win32_Process | Where-Object {
  $_.CommandLine -and (
    $_.CommandLine -match 'run_adaptive_probe_v2_campaign\\.py' -or
    ($_.CommandLine -match 'run_adaptive_probe_v2\\.ps1' -and $_.CommandLine -notmatch 'watchdog') -or
    $_.CommandLine -match 'temperature_receiver\\.exe' -or
    ($_.Name -match '^(ninja|cmake)\\.exe$' -and $_.CommandLine -match 'adaptive-probe')
  )
} | ForEach-Object { Write-Output $_.ProcessId }
"""
], text=True, errors="replace")
ids = [int(x) for x in out.split() if x.strip().isdigit()]
print("kill candidates", ids)
for pid in ids:
    r = subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True, text=True)
    print("kill", pid, r.returncode, (r.stdout or r.stderr or "").strip()[:120])
time.sleep(4)

exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
cp_path = exp / "adaptive_probe_checkpoint.json"
j = json.loads(cp_path.read_text(encoding="utf-8-sig"))
t4 = j.setdefault("results", {}).setdefault("TEST4", {})
t4.pop("aethernetio", None)
j["step_index"] = 7
j.pop("fatal", None)
cp_path.write_text(json.dumps(j, indent=2), encoding="utf-8")
print("checkpoint ok", j["step_index"], list(t4.keys()))

for name in ["aethernetio_test4_sleep.tsv", "aethernetio_test4_rx.log", "aethernetio_test4_rx.err"]:
    p = exp / "adaptive_probe_v2_results" / name
    if p.exists():
        try:
            p.unlink()
            print("deleted", name)
        except Exception as e:
            print("del fail", name, e)

# confirm watchdog alive
out2 = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'watchdog|run_adaptive_probe_v2' } | ForEach-Object { \"$($_.ProcessId) $($_.Name) $($_.CommandLine.Substring(0,[Math]::Min(90,$_.CommandLine.Length)))\" }"
], text=True, errors="replace")
print("after:\n", out2)
