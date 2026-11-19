# Live A/B on the frozen grid (PGR3 session 3).
#
#   . .\tools\ab.ps1
#   Start-Pgr3AbGrid [-Env @{...}]                   # uncapped (360, vsync off, vblank default 4000) frozen grid, minimized
#   Invoke-Pgr3AB REGPU_NAME [-Rounds 4] [-Seconds 5] [-Settle 3]      # live ReGPU switch 0 vs 1, interleaved
#   Invoke-Pgr3AB PGR3_NAME -Env                     # a PGR3_* live switch (debug server "env")
#   Measure-Pgr3Fps [-Seconds 5] [-Settle 3]         # average fps of the perf lite lines over the next N seconds
#
# The grid is frozen (src/hooks/frame_time.cpp), so each second draws the same frame: differences are the change.
# Needs the light performance log (REGPU_PERF_LITE=1, set by Start-Pgr3AbGrid).
$AbRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $AbRoot 'tools\benchmark.ps1')
$AbLog = Join-Path $AbRoot 'out\run\pgr3.log'

function Get-Pgr3PerfLines {
  $fs = [System.IO.File]::Open($AbLog, 'Open', 'Read', 'ReadWrite')
  try { $sr = New-Object System.IO.StreamReader($fs); $text = $sr.ReadToEnd() } finally { $fs.Close() }
  $text -split "`n" | Where-Object { $_ -match 'perf lite: ([\d.]+) fps, ([\d.]+) ms/frame.*GPU ([\d.]+) ms/frame \| draw thread (\d+)%' }
}

function Measure-Pgr3Fps([int]$Seconds = 5, [int]$Settle = 3) {
  Start-Sleep $Settle
  $before = @(Get-Pgr3PerfLines).Count
  Start-Sleep ($Seconds + 0.3)
  $lines = @(Get-Pgr3PerfLines) | Select-Object -Skip $before
  $fps = @(); $gpu = @(); $busy = @()
  foreach ($l in $lines) {
    if ($l -match 'perf lite: ([\d.]+) fps, ([\d.]+) ms/frame.*GPU ([\d.]+) ms/frame \| draw thread (\d+)%') {
      $fps += [double]$Matches[1]; $gpu += [double]$Matches[3]; $busy += [double]$Matches[4]
    }
  }
  if (-not $fps.Count) { return [pscustomobject]@{ fps = 0; gpu = 0; busy = 0; n = 0 } }
  [pscustomobject]@{
    fps = [math]::Round(($fps | Measure-Object -Average).Average, 1)
    gpu = [math]::Round(($gpu | Measure-Object -Average).Average, 2)
    busy = [math]::Round(($busy | Measure-Object -Average).Average, 0)
    n = $fps.Count
  }
}

function Start-Pgr3AbGrid([hashtable]$Env = @{}) {
  $vars = @{ PGR3_MAX_FPS = '360'; PGR3_VSYNC = '0'; REGPU_PERF_LITE = '1' } # (vblank: the uncapped default, 4000 Hz)
  foreach ($k in $Env.Keys) { $vars[$k] = $Env[$k] }
  # PGR3_AB_EXE=<path>: boot that exe instead of build\rel\pgr3.exe (a copy of an older build with its DLLs, kept
  # in its own folder so the process is still named pgr3), for a boot A/B of a code change without a switch.
  $exe = Join-Path $AbRoot 'build\rel\pgr3.exe'
  if ($vars.ContainsKey('PGR3_AB_EXE')) { $exe = $vars['PGR3_AB_EXE']; $vars.Remove('PGR3_AB_EXE') }
  $script:Pgr3DevExe = $exe
  Start-Pgr3Grid -Env $vars | Out-Null
  for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep 1
    if (Select-String -Path $AbLog -Pattern 'grid test: frozen' -Quiet) { Start-Sleep 4; return 'grid frozen' }
  }
  throw 'the grid did not freeze in 90 s'
}

function Set-Pgr3Switch([string]$Name, [string]$Value, [switch]$Env) {
  if ($Env) { Pgr3 "env $Name $Value" | Out-Null } else { Pgr3 "switch $Name $Value" | Out-Null }
}

function Invoke-Pgr3AB([string]$Name, [int]$Rounds = 4, [int]$Seconds = 5, [int]$Settle = 3, [switch]$Env,
                       [string]$Off = '0', [string]$On = '1') {
  $a = @(); $b = @()
  for ($r = 0; $r -lt $Rounds; $r++) {
    # alternate which goes first each round
    $order = if ($r % 2) { @($On, $Off) } else { @($Off, $On) }
    foreach ($v in $order) {
      Set-Pgr3Switch $Name $v -Env:$Env
      $m = Measure-Pgr3Fps $Seconds $Settle
      if ($v -eq $Off) { $a += $m } else { $b += $m }
    }
  }
  Set-Pgr3Switch $Name $Off -Env:$Env
  $fa = ($a.fps | Measure-Object -Average).Average; $fb = ($b.fps | Measure-Object -Average).Average
  $wins = 0; for ($i = 0; $i -lt $Rounds; $i++) { if ($b[$i].fps -gt $a[$i].fps) { $wins++ } }
  [pscustomobject]@{
    name = $Name
    off = "$([math]::Round($fa,1)) fps (" + (($a.fps) -join ' ') + ")"
    on = "$([math]::Round($fb,1)) fps (" + (($b.fps) -join ' ') + ")"
    change = "{0:+0.0;-0.0}%" -f (100 * ($fb - $fa) / [math]::Max($fa, 0.001))
    on_wins = "$wins/$Rounds"
    gpu_ms = "$([math]::Round(($a.gpu | Measure-Object -Average).Average,2)) -> $([math]::Round(($b.gpu | Measure-Object -Average).Average,2))"
    draw_busy = "$([math]::Round(($a.busy | Measure-Object -Average).Average,0)) -> $([math]::Round(($b.busy | Measure-Object -Average).Average,0))"
  }
}

# Boot-time A/B: each variant is its own boot of the frozen grid (interleaved, Rounds rounds), measured Seconds
# seconds once frozen. $Variants: ordered hashtable name -> env hashtable, e.g.
#   Invoke-Pgr3BootAB ([ordered]@{ base = @{}; submit = @{ REGPU_SUBMIT_DRAWS = '512' } }) -Base @{...}
function Invoke-Pgr3BootAB($Variants, [hashtable]$Base = @{}, [int]$Rounds = 2, [int]$Seconds = 8) {
  $results = [ordered]@{}
  foreach ($k in $Variants.Keys) { $results[$k] = @() }
  for ($r = 0; $r -lt $Rounds; $r++) {
    $keys = @($Variants.Keys); if ($r % 2) { [array]::Reverse($keys) }
    foreach ($k in $keys) {
      $envv = @{}; foreach ($b in $Base.Keys) { $envv[$b] = $Base[$b] }
      foreach ($e in $Variants[$k].Keys) { $envv[$e] = $Variants[$k][$e] }
      Start-Pgr3AbGrid -Env $envv | Out-Null
      Start-Sleep 4
      $results[$k] += Measure-Pgr3Fps $Seconds 2
      Close-Pgr3
    }
  }
  foreach ($k in $results.Keys) {
    $f = $results[$k]
    [pscustomobject]@{ variant = $k; fps = [math]::Round(($f.fps | Measure-Object -Average).Average, 1); rounds = ($f.fps -join ' ');
      gpu = [math]::Round(($f.gpu | Measure-Object -Average).Average, 2); busy = [math]::Round(($f.busy | Measure-Object -Average).Average, 0) }
  }
}
