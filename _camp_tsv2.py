import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path
tsv = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_v2_results\aethernetio_test4_sleep.tsv")
text = tsv.read_text(encoding="utf-8", errors="replace").splitlines()
print("lines", len(text))
print("header", text[0] if text else None)
for L in text[1:6]:
    print(L[:200])
print("---tail---")
for L in text[-5:]:
    print(L[:200])
# tags
from collections import Counter
tags = Counter()
for L in text[1:]:
    parts = L.split("\t")
    if parts:
        tags[parts[0] if len(parts)>1 else "?"] += 1
# try find tag column
if text:
    hdr = text[0].split("\t")
    print("cols", hdr)
    for name in ("tag","bench","run","ssid"):
        if name in hdr:
            i = hdr.index(name)
            c = Counter(L.split("\t")[i] for L in text[1:] if len(L.split("\t"))>i)
            print(name, c.most_common(10))
