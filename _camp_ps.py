import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
for name in ["run_adaptive_probe_v2.ps1", "run_adaptive_probe_v2_watchdog.ps1"]:
    p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments") / name
    print("====", name)
    print(p.read_text(encoding="utf-8", errors="replace"))
