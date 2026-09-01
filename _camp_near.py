import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_probe_v2_campaign.py")
t = p.read_text(encoding="utf-8")
old = '''        if hot >= target:
            return st
        # Board can hang after a partial outer; hard-reset if HOT stalls too long.
        if hot > 0 and (time.time() - last_progress) > 180:
            log(f"{tag} HOT stalled at {hot} - hard-reset COM7")'''
# may still have em dash version
old2 = old.replace("HOT stalled at {hot} - hard-reset", "HOT stalled at {hot} — hard-reset")
new = '''        if hot >= target:
            return st
        # One-short near-miss: board often completes outer budget at target-1 and stops.
        if hot >= max(1, target - 1) and (time.time() - last_progress) > 120:
            log(f"{tag} accepting hot={hot}/{target} after stall (near target)")
            return st
        # Board can hang after a partial outer; hard-reset if HOT stalls too long.
        if hot > 0 and (time.time() - last_progress) > 180:
            log(f"{tag} HOT stalled at {hot} - hard-reset COM7")'''
if old in t:
    t = t.replace(old, new)
    print("patched ascii stall")
elif old2 in t:
    t = t.replace(old2, new)
    print("patched unicode stall")
else:
    # show context
    idx = t.find("if hot >= target:")
    print(repr(t[idx:idx+350]))
    raise SystemExit("pattern missing")
p.write_text(t, encoding="utf-8", newline="\n")
print("ok")
