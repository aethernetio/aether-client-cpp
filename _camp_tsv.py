import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_probe_v2_campaign.py")
lines = p.read_text(encoding="utf-8").splitlines()
for i, line in enumerate(lines, 1):
    if "start_v2_receiver" in line or "preserve_tsv" in line or "unlink" in line or "write_text" in line or "truncate" in line:
        print(f"{i}|{line}")
print("--- fn ---")
# print function definition
text = p.read_text(encoding="utf-8")
idx = text.find("def start_v2_receiver")
print(text[idx:idx+1200])
# check tsv file mtimes/sizes
out = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_v2_results")
for f in sorted(out.glob("*test4*")):
    print(f.name, f.stat().st_size, "mtime", __import__("time").ctime(f.stat().st_mtime))
