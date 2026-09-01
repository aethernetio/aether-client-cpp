import sys, json
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
cp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_checkpoint.json")
raw = cp.read_bytes()[:8]
print("head_bytes", raw)
text = cp.read_text(encoding="utf-8-sig")
# rewrite without BOM if needed
if cp.read_bytes().startswith(b"\xef\xbb\xbf"):
    cp.write_text(text, encoding="utf-8")
    print("stripped_BOM")
j = json.loads(cp.read_text(encoding="utf-8"))
print("step", j.get("step_index"), "fatal", repr(j.get("fatal")))
for name in ["run_adaptive_probe_v2_campaign.py", "run_adaptive_wifi_probe_campaign.py"]:
    p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments") / name
    t = p.read_text(encoding="utf-8")
    print("====", name)
    for i, line in enumerate(t.splitlines(), 1):
        if "CHECKPOINT" in line or "json.load" in line or "utf-8-sig" in line or "open(CHECKPOINT" in line.replace(" ", ""):
            print(f"{i}|{line}")
        if "load_checkpoint" in line or "save_checkpoint" in line or "checkpoint.json" in line:
            print(f"{i}|{line}")
log = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_v2_campaign.log").read_text(encoding="utf-8", errors="replace").splitlines()
print("==== crash context ====")
for i, line in enumerate(log):
    if "UnicodeEncodeError" in line or "Traceback" in line or "cleared fatal" in line or "STEP 6" in line or "STEP 7" in line or "fatal=" in line:
        start = max(0, i - 2)
        end = min(len(log), i + 5)
        if i > 0 and "Unicode" not in log[i-1] and "Traceback" not in line and "STEP" in line:
            print(f"{i}|{line[:220]}")
        elif "Unicode" in line or "Traceback" in line or "fatal" in line.lower():
            for j in range(start, end):
                print(f"{j}|{log[j][:220]}")
            print("---")
