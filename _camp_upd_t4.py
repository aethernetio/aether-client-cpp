import re, json, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
path = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\ADAPTIVE_WIFI_PROBE_REPORT.md")
c = path.read_text(encoding="utf-8")
cp = json.loads(Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_checkpoint.json").read_text(encoding="utf-8-sig"))
ch = cp["results"]["TEST4"]["chirkov"]
rows = {d["sleep_ms"]: d["delivery"] for d in ch}

new_t4 = f"""### TEST4 prepared_sleep (interim)

| sleep_ms | chirkov hot/rx | aethernetio |
|----------|----------------|-------------|
| 1000 | **{rows[1000]['hot']} hot / {rows[1000]['received']} rx** (60m timeout; target 150) | pending |
| 250 | **{rows[250]['hot']} hot / {rows[250]['received']} rx** (timeout @149) | pending |
| 500 | **{rows[500]['hot']} hot / {rows[500]['received']} rx** (timeout) | pending |

POST used from TEST3 winners: chirkov **25 ms**, aethernetio **0 ms**.

"""

# Insert TEST4 section before Progress if missing, else replace
if "### TEST4 prepared_sleep" in c:
    c2, n = re.subn(r"(?s)### TEST4 prepared_sleep.*?(?=### Progress \(interim\)|### TEST5|### Final)", new_t4, c, count=1)
    print("t4_replace", n)
    c = c2
else:
    # insert after TEST3 section / before Progress
    c2, n = re.subn(r"(?=### Progress \(interim\))", new_t4 + "\n", c, count=1)
    print("t4_insert", n)
    c = c2

new_prog = """### Progress (interim)

| Step | Status |
|------|--------|
| 0-1 TEST1 ICMP both APs | **done** |
| 2-3 TEST2 full_ping both APs | **done** |
| 4-5 TEST3 prepared_nosleep both APs | **done** (POST 25 / 0 ms) |
| 6 TEST4 chirkov prepared_sleep | **done** (s1000=105, s250=149, s500=138 hot) |
| 7 TEST4 aethernetio | **in progress** (cmake/ninja) |
| 8-9 TEST5 | pending |

"""
c3, n2 = re.subn(r"(?s)### Progress \(interim\).*?(?=\*\*Blockers fixed)", new_prog, c, count=1)
print("prog", n2)
c3 = c3.replace(
    "Checkpoint: step_index=6 (TEST3 both APs complete; TEST4 chirkov prepared_sleep starting).",
    "Checkpoint: step_index=7 (TEST4 chirkov done; TEST4 aethernetio starting).",
)
# HOT sleep delivery interim
c3 = c3.replace(
    "| HOT sleep delivery | TBD | TBD |",
    "| HOT sleep delivery | s1000=105 / s250=149 / s500=138 (target 150) | TBD |",
)
path.write_text(c3, encoding="utf-8")
print("OK")
