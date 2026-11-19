# AI-driven benchmark race and test grid (PGR3 session 3).
#
#   . .\tools\benchmark.ps1
#   Invoke-Pgr3Benchmark [-Label name] [-Seconds 90] [-Settle 10] [-Env @{REGPU_X='1'}]
#   Start-Pgr3Grid [-Env @{...}]          # the frozen grid, game left running (Pgr3 "env PGR3_FREEZE_TIME 0" lets go)
#
# Both take the grid route (src/hooks/grid_test.cpp): A/START from boot into the route profile's first race (London,
# 6 cars, Honda NSX, 3 laps). The benchmark has the race AI drive the player's car (src/hooks/ai_player.cpp) and the
# light performance log measure Seconds seconds from Settle seconds after GO; the game closes its window afterwards.
# - Never on the user's profile: runs use out\user-route, and its profile is put back from out\bench\baseline before
#   each run (a finished race would move the career on and change the route).
# - Results: out\bench\<label>-<time>\ (result.json, result.txt, pgr3.log); one line per run in out\bench\runs.csv.
# - The AI varies from run to run: compare several runs per variant, interleaved.
$BenchRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $BenchRoot 'tools\pgr3ctl.ps1')

function Restore-Pgr3RouteProfile {
  $profiles = Join-Path $BenchRoot 'out\user-route\profiles'
  $baseline = Join-Path $BenchRoot 'out\bench\baseline'
  if (-not (Test-Path $baseline)) {
    New-Item -ItemType Directory -Force $baseline | Out-Null
    Copy-Item -Recurse (Join-Path $profiles '*') $baseline
    "benchmark: saved the route profile as the baseline ($baseline)"
  }
  Remove-Item -Recurse -Force (Join-Path $profiles '*')
  Copy-Item -Recurse (Join-Path $baseline '*') $profiles
}

function Wait-Pgr3Closed([int]$Seconds) {
  $p = Get-Process pgr3 -ErrorAction SilentlyContinue
  if (-not $p) { return $true }
  return $p.WaitForExit($Seconds * 1000)
}

function Close-Pgr3 {
  $p = Get-Process pgr3 -ErrorAction SilentlyContinue
  if ($p) { [void]$p.CloseMainWindow(); if (-not $p.WaitForExit(15000)) { Write-Warning 'pgr3 did not close' } }
}

function Start-Pgr3Grid([hashtable]$Env = @{}) {
  Close-Pgr3
  Restore-Pgr3RouteProfile
  $vars = @{ PGR3_GRID_TEST = '1' }
  foreach ($k in $Env.Keys) { $vars[$k] = $Env[$k] }
  Start-Pgr3Dev -Env $vars
}

function Invoke-Pgr3Benchmark([string]$Label = 'run', [int]$Seconds = 90, [int]$Settle = 10, [hashtable]$Env = @{}) {
  Close-Pgr3
  Restore-Pgr3RouteProfile
  $vars = @{ PGR3_BENCHMARK = "$Seconds"; PGR3_BENCHMARK_SETTLE = "$Settle" }
  foreach ($k in $Env.Keys) { $vars[$k] = $Env[$k] }
  Start-Pgr3Dev -Env $vars | Out-Null
  # route ~15 s + countdown + settle + window; the game closes itself at the end
  if (-not (Wait-Pgr3Closed ($Settle + $Seconds + 150))) { Write-Warning 'benchmark: the game did not close by itself'; Close-Pgr3 }
  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $dir = Join-Path $BenchRoot "out\bench\$Label-$stamp"
  New-Item -ItemType Directory -Force $dir | Out-Null
  Copy-Item (Join-Path $BenchRoot 'out\run\pgr3.log') (Join-Path $dir 'pgr3.log')
  $summary = & python -I (Join-Path $BenchRoot 'tools\bench_summary.py') (Join-Path $dir 'pgr3.log') (Join-Path $dir 'result.json')
  $summary | Set-Content (Join-Path $dir 'result.txt') -Encoding ascii
  $r = Get-Content (Join-Path $dir 'result.json') -Raw | ConvertFrom-Json
  $csv = Join-Path $BenchRoot 'out\bench\runs.csv'
  if (-not (Test-Path $csv)) { 'time,label,env,seconds,fps_avg,fps_lowest_second,p99_ms,worst_ms,draws,gpu_ms,draw_busy' | Set-Content $csv -Encoding ascii }
  $envText = ($Env.Keys | ForEach-Object { "$_=$($Env[$_])" }) -join ' '
  if ($r.ok) {
    "$stamp,$Label,$envText,$($r.seconds),$($r.fps_avg),$($r.fps_lowest_second),$($r.frame_ms_p99_median),$($r.frame_ms_worst),$($r.draws_avg),$($r.gpu_ms_avg),$($r.draw_thread_busy_avg)" | Add-Content $csv -Encoding ascii
  }
  $summary
  "results: $dir"
}
