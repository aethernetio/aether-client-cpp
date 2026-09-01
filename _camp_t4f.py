import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_probe_v2_campaign.py")
lines = p.read_text(encoding="utf-8").splitlines()
for i in range(308, 460):
    print(f"{i+1}|{lines[i]}")
