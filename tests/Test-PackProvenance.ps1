#requires -Version 7.0
# Portable identity and provenance guards for the merged companions:
# - imported module files are byte-identical to the Git blobs recorded in tools/pack.json;
#   a file changed here records its imported blob under localChanges.originBlob;
# - the pack identity, dependencies and Workshop metadata are the reserved ones;
# - no runtime file still names a standalone parent or companion identity;
# - CDF save keys and the Garrison ledger bridge remain in place.
# When the source repositories are checked out beside this one (or EXPBG_SOURCES_ROOT names
# their parent directory), the recorded blobs are also checked against the pinned commits.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
$project = & "$repo/tools/Get-ProjectConfig.ps1"
$asset = $project.asset
$reserved = '07BC942D90324CD9'
$base = '58D0FB3206B6F859'; $gmTools = 'FC1402F65B2F4A45'; $cdf = '6A1876F37D65AB09'
$retired = @('F3B7C6FB18AB1F79','E110000000000001','E2A47D19C8B6503F','8C5A6D9E73B241F0','E110000000000002','D7A82F4139C60BE5')

# Identity and dependencies.
if ($project.addon.id -cne $reserved -or $asset.id -cne $reserved) { throw 'The pack must use the identity reserved by EXPBG GM Tools.' }
if ($project.addon.name -cne 'EXPBG_CDF_Compat' -or $pack.project -cne 'EXPBG_CDF_Compat.gproj') { throw 'Project name changed.' }
if (@(Compare-Object @($project.addon.dependencies) @($base,$gmTools,$cdf) -CaseSensitive).Count -or @($project.addon.dependencies).Count -ne 3) { throw 'Dependencies must be exactly base game, EXPBG GM Tools and CDF Game Master Save.' }
if ((@($project.addon.installedDependencies.Keys | Sort-Object) -join ',') -cne (@($cdf,$gmTools | Sort-Object) -join ',')) { throw 'Native builds must snapshot CDF and EXPBG GM Tools.' }
$gproj = Get-Content -LiteralPath "$repo/addon/$($pack.project)" -Raw
if ($gproj -cnotmatch ('\bGUID\s+"' + $reserved + '"') -or $gproj -cnotmatch 'TITLE\s+"EXPBG CDF Compat"') { throw 'Project file identity differs.' }
if ($pack.dependencies.gmTools.id -cne $gmTools -or $pack.dependencies.cdf.id -cne $cdf -or $pack.dependencies.gmTools.commit -cnotmatch '^([0-9a-f]{40}|PENDING)$') { throw 'Pinned dependency metadata is incomplete.' }
# PENDING: the GM Tools release this pack needs is not tagged yet; the orchestrator fills
# in the commit at release, and Test-DependencyOverlap runs against it then.
if ($pack.dependencies.gmTools.commit -ceq 'PENDING') { Write-Warning "EXPBG GM Tools $($pack.dependencies.gmTools.version) pin is PENDING: fill in its release commit before releasing this pack." }

# Workshop listing: Unlisted, the simplified Arma Public License (APL) like the other EXPBG script mods, short name, space-free tags.
if (!$asset.unlisted -or $asset.private -or $asset.license -cne 'Arma Public License (APL)') { throw 'Workshop visibility or license differs.' }
if ($asset.name -cne 'EXPBG CDF Compat' -or $asset.name.Length -gt 30) { throw 'Workshop name differs or exceeds 30 characters.' }
if (!@($asset.tags).Count -or @($asset.tags | Where-Object { $_ -cnotmatch '^[A-Z0-9]+$' }).Count) { throw 'Workshop tags must be space-free upper-case words.' }
# No version in the description (0.1.7): CLI updates keep the old listing text, so it would go stale.
if ($asset.description -match '\{\{VERSION\}\}|\bVersion \d' -or $asset.description -match 'Authors?:|K\.Edgar|M\.Pac') { throw 'Workshop description must carry no version and name nobody.' }
foreach ($id in @($gmTools,$cdf)) { if (!$asset.description.Contains($id)) { throw "Workshop description must name dependency $id." } }

# Module provenance: exact file inventory and Git blob identity (raw bytes, no filters).
$moduleNames = @($pack.modules | ForEach-Object { $_.name })
if ((@($moduleNames) -join ',') -cne 'unit-caching-cdf,intel-items-cdf,ambient-destruction-cdf,garrison-cdf,unit-dialog-cdf,ai-global-skills-cdf,ai-surrender-cdf,unit-scripts-cdf,inventory-cdf,vehicle-crew-cdf') { throw 'Module list changed.' }
$checked = 0
$sourcesRoot = $env:EXPBG_SOURCES_ROOT
if (!$sourcesRoot) { $sourcesRoot = Split-Path -Parent $repo }
$sourceChecks = 0; $sourceSkips = @()
foreach ($module in $pack.modules) {
 $folder = Join-Path $repo "addon/$($module.name)"
 $actual = @(Get-ChildItem -LiteralPath $folder -Recurse -File | ForEach-Object { $_.FullName.Substring($folder.Length + 1).Replace('\','/') } | Sort-Object)
 if (!$module.files) {
  if ($module.origin.repository -cne 'ExpBG-Tech/mod-cdf-compat') { throw "Imported module $($module.name) needs a recorded file inventory." }
  continue
 }
 if ($module.origin.commit -cnotmatch '^[0-9a-f]{40}$' -or !$module.origin.version -or $module.origin.workshopId -cnotin $retired -or $module.origin.formerParentId -cnotin $retired) { throw "Module $($module.name) lacks exact origin metadata." }
 $expected = @($module.files.Keys | Sort-Object)
 if (($actual -join '|') -cne ($expected -join '|')) { throw "Module $($module.name) files differ from the recorded inventory." }
 $repositoryName = ($module.origin.repository -split '/')[-1]
 $source = Join-Path $sourcesRoot $repositoryName
 $haveSource = (Test-Path -LiteralPath (Join-Path $source '.git')) -and $null -ne (& git -C $source rev-parse --verify --quiet "$($module.origin.commit)^{commit}" 2>$null)
 foreach ($relative in $expected) {
  $blob = (& git hash-object --no-filters -- (Join-Path $folder $relative)).Trim()
  if ($LASTEXITCODE -ne 0) { throw 'git hash-object failed.' }
  if ($blob -cne $module.files[$relative]) { throw "Imported bytes changed: addon/$($module.name)/$relative" }
  $checked++
  if ($haveSource) {
   $imported = $module.files[$relative]
   if ($module.localChanges -and $module.localChanges[$relative]) { $imported = $module.localChanges[$relative].originBlob }
   $original = (& git -C $source rev-parse --verify --quiet "$($module.origin.commit):$($module.origin.path)/$relative" 2>$null)
   if ($original -cne $imported) { throw "Recorded blob differs from $repositoryName@$($module.origin.commit): $relative" }
   $sourceChecks++
  }
 }
 if (!$haveSource) { $sourceSkips += $repositoryName }
}
$global:LASTEXITCODE = 0

# No runtime text still names a standalone parent/companion identity.
foreach ($file in Get-ChildItem -LiteralPath "$repo/addon" -Recurse -File | Where-Object Extension -in '.c','.conf','.et','.gproj','.layout','.meta','.st') {
 $text = Get-Content -LiteralPath $file.FullName -Raw
 foreach ($id in $retired) { if ($text.Contains($id)) { throw "Retired identity $id in $($file.FullName)" } }
}
foreach ($config in @('tools/project.json','tools/workshop-asset.json')) {
 $text = Get-Content -LiteralPath "$repo/$config" -Raw
 foreach ($id in $retired) { if ($text.Contains($id)) { throw "Retired identity $id in $config" } }
}

# Saved-data keys stay as written by the standalone companions.
$keys = @{
 'unit-caching-cdf/Scripts/Game/EXPBG/EBG_CDFCacheAdapter.c' = @('"ebgCache"', '"cdfState"', '7E1080ED8F0633FD')
 'intel-items-cdf/Scripts/Game/EXPII/EII_CDFState.c' = @('"eiiIntel"', '"eirIntel"', '"cdfState"', '{D3DCA7AB761413C6}PrefabsEditable/EXPII/EII_ManualUS.et', '{AF2266B64D5D4750}PrefabsEditable/EXPII/EIR_ServerRackA.et', '{74CA7EB748CF82EC}PrefabsEditable/EXPII/EIR_USBDrive.et')
 'ambient-destruction-cdf/Scripts/Game/EAD_CDF/EAD_CDF.c' = @('"eadZone"', '"eadBuildings"', '"cdfOriginal"', 'EAD1000000000010')
 'unit-dialog-cdf/Scripts/Game/EXPUD_CDF/EUD_CDFState.c' = @('"eudDialog"', '"cdfState"', '"version"', '"name"', '"lines"', '"gesture"')
 'unit-scripts-cdf/Scripts/Game/EXPUS_CDF/EUS_CDFState.c' = @('"eusScript"', '"cdfState"')
 'inventory-cdf/Scripts/Game/EXPINV_CDF/EINV_CDFInventory.c' = @('"ivp"', '"ivo"', '"ivs"', '"ivl"', '"iva"', '"ivx"', '"cdfState"')
 'vehicle-crew-cdf/Scripts/Game/EXPVC_CDF/EVC_CDFCrew.c' = @('"evcSeat"', '"v"', '"t"', '"i"', '"veh"', '"p"', '"x"', '"y"', '"z"', '"f"')
}
foreach ($path in $keys.Keys) {
 $text = Get-Content -LiteralPath "$repo/addon/$path" -Raw
 foreach ($key in $keys[$path]) { if (!$text.Contains($key)) { throw "Saved-data key $key missing from $path" } }
}

# Garrison ledger bridge (GM Tools 0.1.11): the handshake, capture with exclusion and
# backstop, the clear predicate, validation before the clear, clearBeforeLoad, recovery,
# finalization polling and the GM dialog. The 0.1.5 Full guard is retired.
if (Test-Path -LiteralPath "$repo/addon/garrison-cdf/Scripts/Game/EXPG_CDF/EXPG_CDFGarrisonGuard.c") { throw 'The retired Garrison Full guard must be removed.' }
$bridge = Get-Content -LiteralPath "$repo/addon/garrison-cdf/Scripts/Game/EXPG_CDF/EXPG_CDFGarrisonBridge.c" -Raw
foreach ($required in @(
 'override protected int CdfBridgeVersion()', 'return EXPG_GarrisonPersistence.BRIDGE_API;',
 'static const string KEY = "expgGarrisons";', 'static const string INNER = "cdfOriginal";',
 'EXPG_GarrisonPersistence.ExportJson(json, reason)', 'EXPG_CDFGarrison.StripOwned(document)',
 'override static bool IsManaged(SCR_EditableEntityComponent entity)', 'EXPG_GarrisonPersistence.OwnsForSave(entity.GetOwner())',
 'EXPG_GarrisonPersistence.ParseJson(payload, ledger, reason)', '!ledger.IsEmpty() && !clear', 'EXPG_GarrisonPersistence.BeginImport(why)',
 'bool mutated = result || s_RestoredEntities != previous;', 'EXPG_GarrisonPersistence.DiscardForImport(', 'EXPG_GarrisonPersistence.QueueImport(ledger, queued)',
 'EXPG_GarrisonPersistence.FinishImport();', 'EXPG_CDFGarrison.SkipConfirmed(key)', 'CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)')) {
 if (!$bridge.Contains($required)) { throw "Garrison bridge lost: $required" }
}
# Every refusal returns before super.Restore: the scene is untouched.
$restore = $bridge.Substring($bridge.IndexOf('override static bool Restore('))
$super = $restore.IndexOf('bool result = super.Restore(document);')
foreach ($refusal in @('cannot be loaded:', 'requires CDF clearBeforeLoad', 'still being placed', 'cannot be loaded now')) {
 $at = $restore.IndexOf($refusal)
 if ($at -lt 0 -or $at -gt $super) { throw "Garrison load refusal must come before CDF changes the scene: $refusal" }
}
if ($bridge -match 'TryFullSleep|EXPG_CacheMode\s*=') { throw 'The Garrison bridge must not change caching or the GM-selected mode.' }
# Enforce gotchas in the bridge and its native fixture: reserved names, ASCII, LF.
foreach ($path in @("$repo/addon/garrison-cdf/Scripts/Game/EXPG_CDF/EXPG_CDFGarrisonBridge.c", "$repo/tests/EXPG_CDFGarrisonRoundTrip.c")) {
 $text = Get-Content -LiteralPath $path -Raw
 if ($text -match '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|external|native)\b') { throw "Reserved Enforce name used as a variable in $path" }
 $bytes = [IO.File]::ReadAllBytes($path)
 if (@($bytes | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count) { throw "$path must be ASCII with LF line endings." }
}

$sourceNote = if ($sourceSkips.Count) { " Source repositories not found (skipped): $($sourceSkips -join ', ')." } else { '' }
"PASS: $checked imported files match recorded blobs ($sourceChecks verified against pinned source commits); identity $reserved, dependencies, Unlisted APL listing, retired identities absent, saved-data keys and Garrison ledger bridge.$sourceNote"
exit 0
