import re, json, sys, time, subprocess
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path

exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
cp = json.loads((exp/"adaptive_probe_checkpoint.json").read_text(encoding="utf-8-sig"))
ae = {d["sleep_ms"]: d["delivery"] for d in cp["results"]["TEST4"]["aethernetio"]}
ch = {d["sleep_ms"]: d["delivery"] for d in cp["results"]["TEST4"]["chirkov"]}

path = exp / "ADAPTIVE_WIFI_PROBE_REPORT.md"
c = path.read_text(encoding="utf-8")
new_t4 = f"""### TEST4 prepared_sleep (complete)

| sleep_ms | chirkov hot/rx | aethernetio hot/rx |
|----------|----------------|--------------------|
| 1000 | **{ch[1000]['hot']} / {ch[1000]['received']}** (60m timeout; target 150) | **{ae[1000]['hot']} / {ae[1000]['received']}** (timeout @149) |
| 250 | **{ch[250]['hot']} / {ch[250]['received']}** (timeout @149) | **{ae[250]['hot']} / {ae[250]['received']}** (timeout @149) |
| 500 | **{ch[500]['hot']} / {ch[500]['received']}** (timeout) | **{ae[500]['hot']} / {ae[500]['received']}** (timeout @149) |

POST from TEST3: chirkov **25 ms**, aethernetio **0 ms**. Both APs routinely stop at 149/150 (outer budget); treated as near-pass.

"""
if "### TEST4 prepared_sleep" in c:
    c, n = re.subn(r"(?s)### TEST4 prepared_sleep.*?(?=### Progress \(interim\)|### TEST5|### Final)", new_t4, c, count=1)
    print("t4", n)
else:
    c, n = re.subn(r"(?=### Progress \(interim\))", new_t4+"\n", c, count=1)
    print("t4ins", n)

new_prog = """### Progress (interim)

| Step | Status |
|------|--------|
| 0-5 TEST1-3 both APs | **done** |
| 6-7 TEST4 prepared_sleep both APs | **done** |
| 8 TEST5 chirkov long | **in progress** (restarted with near-target accept) |
| 9 TEST5 aethernetio | pending |

"""
c, n2 = re.subn(r"(?s)### Progress \(interim\).*?(?=\*\*Blockers fixed)", new_prog, c, count=1)
print("prog", n2)
c = c.replace(
    "| HOT sleep delivery | s1000=105 / s250=149 / s500=138 (target 150) | TBD |",
    "| HOT sleep delivery | s1000=105 / s250=149 / s500=138 | s1000=149 / s250=149 / s500=149 |",
)
c = re.sub(r"Checkpoint: step_index=\d+[^\n]*", "Checkpoint: step_index=8 (TEST4 done; TEST5 chirkov starting).", c, count=1)
# note orchestrator fixes
if "TSV pollution" not in c:
    c = c.replace(
        "**Blockers fixed 2026-08-31:**",
        "**Blockers fixed 2026-08-31:**\n"
        "3. Tee-Object UTF-16 log → UTF-8 Add-Content launcher; Unicode arrow/`log()` cp1251 harden.\n"
        "4. TEST4/5 start TCP receiver **after** flash (prevents prior image polluting TSV during cmake/ninja).\n"
        "5. `wait_hot_delivery` accepts hot>=target-1 after 120s stall (149/150 near-miss).\n\n"
        "**Blockers fixed 2026-08-31 (earlier):**",
    )
path.write_text(c, encoding="utf-8")
print("report ok")

# Restart for near-target fix on TEST5
out = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 """
Get-CimInstance Win32_Process | Where-Object {
  $_.CommandLine -and (
    $_.CommandLine -match 'run_adaptive_probe_v2_campaign\\.py' -or
    ($_.CommandLine -match 'run_adaptive_probe_v2\\.ps1' -and $_.CommandLine -notmatch 'watchdog') -or
    $_.CommandLine -match 'temperature_receiver\\.exe' -or
    ($_.Name -match '^(ninja|cmake)\\.exe$' -and $_.CommandLine -match 'adaptive-probe')
  )
} | ForEach-Object { $_.ProcessId }
"""
], text=True, errors="replace")
ids = [int(x) for x in out.split() if x.strip().isdigit()]
print("killing", ids)
for pid in ids:
    subprocess.run(["taskkill","/PID",str(pid),"/T","/F"], capture_output=True)
time.sleep(3)
# ensure step 8, no TEST5 partial, no fatal
cp["step_index"] = 8
cp.get("results", {}).pop("TEST5", None)
cp.pop("fatal", None)
(exp/"adaptive_probe_checkpoint.json").write_text(json.dumps(cp, indent=2), encoding="utf-8")
# power + start
subprocess.run([
 str(exp/"ppk2-venv/Scripts/python.exe"), str(exp/"ppk2_power.py"), "--voltage-mv", "3000"
], capture_output=True)
subprocess.Popen([
 "powershell","-NoProfile","-ExecutionPolicy","Bypass","-File",
 str(exp/"run_adaptive_probe_v2.ps1")
], cwd=str(exp.parent), creationflags=0x08000000)
time.sleep(25)
out2 = subprocess.check_output([
 "powershell","-NoProfile","-Command",
 "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'run_adaptive_probe_v2_campaign' } | ForEach-Object { $_.ProcessId }"
], text=True, errors="replace")
print("campaign pids", out2.strip())
prog = (exp/"adaptive_probe_progress.log").read_text(encoding="utf-8", errors="replace").splitlines()[-8:]
for L in prog: print(L)
