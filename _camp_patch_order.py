import sys, re
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
p = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_probe_v2_campaign.py")
t = p.read_text(encoding="utf-8")

old_t4 = '''def run_test4(ap: str, results: dict) -> None:
    t3 = results.get("TEST3", {}).get(ap, {})
    post_w = int(t3.get("post_winner", 300))
    tsv = V2_OUT / f"{ap}_test4_sleep.tsv"
    rx_log = V2_OUT / f"{ap}_test4_rx.log"
    sleep_results = []
    for sleep_ms in (1000, 250, 500):
        start_v2_receiver(f"v2_t4_{ap}_s{sleep_ms}", tsv, rx_log)
        camp.cmake_configure(
            ap,
            "C",
            phase_c_defs(ap, results, nosleep=False, sleep_us=sleep_ms * 1000, outer=5, hot=30, post=post_w, run_id=100 + sleep_ms),
        )
        camp.ninja_build()
        camp.flash_after_ppk_power_cycle(erase=True)
        st = wait_hot_delivery(tsv, 150, rx_log, f"T4_{ap}_s{sleep_ms}", 60 * 60)
        sleep_results.append({"sleep_ms": sleep_ms, "delivery": st})
    results.setdefault("TEST4", {})[ap] = sleep_results'''

new_t4 = '''def run_test4(ap: str, results: dict) -> None:
    t3 = results.get("TEST3", {}).get(ap, {})
    post_w = int(t3.get("post_winner", 300))
    tsv = V2_OUT / f"{ap}_test4_sleep.tsv"
    rx_log = V2_OUT / f"{ap}_test4_rx.log"
    sleep_results = []
    for sleep_ms in (1000, 250, 500):
        # Build/flash first, then start receiver: otherwise the previous image
        # keeps delivering into the TSV during cmake/ninja and inflates HOT.
        camp.cmake_configure(
            ap,
            "C",
            phase_c_defs(ap, results, nosleep=False, sleep_us=sleep_ms * 1000, outer=5, hot=30, post=post_w, run_id=100 + sleep_ms),
        )
        camp.ninja_build()
        camp.flash_after_ppk_power_cycle(erase=True)
        start_v2_receiver(f"v2_t4_{ap}_s{sleep_ms}", tsv, rx_log)
        st = wait_hot_delivery(tsv, 150, rx_log, f"T4_{ap}_s{sleep_ms}", 60 * 60)
        sleep_results.append({"sleep_ms": sleep_ms, "delivery": st})
    results.setdefault("TEST4", {})[ap] = sleep_results'''

if old_t4 not in t:
    raise SystemExit("run_test4 block not found exactly")
t = t.replace(old_t4, new_t4)

old_t5 = '''def run_test5(ap: str, results: dict) -> None:
    t3 = results.get("TEST3", {}).get(ap, {})
    post_w = int(t3.get("post_winner", 300))
    tsv = V2_OUT / f"{ap}_test5_long.tsv"
    rx_log = V2_OUT / f"{ap}_test5_rx.log"
    start_v2_receiver(f"v2_t5_{ap}_long", tsv, rx_log)
    camp.cmake_configure(
        ap,
        "C",
        phase_c_defs(ap, results, nosleep=False, sleep_us=1_000_000, outer=10, hot=50, post=post_w, run_id=500),
    )
    camp.ninja_build()
    flash_erase_always()
    st = wait_hot_delivery(tsv, 500, rx_log, f"T5_{ap}_long", 3 * 60 * 60)
    results.setdefault("TEST5", {})[ap] = st'''

new_t5 = '''def run_test5(ap: str, results: dict) -> None:
    t3 = results.get("TEST3", {}).get(ap, {})
    post_w = int(t3.get("post_winner", 300))
    tsv = V2_OUT / f"{ap}_test5_long.tsv"
    rx_log = V2_OUT / f"{ap}_test5_rx.log"
    camp.cmake_configure(
        ap,
        "C",
        phase_c_defs(ap, results, nosleep=False, sleep_us=1_000_000, outer=10, hot=50, post=post_w, run_id=500),
    )
    camp.ninja_build()
    flash_erase_always()
    start_v2_receiver(f"v2_t5_{ap}_long", tsv, rx_log)
    st = wait_hot_delivery(tsv, 500, rx_log, f"T5_{ap}_long", 3 * 60 * 60)
    results.setdefault("TEST5", {})[ap] = st'''

if old_t5 not in t:
    raise SystemExit("run_test5 block not found exactly")
t = t.replace(old_t5, new_t5)

# Also fix em dash in stall message for cp1251 safety
t = t.replace("HOT stalled at {hot} — hard-reset", "HOT stalled at {hot} - hard-reset")
t = t.replace("receiver dead — restart", "receiver dead - restart")

p.write_text(t, encoding="utf-8", newline="\n")
print("patched ok")
