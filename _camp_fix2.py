import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path

# Convert campaign log UTF-16 LE -> UTF-8
log = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_v2_campaign.log")
raw = log.read_bytes()
if raw.startswith(b"\xff\xfe") or (len(raw) > 4 and raw[1] == 0 and raw[3] == 0):
    text = raw.decode("utf-16")
    log.write_text(text, encoding="utf-8", newline="\n")
    print("converted_log_to_utf8", "chars", len(text), "new_size", log.stat().st_size)
else:
    print("log_already_ok", raw[:4])

# Fix launcher: avoid Tee-Object UTF-16
ps1 = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_probe_v2.ps1")
t = ps1.read_text(encoding="utf-8")
old = '& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | Tee-Object -FilePath $log -Append\r\nexit $LASTEXITCODE'
# try both newline styles
replaced = False
for old in [
    '& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | Tee-Object -FilePath $log -Append\nexit $LASTEXITCODE',
    '& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | Tee-Object -FilePath $log -Append\r\nexit $LASTEXITCODE',
]:
    if old in t:
        new = '''$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUTF8 = "1"
& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | ForEach-Object {
  $line = "$_"
  Add-Content -Path $log -Value $line -Encoding utf8
  Write-Host $line
}
exit $LASTEXITCODE'''
        if "\r\n" in old:
            new = new.replace("\n", "\r\n")
        t = t.replace(old, new)
        replaced = True
        break
if not replaced:
    # softer match
    if "Tee-Object" in t:
        t2 = t.replace(
            "& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | Tee-Object -FilePath $log -Append",
            '''$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUTF8 = "1"
& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | ForEach-Object {
  $line = "$_"
  Add-Content -Path $log -Value $line -Encoding utf8
  Write-Host $line
}'''
        )
        if t2 != t:
            t = t2
            replaced = True
ps1.write_text(t, encoding="utf-8", newline="\n")
print("launcher_fixed", replaced, "still_has_tee", "Tee-Object" in t)

# Harden log() further in campaign module (belt and suspenders)
camp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_wifi_probe_campaign.py")
ct = camp.read_text(encoding="utf-8")
# ensure arrow ascii
if "\u2192" in ct:
    ct = ct.replace("\u2192", "->")
    print("replaced_arrows_in_camp")
camp.write_text(ct, encoding="utf-8", newline="\n")
print("camp_ok")
