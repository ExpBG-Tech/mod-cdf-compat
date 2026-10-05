#requires -Version 7.0
param([string]$Repo = (Split-Path -Parent $PSScriptRoot), [switch]$Companion)
$ErrorActionPreference = 'Stop'
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1" -Repo $Repo
$versionFile = 'VERSION'
$changelogFile = 'CHANGELOG.md'
$assetFile = 'tools/workshop-asset.json'
$tagPrefix = 'v'
if ($Companion) {
 if (!$settings.companion) { throw 'No companion is configured.' }
 $settings.addon = $settings.companion
 $versionFile = 'CDF_VERSION'
 $changelogFile = 'CDF_CHANGELOG.md'
 $assetFile = 'tools/workshop-cdf-asset.json'
 $tagPrefix = 'cdf-v'
 $settings.asset = Get-Content -LiteralPath (Join-Path $Repo $assetFile) -Raw | ConvertFrom-Json -AsHashtable
 if ($settings.asset.id -cne $settings.addon.id -or !$settings.asset.name -or $settings.asset.unlisted -isnot [bool] -or $settings.asset.private -isnot [bool]) { throw 'Companion publication metadata must match its own identity and explicit visibility.' }
 if (!$settings.addon.preview -or $settings.addon.preview -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -or @($settings.addon.preview.Split('/') | Where-Object { $_ -in '.', '..' }).Count) { throw 'Companion preview must be a safe addon-relative path.' }
 if (!(Test-Path -LiteralPath (Join-Path $settings.addon.sourcePath $settings.addon.preview) -PathType Leaf)) { throw 'Companion preview is missing.' }
}
$settings | Add-Member -NotePropertyMembers @{versionFile=$versionFile;changelogFile=$changelogFile;assetFile=$assetFile;tagPrefix=$tagPrefix}
$settings
