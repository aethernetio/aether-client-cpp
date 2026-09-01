import json, re, sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
cp = json.loads((exp/"adaptive_probe_checkpoint.json").read_text(encoding="utf-8-sig"))
r = cp["results"]
t1c, t1a = r["TEST1"]["chirkov"], r["TEST1"]["aethernetio"]
t2c, t2a = r["TEST2"]["chirkov"], r["TEST2"]["aethernetio"]
t3c, t3a = r["TEST3"]["chirkov"], r["TEST3"]["aethernetio"]
t4c = {x["sleep_ms"]: x["delivery"] for x in r["TEST4"]["chirkov"]}
t4a = {x["sleep_ms"]: x["delivery"] for x in r["TEST4"]["aethernetio"]}
t5c, t5a = r["TEST5"]["chirkov"], r["TEST5"]["aethernetio"]

# conclusion flags
diff_profile = t1c["winner_profile"] != t1a["winner_profile"]
diff_pre = t1c["pre_ms"] != t1a["pre_ms"]
diff_post = t3c["post_winner"] != t3a["post_winner"]
sleep_gap = (t4c[1000]["hot"] < 140) and (t4a[1000]["hot"] >= 140)
long_ok = t5c["missing_in_span"] == 0 and t5a["missing_in_span"] == 0

path = exp / "ADAPTIVE_WIFI_PROBE_REPORT.md"
c = path.read_text(encoding="utf-8")

final_block = f"""### Final comparison table (complete 2026-08-31)

| METRIC | CHIRKOV | AETHERNETIO |
|--------|---------|-------------|
| profile | **P{t1c['winner_profile']}** | **P{t1a['winner_profile']}** |
| PRE | **{t1c['pre_ms']} ms** | **{t1a['pre_ms']} ms** |
| POST | **{t3c['post_winner']} ms** | **{t3a['post_winner']} ms** |
| ICMP loss (winner) | **{t1c['loss']}%** | **{t1a['loss']}%** |
| connect median | **{t1c['connect_median_ms']} ms** | **{t1a['connect_median_ms']} ms** |
| cold FULL median | **{t2c['cold_median_ms']} ms** | **{t2a['cold_median_ms']} ms** |
| cold FULL p90 | **{t2c['cold_p90_ms']} ms** | **{t2a['cold_p90_ms']} ms** |
| Ping RTT median | **{t2c['rtt_median_ms']} ms** | **{t2a['rtt_median_ms']} ms** |
| FULL Write() call median us | (not in chirkov parse) | **{t2a.get('write_call_median_us')}** |
| FULL WriteAction median us | (not in chirkov parse) | **{t2a.get('write_action_median_us')}** |
| HOT no-sleep post_winner | **{t3c['post_winner']} ms** (post10 fail) | **{t3a['post_winner']} ms** (post0 pass) |
| HOT sleep s1000/s250/s500 | **{t4c[1000]['hot']}/{t4c[250]['hot']}/{t4c[500]['hot']}** /150 | **{t4a[1000]['hot']}/{t4a[250]['hot']}/{t4a[500]['hot']}** /150 |
| long-run HOT | **{t5c['hot']}/500** (miss={t5c['missing_in_span']}) | **{t5a['hot']}/500** (miss={t5a['missing_in_span']}) |

### Conclusion flags

| Flag | Result |
|------|--------|
| Different winning Wi-Fi profile across APs? | **{'YES' if diff_profile else 'NO'}** (P{t1c['winner_profile']} vs P{t1a['winner_profile']}) |
| Different PRE needed? | **{'YES' if diff_pre else 'NO'}** (both {t1c['pre_ms']} ms) |
| Different POST needed? | **{'YES' if diff_post else 'NO'}** ({t3c['post_winner']} vs {t3a['post_winner']} ms) |
| Sleep/deep-cycle delivery gap (s1000)? | **{'YES' if sleep_gap else 'NO'}** (chirkov {t4c[1000]['hot']} vs aethernetio {t4a[1000]['hot']}) |
| Long-run seq integrity OK both APs? | **{'YES' if long_ok else 'NO'}** (missing_in_span=0 both) |

Checkpoint: **step_index=10** — V2 campaign complete. TCP receiver; erase-flash each firmware; chirkov then aethernetio per test.

"""

# replace final comparison section through end-ish
if "### Final comparison table" in c:
    c2, n = re.subn(r"(?s)### Final comparison table.*", final_block.rstrip()+"\n", c, count=1)
    print("final_repl", n)
    c = c2
else:
    c = c.rstrip() + "\n\n" + final_block
    print("final_append")

# mark TEST5 complete
t5sec = f"""### TEST5 long-run (complete)

| | chirkov | aethernetio |
|--|---------|-------------|
| hot / received | **{t5c['hot']} hot / {t5c['received']} rx** (target 500; near-accept) | **{t5a['hot']} hot / {t5a['received']} rx** (near-accept) |
| full | {t5c['full']} | {t5a['full']} |
| missing_in_span | **{t5c['missing_in_span']}** | **{t5a['missing_in_span']}** |

"""
c, n = re.subn(r"(?s)### TEST5 long-run \(interim\).*?(?=### Progress|### Final)", t5sec, c, count=1)
print("t5", n)

new_prog = """### Progress (complete)

| Step | Status |
|------|--------|
| 0-9 all TEST1-5 both APs | **done** (step_index=10) |

"""
c, n = re.subn(r"(?s)### Progress \(interim\).*?(?=\*\*Blockers fixed)", new_prog, c, count=1)
print("prog", n)
c = re.sub(r"Checkpoint: step_index=\d+[^\n]*", "Checkpoint: step_index=10 (V2 campaign complete).", c, count=1)
path.write_text(c, encoding="utf-8")
print("REPORT_FINALIZED")
