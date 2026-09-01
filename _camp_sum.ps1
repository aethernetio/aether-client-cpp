$j = Get-Content 'C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_checkpoint.json' -Raw | ConvertFrom-Json
Write-Output ("step_index=" + $j.step_index)
Write-Output ("fatal=" + $j.fatal)
Write-Output ("updated=" + $j.updated)
Write-Output ("phase=" + $j.phase)
Write-Output ("current=" + $j.current)
foreach ($t in @('TEST1','TEST2','TEST3','TEST4','TEST5')) {
  if ($j.results.$t) {
    foreach ($ap in $j.results.$t.PSObject.Properties.Name) {
      $r = $j.results.$t.$ap
      $pw = $r.post_winner; if (-not $pw) { $pw = $r.PSObject.Properties['post_winner_profile'] }
      Write-Output ("$t/$ap winner=$($r.winner_profile) post=$($r.post_winner) pre_ms=$($r.pre_ms) loss=$($r.loss) median=$($r.connect_median_ms) hot=$($r.hot) props=$($r.PSObject.Properties.Name -join '|')")
    }
  } else { Write-Output "$t missing" }
}
Write-Output '---LOG---'
Get-Content 'C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_v2_campaign.log' -Tail 35
Write-Output '---PROCS---'
Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and ($_.CommandLine -match 'adaptive_probe|watchdog|run_adaptive') } | ForEach-Object { "$($_.ProcessId) $($_.Name)" }
Get-Process ninja -ErrorAction SilentlyContinue | ForEach-Object { "ninja pid=$($_.Id)" }
Get-Process cmake -ErrorAction SilentlyContinue | ForEach-Object { "cmake pid=$($_.Id)" }
