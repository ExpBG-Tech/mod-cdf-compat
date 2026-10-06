#requires -Version 7.0
param([string]$Repo = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath($Repo)
$config = Get-Content -LiteralPath (Join-Path $repoPath 'tools/project.json') -Raw | ConvertFrom-Json -AsHashtable
$asset = Get-Content -LiteralPath (Join-Path $repoPath 'tools/workshop-asset.json') -Raw | ConvertFrom-Json -AsHashtable
function Assert-RelativePath([string]$Path) {
 if (!$Path -or $Path -notmatch '^[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$' -or @($Path.Split('/') | Where-Object { $_ -in '.', '..' }).Count) { throw "Expected a repository-relative path: $Path" }
}
if ($config -isnot [System.Collections.IDictionary] -or $config.addon -isnot [System.Collections.IDictionary]) { throw 'Project configuration requires an addon object.' }
if ($asset.id -cnotmatch '^[A-F0-9]{16}$' -or !$asset.name -or $asset.unlisted -isnot [bool] -or $asset.private -isnot [bool]) { throw 'Workshop metadata requires an item GUID, name and explicit visibility.' }
# The Publish form refuses a summary over 200 characters.
if (([string]$asset.summary).Length -gt 200) { throw "Workshop summary is $(([string]$asset.summary).Length) characters; the Workshop allows at most 200." }
$config.addon.id = $asset.id
# A pack assembles its module folders (tools/pack.json) into one engine project.
$config.addon.assembled = Test-Path -LiteralPath (Join-Path $repoPath 'tools/pack.json') -PathType Leaf
$entries = @($config.addon)
if ($config.companion) { $entries += $config.companion }
$ids = @{}
$names = @{}
foreach ($entry in $entries) {
 if ($entry.name -cnotmatch '^[A-Za-z][A-Za-z0-9_-]*$' -or $entry.id -cnotmatch '^[A-F0-9]{16}$') { throw 'Each addon requires a safe name and project GUID.' }
 if ($names.ContainsKey($entry.name) -or $ids.ContainsKey($entry.id)) { throw 'Addon names and project GUIDs must be distinct.' }
 $names[$entry.name] = $true; $ids[$entry.id] = $true
 Assert-RelativePath $entry.source
 $entry.sourcePath = Join-Path $repoPath $entry.source
 $entry.project = $entry.name + '.gproj'
 if ($entry.dependencies -isnot [array] -or !$entry.dependencies.Count -or @($entry.dependencies | Where-Object { $_ -cnotmatch '^[A-F0-9]{16}$' -or $_ -ceq $entry.id }).Count -or @($entry.dependencies | Select-Object -Unique).Count -ne $entry.dependencies.Count) { throw 'Dependencies must be distinct GUIDs and cannot include the addon itself.' }
 if ($entry.runtimePaths -isnot [array] -or !$entry.runtimePaths.Count) { throw 'Each addon requires an explicit runtime path allowlist.' }
 foreach ($path in $entry.runtimePaths) {
  Assert-RelativePath $path
  if ($path -match '(?i)(^|/)(docs?|tests?|tools|logs?|profile|evidence|\.local|build|artifacts)(/|$)') { throw 'Development content cannot be a runtime path.' }
 }
 if ($entry.installedDependencies) {
  if ($entry.installedDependencies -isnot [System.Collections.IDictionary]) { throw 'Installed dependencies must map GUIDs to installed directory names.' }
  foreach ($id in $entry.installedDependencies.Keys) {
   # One directory name, or several candidates in search order (Workshop download, local build).
   $directoryNames = @($entry.installedDependencies[$id])
   if ($id -cnotin $entry.dependencies -or !$directoryNames.Count -or @($directoryNames | Where-Object { $_ -isnot [string] -or $_ -notmatch '^[A-Za-z0-9_-]+$' }).Count) { throw 'Installed dependencies require a declared GUID and one or more directory names.' }
  }
 }
}
Assert-RelativePath $config.addon.preview
# The preview path is relative to the engine project; in a pack it lives in one module folder.
$config.addon.previewPath = Join-Path $config.addon.sourcePath $config.addon.preview
if ($config.addon.assembled) {
 $found = @(Get-ChildItem -LiteralPath $config.addon.sourcePath -Directory | ForEach-Object { Join-Path $_.FullName $config.addon.preview } | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
 if ($found.Count -eq 1) { $config.addon.previewPath = $found[0] }
}
if ($config.companion -and ($config.companion.source -eq $config.addon.source -or $config.companion.id -ceq $config.addon.id)) { throw 'The companion must have its own source and identity.' }
if ($config.legacyPublishWorkflows -isnot [array] -or @($config.legacyPublishWorkflows | Where-Object { $_ -notmatch '^[A-Za-z0-9_-]+\.ya?ml$' }).Count) { throw 'Legacy workflows must be an explicit array of workflow filenames (empty for new repositories).' }
$config.asset = $asset
$config.repo = $repoPath
[pscustomobject]$config
