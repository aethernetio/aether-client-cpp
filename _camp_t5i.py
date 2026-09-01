import re, json, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
cp = json.loads((exp/"adaptive_probe_checkpoint.json").read_text(encoding="utf-8-sig"))
ch = cp["results"]["TEST5"]["chirkov"]
path = exp/"ADAPTIVE_WIFI_PROBE_REPORT.md"
c = path.read_text(encoding="utf-8")
sec = f"""### TEST5 long-run (interim)

| | chirkov | aethernetio |
|--|---------|-------------|
| hot / received | **{ch.get('hot')} hot / {ch.get('received')} rx** (target 500; near-accept) | pending |
| missing_in_span | {ch.get('missing_in_span')} | pending |

"""
if "### TEST5 long-run" in c:
    c, n = re.subn(r"(?s)### TEST5 long-run.*?(?=### Progress|### Final)", sec, c, count=1)
else:
    c, n = re.subn(r"(?=### Progress \(interim\))", sec+"\n", c, count=1)
print("t5", n)
c = re.sub(r"Checkpoint: step_index=\d+[^\n]*", "Checkpoint: step_index=9 (TEST5 chirkov done hot=499; aethernetio building).", c, count=1)
c = c.replace("| long-run loss | TBD | TBD |", f"| long-run loss | hot={ch.get('hot')}/500 miss={ch.get('missing_in_span')} | TBD |")
new_prog = """### Progress (interim)

| Step | Status |
|------|--------|
| 0-7 TEST1-4 both APs | **done** |
| 8 TEST5 chirkov long | **done** (hot=**499**/500 near-accept) |
| 9 TEST5 aethernetio long | **in progress** (cmake/ninja) |

"""
c, n2 = re.subn(r"(?s)### Progress \(interim\).*?(?=\*\*Blockers fixed)", new_prog, c, count=1)
print("prog", n2)
path.write_text(c, encoding="utf-8")
print("ok", ch)
