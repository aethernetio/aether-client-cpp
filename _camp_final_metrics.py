import json, sys, re
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
exp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments")
cp = json.loads((exp/"adaptive_probe_checkpoint.json").read_text(encoding="utf-8-sig"))
r = cp["results"]
# print key metrics for report
print(json.dumps({
  "TEST1": {ap: {k: r["TEST1"][ap].get(k) for k in ("winner_profile","pre_ms","loss","connect_median_ms")} for ap in ("chirkov","aethernetio")},
  "TEST2": {ap: {k: r["TEST2"][ap].get(k) for k in ("ping_ok","cold_median_ms","cold_p90_ms","rtt_median_ms","write_call_median_us","write_action_median_us")} for ap in ("chirkov","aethernetio")},
  "TEST3": {ap: {"post_winner": r["TEST3"][ap].get("post_winner"), "baseline_hot": r["TEST3"][ap]["baseline"].get("hot")} for ap in ("chirkov","aethernetio")},
  "TEST4": {ap: [(x["sleep_ms"], x["delivery"].get("hot")) for x in r["TEST4"][ap]] for ap in ("chirkov","aethernetio")},
  "TEST5": {ap: {k: r["TEST5"][ap].get(k) for k in ("hot","received","missing_in_span","full")} for ap in ("chirkov","aethernetio")},
}, indent=2))
# summary md if any
sm = exp/"adaptive_probe_v2_results"/"v2_summary.md"
if sm.exists():
    print("---SUMMARY---")
    print(sm.read_text(encoding="utf-8")[:3000])
