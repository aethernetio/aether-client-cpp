import sys, json, time, subprocess
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path

exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
cp_path = exp / "adaptive_probe_checkpoint.json"
j = json.loads(cp_path.read_text(encoding="utf-8-sig"))
# keep TEST4 chirkov only
t4 = j.setdefault("results", {}).setdefault("TEST4", {})
if "aethernetio" in t4:
    del t4["aethernetio"]
    print("removed polluted TEST4 aethernetio")
j["step_index"] = 7
j.pop("fatal", None)
cp_path.write_text(json.dumps(j, indent=2), encoding="utf-8")
print("checkpoint step", j["step_index"], "TEST4 keys", list(t4.keys()))

# remove polluted tsv
for name in ["aethernetio_test4_sleep.tsv", "aethernetio_test4_rx.log", "aethernetio_test4_rx.err"]:
    p = exp / "adaptive_probe_v2_results" / name
    if p.exists():
        p.unlink()
        print("deleted", name)

# kill campaign tree: launcher 59072 and campaign pythons; leave watchdog 42436 and ppk hold
kill_ids = []
out = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 """
Get-CimInstance Win32_Process | Where-Object {
  $_.CommandLine -and (
    $_.CommandLine -match 'run_adaptive_probe_v2_campaign\\.py' -or
    $_.CommandLine -match 'run_adaptive_probe_v2\\.ps1' -or
    $_.CommandLine -match 'temperature_receiver\\.exe' -or
    ($_.Name -match 'ninja|cmake' -and $_.CommandLine -match 'build-esp32c6-adaptive-probe')
  ) -and $_.CommandLine -notmatch 'watchdog'
} | ForEach-Object { Write-Output $_.ProcessId }
"""
], text=True, errors="replace")
ids = [int(x) for x in out.split() if x.strip().isdigit()]
print("kill candidates", ids)
for pid in ids:
    subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
    print("killed", pid)
time.sleep(3)
# verify
out2 = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'run_adaptive_probe_v2_campaign|run_adaptive_probe_v2\\.ps1' } | ForEach-Object { \"$($_.ProcessId) $($_.CommandLine.Substring(0,[Math]::Min(100,$_.CommandLine.Length)))\" }"
], text=True, errors="replace")
print("remaining:\n", out2)
print("watchdog should autoresume within ~90s")
