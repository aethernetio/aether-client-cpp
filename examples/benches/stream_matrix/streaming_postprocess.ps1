param(
  [Parameter(Mandatory = $true)][string]$BenchOut
)

$tcp = Join-Path $BenchOut "tcp_streaming.csv"
$udp = Join-Path $BenchOut "udp_safestream_streaming.csv"
$best = Join-Path $BenchOut "best_results.csv"
$md = Join-Path $BenchOut "streaming_comparison.md"

function Import-StreamingCsv($path) {
  if (-not (Test-Path $path)) { return @() }
  Import-Csv $path | ForEach-Object {
    $_ | Add-Member -NotePropertyName goodput -NotePropertyValue ([double]$_.goodput_mib_s) -Force
    $_
  }
}

$rows = @()
$rows += Import-StreamingCsv $tcp
$rows += Import-StreamingCsv $udp

$ok = $rows | Where-Object { $_.delivery_ok -eq "1" -and $_.outcome -eq "OK" }
$ok | Sort-Object { [double]$_.goodput_mib_s } -Descending | Select-Object -First 20 |
  Export-Csv $best -NoTypeInformation

$tcpBest = ($ok | Where-Object { $_.transport -eq "TCP" } | Sort-Object goodput -Descending | Select-Object -First 1)
$udpBest = ($ok | Where-Object { $_.transport -eq "UDP" } | Sort-Object goodput -Descending | Select-Object -First 1)

@"
# Streaming 1 MiB comparison

## TCP / P2pStream (best confirmed delivery)
$(if ($tcpBest) { "- goodput: $($tcpBest.goodput_mib_s) MiB/s`n- block: $($tcpBest.block_bytes) B, outstanding: $($tcpBest.outstanding), mode: $($tcpBest.mode)`n- last_rx_s: $($tcpBest.last_rx_s), peak_outstanding_bytes: $($tcpBest.peak_outstanding_bytes)" } else { "No successful TCP runs yet." })

## UDP / P2pSafeStream (best confirmed delivery)
$(if ($udpBest) { "- goodput: $($udpBest.goodput_mib_s) MiB/s`n- block: $($udpBest.block_bytes) B, outstanding: $($udpBest.outstanding), mode: $($udpBest.mode)`n- last_rx_s: $($udpBest.last_rx_s), udp_max_tx: $($udpBest.udp_max_tx), violations: tx=$($udpBest.udp_tx_violations) rx=$($udpBest.udp_rx_violations)" } else { "No successful UDP runs yet." })

Generated from ``$(Split-Path $BenchOut -Leaf)``.
"@ | Set-Content -Encoding utf8 $md

Write-Output "Wrote $best and $md"
