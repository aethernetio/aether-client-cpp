$j = Get-Content 'C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_checkpoint.json' -Raw | ConvertFrom-Json
foreach ($ap in @('chirkov','aethernetio')) {
  Write-Output "==== TEST3 $ap ===="
  $r = $j.results.TEST3.$ap
  Write-Output ("post_winner=" + $r.post_winner)
  Write-Output "BASELINE:"
  $r.baseline | ConvertTo-Json -Depth 6 -Compress
  Write-Output "POST:"
  $r.post | ConvertTo-Json -Depth 6 -Compress
}
Write-Output "==== TEST2 ===="
$j.results.TEST2 | ConvertTo-Json -Depth 5 -Compress
Write-Output "==== REPORT interim ===="
Select-String -Path 'C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\ADAPTIVE_WIFI_PROBE_REPORT.md' -Pattern 'interim|TEST3|TEST4|Interim|## ' | Select-Object -First 40 | ForEach-Object { "$($_.LineNumber):$($_.Line)" }
