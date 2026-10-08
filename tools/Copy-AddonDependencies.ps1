#requires -Version 7.0
# Freeze every declared external dependency into a GUID-named snapshot for one native run.
# Roots are searched in order (-SearchRoots, or -InstalledAddonsRoot alone), and within a root
# the configured directory names in order. The first existing candidate wins; a candidate whose
# project GUID differs stops the build. The dependencies of those dependencies (their project files)
# are frozen too, found by project GUID in the same roots. The chosen sources are recorded in
# <Destination>.json.
param([Parameter(Mandatory)][string]$Destination, [string]$InstalledAddonsRoot, [string[]]$SearchRoots = @(), [switch]$Companion)
$ErrorActionPreference = 'Stop'
$settings = & "$PSScriptRoot/Get-ProjectConfig.ps1"
$entry = $settings.addon
$sources = [ordered]@{}
$origins = @{}
if ($Companion) {
 $entry = $settings.companion
 if (!$entry) { throw 'No companion is configured.' }
 $built = Get-Content -LiteralPath (Join-Path $settings.repo '.local/last-build.json') -Raw | ConvertFrom-Json
 if (!$built.compiled -or !(Test-Path -LiteralPath (Join-Path $built.installedAddon 'resourceDatabase.rdb'))) { throw 'Build the core addon before the companion.' }
 $sources[$settings.addon.id] = $built.installedAddon
 $origins[$settings.addon.id] = 'last-build'
}
$roots = @(@($SearchRoots) | Where-Object { $_ } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if (!$roots.Count -and $InstalledAddonsRoot) { $roots = @($InstalledAddonsRoot) }
function Test-AddonIdentity([string]$Path, [string]$Id) {
 $projects = @(Get-ChildItem -LiteralPath $Path -Filter '*.gproj' -File)
 return $projects.Count -eq 1 -and (Get-Content -LiteralPath $projects[0].FullName -Raw) -cmatch ('\bGUID\s+"?' + $Id + '\b')
}
if ($entry.installedDependencies) {
 foreach ($id in $entry.installedDependencies.Keys) {
  $found = $null
  foreach ($root in $roots) {
   foreach ($name in @($entry.installedDependencies[$id])) {
    $candidate = Join-Path $root $name
    if (!(Test-Path -LiteralPath $candidate -PathType Container)) { continue }
    if (!(Test-AddonIdentity $candidate $id)) { throw "Install the configured dependency with GUID $id before building: $candidate has a different project identity." }
    $found = $candidate
    break
   }
   if ($found) { break }
  }
  if (!$found) { throw "Install the configured dependency with GUID $id before building (looked for $(@($entry.installedDependencies[$id]) -join ', ') in: $($roots -join '; '))." }
  $sources[$id] = $found
  $origins[$id] = 'search-root'
 }
 # A dependency's own dependencies must be in the same -addonsDir snapshot, or the engine cannot load it
 # (EXPBG GM Tools 0.1.16+ depends on EXPBG Audio Data 198987BE7BAC4C84). They are found by project GUID
 # in the same roots, in root order, and recorded with origin "dependency of <GUID>".
 if (!$Companion) {
  $baseGame = '58D0FB3206B6F859'
  $pending = [Collections.Generic.Queue[string]]::new()
  foreach ($id in @($sources.Keys)) { $pending.Enqueue($id) }
  while ($pending.Count) {
   $owner = $pending.Dequeue()
   $ownerProject = @(Get-ChildItem -LiteralPath $sources[$owner] -Filter '*.gproj' -File)[0]
   $needs = @([regex]::Matches([regex]::Match((Get-Content -LiteralPath $ownerProject.FullName -Raw), '(?s)Dependencies\s*\{([^}]*)\}').Groups[1].Value, '[A-Fa-f0-9]{16}') | ForEach-Object { $_.Value.ToUpperInvariant() })
   foreach ($id in $needs) {
    if ($id -ceq $baseGame -or $id -ceq $entry.id -or $sources.Contains($id)) { continue }
    $found = $null
    foreach ($root in $roots) {
     if (!(Test-Path -LiteralPath $root -PathType Container)) { continue }
     $found = @(Get-ChildItem -LiteralPath $root -Directory | Sort-Object Name | Where-Object { Test-AddonIdentity $_.FullName $id } | Select-Object -First 1 | ForEach-Object FullName)[0]
     if ($found) { break }
    }
    if (!$found) { throw "Dependency $owner ($($sources[$owner])) needs addon $id, which is in none of: $($roots -join '; '). Install it first (EXPBG GM Tools 0.1.16+ needs EXPBG Audio Data 198987BE7BAC4C84: subscribe to it, or build its mod-audio-data folder with ./build.ps1 -NonInteractive -Audio in mod-ambient-radio)." }
    $sources[$id] = $found
    $origins[$id] = "dependency of $owner"
    $pending.Enqueue($id)
   }
  }
 }
}
if (Test-Path -LiteralPath $Destination) { throw 'Dependency snapshot already exists; use a new run name.' }
New-Item -ItemType Directory -Path $Destination | Out-Null
$record = @(foreach ($id in $sources.Keys) {
 $source = $sources[$id]
 if (!(Test-AddonIdentity $source $id)) { throw "Install the configured dependency with GUID $id before building." }
 # GUID-named snapshots avoid collisions between unrelated installed directory names.
 Copy-Item -LiteralPath $source -Destination (Join-Path $Destination $id) -Recurse
 $version = $null
 $serverData = Join-Path $source 'ServerData.json'
 if (Test-Path -LiteralPath $serverData -PathType Leaf) { try { $version = (Get-Content -LiteralPath $serverData -Raw | ConvertFrom-Json).revision.version } catch { $version = $null } }
 $pak = Join-Path $source 'data.pak'
 [ordered]@{
  id = $id; origin = $origins[$id]; source = $source; workshopVersion = $version
  packed = Test-Path -LiteralPath $pak -PathType Leaf
  dataPakSHA256 = if (Test-Path -LiteralPath $pak -PathType Leaf) { (Get-FileHash -LiteralPath $pak).Hash } else { $null }
  projectSHA256 = (Get-FileHash -LiteralPath (Get-ChildItem -LiteralPath $source -Filter '*.gproj' -File)[0].FullName).Hash
 }
})
# Kept beside the snapshot, never inside the scanned addons directory.
ConvertTo-Json -InputObject $record -Depth 4 | Set-Content -LiteralPath ([IO.Path]::GetFullPath($Destination).TrimEnd('\','/') + '.json') -Encoding utf8
