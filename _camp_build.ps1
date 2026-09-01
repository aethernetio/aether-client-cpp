$n = Get-Process ninja -ErrorAction SilentlyContinue
if ($n) { Write-Output ("ninja pid=$($n.Id) cpu_s=$([math]::Round($n.TotalProcessorTime.TotalSeconds,1)) ws_mb=$([math]::Round($n.WorkingSet64/1MB,1)) start=$($n.StartTime)") } else { Write-Output 'ninja not running' }
$clang = Get-Process -Name 'clang*','xtensa*','riscv*','cc1*','ld*' -ErrorAction SilentlyContinue | Select-Object -First 5
Get-Process | Where-Object { $_.ProcessName -match 'clang|gcc|riscv|ld\.|cc1|cmake|esp-idf' } | Select-Object Id, ProcessName, @{N='CPU';E={[math]::Round($_.TotalProcessorTime.TotalSeconds,1)}} | Format-Table -AutoSize | Out-String | Write-Output
# log file mtime
$lf = Get-Item 'C:\Users\nickc\Projects\temperature-sensor-prepared\experiments\adaptive_probe_v2_campaign.log'
Write-Output ("log_mtime=$($lf.LastWriteTime) size=$($lf.Length)")
# build dirs recent files
Get-ChildItem 'C:\Users\nickc\Projects\temperature-sensor-prepared' -Recurse -Filter '*.obj' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object { "$($_.LastWriteTime) $($_.FullName.Substring([Math]::Min(80,$_.FullName.Length)))" }
Get-ChildItem 'C:\Users\nickc\Projects\temperature-sensor-prepared\build*' -Directory -ErrorAction SilentlyContinue | ForEach-Object { $newest = Get-ChildItem $_.FullName -Recurse -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; if ($newest) { Write-Output ("builddir $($_.Name) newest=$($newest.LastWriteTime) $($newest.Name)") } }
