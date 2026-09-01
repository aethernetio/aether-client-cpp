import sys, json, time
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
j = json.loads((exp/"adaptive_probe_checkpoint.json").read_text(encoding="utf-8-sig"))
print("step", j.get("step_index"), "fatal", j.get("fatal"))
print("TEST4", "TEST4" in (j.get("results") or {}))
print("TEST3_ae_post", (j.get("results") or {}).get("TEST3", {}).get("aethernetio", {}).get("post_winner"))
# progress log is utf-8 from python
prog = exp/"adaptive_probe_progress.log"
if prog.exists():
    lines = prog.read_text(encoding="utf-8", errors="replace").splitlines()
    print("PROGRESS_TAIL:")
    for l in lines[-25:]:
        print(l[:220])
# procs
import subprocess
print("PROCS:")
out = subprocess.check_output(["powershell","-NoProfile","-Command",
 "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'adaptive_probe|watchdog|run_adaptive' } | ForEach-Object { \"$($_.ProcessId) $($_.Name)\" }"], text=True, errors="replace")
print(out)
