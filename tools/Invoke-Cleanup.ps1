#requires -Version 7.0
<#
.SYNOPSIS
Frees disk space in this checkout after a release or a push of main/master.

.DESCRIPTION
Shared EXPBG tooling (identical in every EXPBG mod repository). It runs:
- after every verified release (release.ps1 passes -PublishedRun), and
- after every push of main or master (.githooks/pre-push starts it in the background; enable the
  hook once per clone with tools/Install-GitHooks.ps1).

It calls tools/Invoke-ReleaseHousekeeping.ps1 -Delete, which permanently deletes the heavy payloads
of build/<run>, artifacts/<run> and .local/workshop-local-<utc> folders (addon snapshots, packed
bundles, data.pak, zips, files of 1 MB or more) and keeps:
- every receipt, manifest, change note, result and log (the release guards read them);
- the newest -KeepBuilds build/local-* snapshot(s) and the build named in .local/last-build.json
  (the native fixture runners and tools/art/Import-PackArt.ps1 start from it);
- the newest -KeepRuns run of every other family, and anything modified within -MinAgeHours
  (a published release with a verified receipt is slimmed whatever its age);
- everything else in .local (config.json, e2e profiles, art sources, notes) except leftover
  portable-test-* / pack-assembly-test-* folders older than an hour;
- only the newest local install backup of each addon (the .<addon>-local-backups folders beside
  BuildAddonsDirectory that tools/Install-LocalAddon.ps1 fills on every build).
Nothing runs while Arma Reforger, its server or Workbench is open (a build, fixture or the
publishing form may be using these folders), or while a release of this repository holds its mutex.

.EXAMPLE
./tools/Invoke-Cleanup.ps1 -WhatIf
.EXAMPLE
./tools/Invoke-Cleanup.ps1 -LogPath .local/cleanup.log
#>
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'Low')]
param(
    [ValidatePattern('^workshop-local-\d{8}-\d{6}-\d{3}$')][string]$PublishedRun,
    [ValidateRange(0, 1000)][int]$KeepBuilds = 1,
    [ValidateRange(0, 1000)][int]$KeepRuns = 1,
    [ValidateRange(0, 87600)][double]$MinAgeHours = 0.5,
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot),
    # Append a transcript (used by the background run of the pre-push hook).
    [string]$LogPath = '',
    # Portable test seam: run even while an engine is open (fake repositories only).
    [Parameter(DontShow)][switch]$TestIgnoreEngines
)
$ErrorActionPreference = 'Stop'
$transcript = $false
if ($LogPath) {
    $log = if ([IO.Path]::IsPathRooted($LogPath)) { $LogPath } else { Join-Path $RepoRoot $LogPath }
    New-Item -ItemType Directory -Path (Split-Path -Parent $log) -Force | Out-Null
    # Keep the log small: start over once it passes 1 MB.
    if ((Test-Path -LiteralPath $log) -and (Get-Item -LiteralPath $log).Length -gt 1MB) { Remove-Item -LiteralPath $log -Force }
    Start-Transcript -LiteralPath $log -Append | Out-Null
    $transcript = $true
}
try {
    "EXPBG cleanup of $RepoRoot at $([DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss'))" | Write-Host
    if (!$TestIgnoreEngines -and (Get-Process -Name ArmaReforgerSteam, ArmaReforgerSteamDiag, ArmaReforgerServer, ArmaReforgerServerDiag, ArmaReforgerWorkbenchSteam, ArmaReforgerWorkbenchSteamDiag -ErrorAction SilentlyContinue)) {
        'Cleanup skipped: Arma Reforger, its server or Workbench is running.' | Write-Host
        return
    }
    $arguments = @{ RepoRoot = $RepoRoot; Delete = $true; KeepReleases = 0; KeepBuilds = $KeepBuilds; KeepRuns = $KeepRuns; MinAgeHours = $MinAgeHours; Confirm = $false }
    if ($PublishedRun) { $arguments.PublishedRun = $PublishedRun }
    & "$PSScriptRoot/Invoke-ReleaseHousekeeping.ps1" @arguments
    # Local install backups (tools/Install-LocalAddon.ps1 keeps the replaced addon on every build): only
    # the newest backup of each addon stays beside BuildAddonsDirectory; never links, never in-progress ones.
    $buildAddons = $null
    try { $buildAddons = (& "$PSScriptRoot/Get-LocalConfig.ps1" -ConfigPath (Join-Path $RepoRoot '.local/config.json')).BuildAddonsDirectory } catch { }
    if ($buildAddons -and (Test-Path -LiteralPath $buildAddons -PathType Container)) {
        foreach ($backupRoot in Get-ChildItem -LiteralPath (Split-Path -Parent $buildAddons) -Directory -Force | Where-Object { $_.Name -match '^\..+-local-backups$' -and !($_.Attributes -band [IO.FileAttributes]::ReparsePoint) }) {
            $backups = @(Get-ChildItem -LiteralPath $backupRoot.FullName -Directory -Force | Where-Object { $_.Name -match '^\d{8}-\d{6}-\d{3}(-[0-9a-f]+)?$' } | Sort-Object Name -Descending)
            foreach ($old in @($backups | Select-Object -Skip 1)) {
                if ($old.Attributes -band [IO.FileAttributes]::ReparsePoint -or @(Get-ChildItem -LiteralPath $old.FullName -Recurse -Force -Attributes ReparsePoint).Count) { continue }
                if ($PSCmdlet.ShouldProcess("$($backupRoot.Name)/$($old.Name)", 'Delete old local install backup')) { Remove-Item -LiteralPath $old.FullName -Recurse -Force }
            }
        }
    }
    # Folders the portable tests leave in .local when interrupted; never links, never younger than an hour.
    $local = Join-Path $RepoRoot '.local'
    if (Test-Path -LiteralPath $local -PathType Container) {
        $cutoff = [DateTime]::UtcNow.AddHours(-1)
        foreach ($folder in Get-ChildItem -LiteralPath $local -Directory -Force | Where-Object { $_.Name -match '^(portable-test|pack-assembly-test)-[0-9a-f]{32}$' }) {
            if ($folder.Attributes -band [IO.FileAttributes]::ReparsePoint -or $folder.LastWriteTimeUtc -gt $cutoff) { continue }
            if (@(Get-ChildItem -LiteralPath $folder.FullName -Recurse -Force -Attributes ReparsePoint).Count) { continue }
            if ($PSCmdlet.ShouldProcess(".local/$($folder.Name)", 'Delete leftover test folder')) { Remove-Item -LiteralPath $folder.FullName -Recurse -Force }
        }
    }
} finally {
    if ($transcript) { Stop-Transcript | Out-Null }
}
