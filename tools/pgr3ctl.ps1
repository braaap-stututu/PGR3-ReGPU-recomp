<#
  pgr3ctl.ps1 - drive our own build (build\rel\pgr3.exe) through ReGPU's debug server.
  Ported from the FM3 project's fm3ctl.ps1; the protocol is ReGPU's (src/debug.cpp): one text command per line,
  one JSON reply per line. Port 7342 (REGPU_DEBUG_PORT), so it never collides with an FM3 build on 7341.

      . .\tools\pgr3ctl.ps1
      Start-Pgr3Dev [-Fresh] [-Env @{NAME='v'}]  # minimized, PGR3_USER_DIR=out\user-route, log in out\run
      Pgr3 "press A"                             # also: hold/release <A+B..>, stick l|r x y ms, trigger l|r v ms
      Save-Pgr3Shot boot.png                     # next frame as PNG in out\shots
      Pgr3 "status" ; Pgr3 "log 20" ; Pgr3 "capture"
      Stop-Pgr3Dev                               # closes the window (CloseMainWindow), kills only if it hangs

  Buttons: UP DOWN LEFT RIGHT START BACK LS RS LB RB A B X Y (join with +).
#>
$Pgr3Root    = Split-Path -Parent $PSScriptRoot
$Pgr3DevExe  = Join-Path $Pgr3Root "build\rel\pgr3.exe"
$Pgr3DevGame = Join-Path $Pgr3Root "game\disc"
$Pgr3DevRun  = Join-Path $Pgr3Root "out\run"
$Pgr3DevUser = Join-Path $Pgr3Root "out\user-route"   # scripted runs only; the user's runs use out\user
$Pgr3Shots   = Join-Path $Pgr3Root "out\shots"
$Pgr3Port    = 7342

function Start-Pgr3Dev([switch]$Fresh, [hashtable]$Env = @{}, [switch]$Visible) {
  New-Item -ItemType Directory -Force $Pgr3DevRun, $Pgr3DevUser, $Pgr3Shots | Out-Null
  if ($Fresh -and (Test-Path (Join-Path $Pgr3DevUser "profiles"))) { Remove-Item -Recurse -Force (Join-Path $Pgr3DevUser "profiles") }
  $saved = @{}
  $all = @{ PGR3_USER_DIR = $Pgr3DevUser; REGPU_DEBUG = "1"; REGPU_DEBUG_PORT = "$Pgr3Port" }
  if (-not $Visible) { $all.PGR3_MINIMIZED = "1" }
  foreach ($k in $Env.Keys) { $all[$k] = $Env[$k] }
  foreach ($k in $all.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); Set-Item "env:$k" $all[$k] }
  $script:Pgr3DevProc = Start-Process -FilePath $Pgr3DevExe -ArgumentList "`"$Pgr3DevGame`"" -WorkingDirectory $Pgr3DevRun -PassThru
  foreach ($k in $all.Keys) { if ($null -eq $saved[$k]) { Remove-Item "env:$k" -ErrorAction SilentlyContinue } else { Set-Item "env:$k" $saved[$k] } }
  for ($i = 0; $i -lt 150; $i++) {
    try { $r = Pgr3 "ping" 2000; if ($r -match '"ok":true') { return "pgr3 pid $($script:Pgr3DevProc.Id): debug server up on $Pgr3Port" } } catch { $script:Pgr3Sock = $null }
    Start-Sleep -Milliseconds 200
  }
  throw "debug server did not answer"
}

function Pgr3([string]$Command, [int]$TimeoutMs = 70000) {
  if (-not $script:Pgr3Sock -or -not $script:Pgr3Sock.Connected) {
    $script:Pgr3Sock = New-Object System.Net.Sockets.TcpClient
    $script:Pgr3Sock.Connect("127.0.0.1", $Pgr3Port)
    $s = $script:Pgr3Sock.GetStream()
    $script:Pgr3Writer = New-Object System.IO.StreamWriter($s); $script:Pgr3Writer.AutoFlush = $true
    $script:Pgr3Reader = New-Object System.IO.StreamReader($s)
  }
  $script:Pgr3Sock.ReceiveTimeout = $TimeoutMs
  $script:Pgr3Writer.Write("$Command`n")
  $script:Pgr3Reader.ReadLine()
}

function Save-Pgr3Shot([string]$Name) {
  $path = Join-Path $Pgr3Shots $Name
  $r = Pgr3 "shot $path"
  if ($r -match '"ok":true') { $path } else { "shot failed: $r" }
}

function Stop-Pgr3Dev {
  if ($script:Pgr3Sock) { try { $script:Pgr3Sock.Close() } catch {}; $script:Pgr3Sock = $null }
  if ($script:Pgr3DevProc -and -not $script:Pgr3DevProc.HasExited) {
    [void]$script:Pgr3DevProc.CloseMainWindow()
    if (-not $script:Pgr3DevProc.WaitForExit(8000)) { Stop-Process -Id $script:Pgr3DevProc.Id -Force; "killed (did not close)" } else { "closed" }
  }
}

# Probe-Pgr3 <ps hash> <expr> [x] [y]: makes that pixel shader return <expr> (HLSL over r[n], pc[n], oC0 ...), waits
# until the edit is compiled, takes a shot and returns the RGB at (x, y) (default: a building at 900,200 in a 1024x600
# race frame). The shader goes back to its original afterwards.
function Probe-Pgr3([string]$Hash, [string]$Expr, [int]$X = 900, [int]$Y = 200, [int[]]$More = @()) {
  Add-Type -AssemblyName System.Drawing
  $reply = Pgr3 "ps $Hash $Expr" 20000
  for ($k = 0; $k -lt 30 -and $reply -notmatch '"ready":true'; $k++) { Start-Sleep -Milliseconds 500; $reply = Pgr3 "ps $Hash $Expr" 20000 }
  Start-Sleep -Milliseconds 700
  $p = Save-Pgr3Shot "probe.png"
  Pgr3 "ps $Hash" 20000 | Out-Null
  if (-not (Test-Path $p)) { return "$Expr -> shot failed ($reply)" }
  $bmp = [System.Drawing.Bitmap]::FromFile($p)
  $c = $bmp.GetPixel($X, $Y); $out = "{0,-44} -> {1},{2},{3}" -f $Expr, $c.R, $c.G, $c.B
  for ($i = 0; $i + 1 -lt $More.Count; $i += 2) { $d = $bmp.GetPixel($More[$i], $More[$i + 1]); $out += "  | {0},{1},{2}" -f $d.R, $d.G, $d.B }
  $bmp.Dispose()
  if ($reply -notmatch '"ready":true') { $out += "  (not ready)" }
  $out
}
