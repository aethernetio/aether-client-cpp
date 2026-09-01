$path = 'C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\ADAPTIVE_WIFI_PROBE_REPORT.md'
$lines = Get-Content $path
# print lines 100-180
for ($i=99; $i -lt [Math]::Min(180,$lines.Count); $i++) { Write-Output ("{0,4}|{1}" -f ($i+1), $lines[$i]) }
