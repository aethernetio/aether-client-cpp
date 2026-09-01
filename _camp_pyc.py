import sys, os, time
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
# pycache
for p in exp.rglob("*.pyc"):
    print("pyc", p, "mtime", time.ctime(p.stat().st_mtime))
camp = exp / "run_adaptive_wifi_probe_campaign.py"
print("camp mtime", time.ctime(camp.stat().st_mtime))
# verify arrow gone
data = camp.read_bytes()
print("has_utf8_arrow", b"\xe2\x86\x92" in data)
print("has_ascii_arrow_msg", b"cold boot -> power-wait" in data)
# log encoding
log = exp / "adaptive_probe_v2_campaign.log"
raw = log.read_bytes()
print("log_size", len(raw), "starts", raw[:8], "has_nulls_ratio", raw[:2000].count(0)/2000)
# last 200 bytes repr
print("log_tail_bytes", raw[-120:])
# process start times
import subprocess
out = subprocess.check_output(["powershell","-NoProfile","-Command",
 "Get-Process -Id 62152,33752,14332,42436,59072,48064 -ErrorAction SilentlyContinue | Select-Object Id,ProcessName,StartTime | Format-List"], text=True, errors="replace")
print(out)
