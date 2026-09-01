import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
for name in ["run_adaptive_probe_v2_campaign.py", "run_adaptive_wifi_probe_campaign.py"]:
    p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments") / name
    t = p.read_text(encoding="utf-8")
    print("====", name)
    for i, line in enumerate(t.splitlines(), 1):
        low = line.lower()
        if any(k in low for k in ["stall", "timeout", "s1000", "prepared_sleep", "test4", "hot_target", "baseline_hot", "45", "sleep_us", "AE_PROBE_SLEEP"]):
            if "test" in low or "stall" in low or "timeout" in low or "sleep" in low or "hot" in low or "150" in line or "target" in low:
                print(f"{i}|{line[:160]}")
