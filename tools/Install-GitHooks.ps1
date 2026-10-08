#requires -Version 7.0
# Points this clone's Git hooks at the tracked .githooks folder (once per clone; idempotent).
# .githooks/pre-push starts tools/Invoke-Cleanup.ps1 in the background after a push of main/master.
param([string]$RepoRoot = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath (Join-Path $RepoRoot '.githooks/pre-push') -PathType Leaf)) { throw 'No .githooks/pre-push in this repository.' }
$current = & git -C $RepoRoot config --local --get core.hooksPath
if ($current -and $current -ne '.githooks') { throw "core.hooksPath is already '$current'; merge the hooks by hand." }
& git -C $RepoRoot config --local core.hooksPath .githooks
if ($LASTEXITCODE -ne 0) { throw 'git config failed.' }
'Git hooks enabled: core.hooksPath = .githooks (pre-push: background cleanup after pushing main/master).'
