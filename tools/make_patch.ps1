# make_patch.ps1 -Name regpu-xxx [-Repo ReGPU]: writes patches/<Name>.patch = the working tree of thirdparty/<Repo>
# minus (pinned commit + every patch listed in tools/setup.ps1), built in a scratch index (CLAUDE.md "Patches").
# Then add the name to tools/setup.ps1 and patches/README.md and run tools/setup.ps1 -NoBuild to check the stack.
param([Parameter(Mandatory)][string]$Name, [string]$Repo = 'ReGPU')
$root = Split-Path -Parent $PSScriptRoot
$repoDir = Join-Path $root "thirdparty/$Repo"
$setup = Get-Content (Join-Path $root 'tools\setup.ps1') -Raw
$var = if ($Repo -eq 'ReGPU') { 'regpuPatches' } else { 'rexenonPatches' }
if ($setup -notmatch "(?s)\`$$var = @\((.*?)\n\)") { throw "no `$$var list in setup.ps1" }
$stack = [regex]::Matches($Matches[1], "(?m)^\s*'([^']+)'") | ForEach-Object { $_.Groups[1].Value }
$idx = Join-Path ([IO.Path]::GetTempPath()) ("pgr3-mkpatch-" + [guid]::NewGuid() + ".idx")
$env:GIT_INDEX_FILE = $idx
try {
  & git -C $repoDir read-tree HEAD
  foreach ($p in $stack) {
    & git -C $repoDir apply --cached (Join-Path $root "patches/$p.patch")
    if ($LASTEXITCODE -ne 0) { throw "patch $p does not apply" }
  }
  $out = Join-Path $root "patches/$Name.patch"
  # diff index (stack) -> working tree; LF output written as bytes
  $diff = & git -C $repoDir -c core.autocrlf=false diff --no-color --no-ext-diff
  $text = (($diff -join "`n") + "`n")
  [IO.File]::WriteAllBytes($out, [Text.Encoding]::UTF8.GetBytes($text))
  "wrote $out ($($diff.Count) lines) on top of $($stack.Count) patches"
} finally { Remove-Item Env:GIT_INDEX_FILE; Remove-Item $idx -ErrorAction SilentlyContinue }
