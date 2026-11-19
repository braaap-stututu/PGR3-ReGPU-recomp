# Phase 2 pipeline: default.xex -> out/ppc (ReXenon, the only recompiler of this port) plus the checks.
#   & "<project>\tools\recomp.ps1" [-Prepare] [-Base]
# 1. checks the SHA-256 of game\default.xex (Europe disc) and game\default.xexp (TU #5);
# 2. xexpatch -> out\tu5\default_tu5.xex (the image every later step reads);
#    -Base skips this and uses the disc's XEX as is (the default target: see NOTES.md "TU5 mismatch");
# 3. xexdump  -> out\pgr3.bin + out\pgr3.sections;
# 4. -Prepare: XenonAnalyse -> config\pgr3_switch_tables.toml (review the git diff afterwards);
# 5. ReXenon config\pgr3.toml -> out\ppc (log: out\recomp.log; it stops with an error if anything is unresolved);
# 6. tools\pgr3_check.py -> out\check.txt.
# Needs the host tools (tools\setup.ps1).
param([switch]$Prepare, [switch]$Base)
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$shaXex  = '26FB114109F0FA997497C6836385B8AF391DB253D75C08D78D12F4899475A78A'
$shaXexp = (Get-Content -ErrorAction SilentlyContinue config\tu5.sha256)
$tools = Join-Path $root 'build\tools'
$analyse = Join-Path $tools 'rexenon\XenonAnalyse\XenonAnalyse.exe'
$recomp = Join-Path $tools 'rexenon\XenonRecomp\XenonRecomp.exe'
# Windows PowerShell's > writes UTF-16; redirected native commands run through cmd (plain bytes).
function Invoke-To([string]$out, [string]$cmdline) { & cmd /c "$cmdline > `"$out`" 2>&1"; return $LASTEXITCODE }

New-Item -ItemType Directory -Force out, out\tu5 | Out-Null
if (-not (Test-Path game\default.xex)) { throw 'game\default.xex is missing (see NOTES.md: Game files)' }
if ((Get-FileHash game\default.xex -Algorithm SHA256).Hash -ne $shaXex) { throw 'game\default.xex: unsupported version (expected the Europe disc, SHA-256 in NOTES.md)' }
foreach ($t in "$tools\xexpatch.exe", "$tools\xexdump.exe", $analyse, $recomp) {
    if (-not (Test-Path $t)) { throw "$t is missing: run tools\setup.ps1" }
}

if ($Base) {
    Copy-Item game\default.xex out\tu5\default_tu5.xex -Force
} else {
    if (-not (Test-Path game\default.xexp)) { throw 'game\default.xexp is missing' }
    if ($shaXexp -and (Get-FileHash game\default.xexp -Algorithm SHA256).Hash -ne $shaXexp.Trim()) { throw 'game\default.xexp: not Title Update #5' }
    if ((Invoke-To out\tu5\xexpatch.log "`"$tools\xexpatch.exe`" game\default.xex game\default.xexp out\tu5\default_tu5.xex") -ne 0) {
        Get-Content out\tu5\xexpatch.log; throw 'applying Title Update #5 failed (it does not fit the Europe disc XEX: use -Base)'
    }
}
if ((Invoke-To out\pgr3.sections "`"$tools\xexdump.exe`" out\tu5\default_tu5.xex out\pgr3.bin") -ne 0) { throw 'xexdump failed' }
Get-Content out\pgr3.sections

if ($Prepare) {
    [void](Invoke-To out\analyse.log "`"$analyse`" out\tu5\default_tu5.xex config\pgr3_switch_tables.toml")
    Get-Content out\analyse.log | Select-Object -Last 5
    # Tables XenonAnalyse misses, kept by hand (config\pgr3_switch_tables_extra.toml).
    Add-Content -Encoding ascii config\pgr3_switch_tables.toml ("`n" + (Get-Content -Raw config\pgr3_switch_tables_extra.toml))
}

if (Test-Path out\ppc) { Remove-Item -Recurse -Force out\ppc }
New-Item -ItemType Directory out\ppc | Out-Null
if ((Invoke-To out\recomp.log "`"$recomp`" config\pgr3.toml thirdparty\ReXenon\XenonUtils\ppc_context.h") -ne 0) {
    Get-Content out\recomp.log -Tail 30; throw 'ReXenon failed (out\recomp.log)'
}
Get-Content out\recomp.log | Where-Object { $_ -notmatch '^Recompiling|^Merged ' }

[void](Invoke-To out\check.txt "py -3 -I tools\pgr3_check.py out\pgr3.bin out\pgr3.sections out\ppc")
Get-Content out\check.txt | Select-String -Pattern '^\d+ functions|^pointer targets'
