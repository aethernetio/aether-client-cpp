import sys
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
from pathlib import Path

ps1 = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_probe_v2.ps1")
t = ps1.read_text(encoding="utf-8")
if "Tee-Object" not in t:
    print("already_fixed")
else:
    needle = "& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | Tee-Object -FilePath $log -Append"
    repl = """$env:PYTHONIOENCODING = \"utf-8\"
$env:PYTHONUTF8 = \"1\"
& $py -u experiments/run_adaptive_probe_v2_campaign.py 2>&1 | ForEach-Object {
  $line = \"$_\"
  Add-Content -Path $log -Value $line -Encoding utf8
  Write-Host $line
}"""
    if needle not in t:
        raise SystemExit("needle missing")
    t = t.replace(needle, repl)
    # also fix mojibake comment if present
    t = t.replace("across AP switches вЂ” last", "across AP switches - last")
    t = t.replace("across AP switches \u2014 last", "across AP switches - last")
    ps1.write_text(t, encoding="utf-8", newline="\n")
    print("launcher_fixed", "Tee-Object" in t)

# verify camp module
camp = Path(r"C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\run_adaptive_wifi_probe_campaign.py")
ct = camp.read_text(encoding="utf-8")
print("arrow_in_camp", "\u2192" in ct)
print("ascii_msg", "cold boot -> power-wait" in ct)
print("log_has_try", "except UnicodeEncodeError" in ct)
