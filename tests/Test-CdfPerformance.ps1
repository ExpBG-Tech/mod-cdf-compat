#requires -Version 7.0
# Portable guards for the CDF adapter performance changes (0.1.8):
# - EII_MaybeExpii is an exact prefilter: every whitelisted Intel, rack and drive path contains
#   "/EXPII/" and every GUID form is 18 characters, so GetIntelGuid, GetRackGuid and GetDriveGuid
#   return the same value for every input; each checks it before building its literal list;
# - CDF_GMSaveState.Capture walks a captured inventory once (one GetItems), allocates payloads and
#   duplicate sets only on a hit, keeps the rack trace before the Intel trace, refuses too much
#   Intel at once and too many drives after the pass; the saved classes keep their fields (JSON);
# - the unregistered-author delete summary sums a 30 s window, and world cleanup cancels the
#   pending summary and prints it at once (the maps never stay filled with nothing scheduled).
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$intelPath = "$repo/addon/intel-items-cdf/Scripts/Game/EXPII/EII_CDFState.c"
$authorsPath = "$repo/addon/unit-caching-cdf/Scripts/Game/EXPBG/EBG_CDFAuthorRegistration.c"
$adapterPath = "$repo/addon/unit-caching-cdf/Scripts/Game/EXPBG/EBG_CDFCacheAdapter.c"

# Enforce text scanner: brace depth before each character, skipping string literals and // comments.
$script:DepthCache = @{}
function Get-Depths([string]$Text) {
    if ($script:DepthCache.ContainsKey($Text)) { return ,$script:DepthCache[$Text] }
    $depth = 0; $inString = $false; $comment = $false
    $depths = [int[]]::new($Text.Length)
    for ($i = 0; $i -lt $Text.Length; $i++) {
        $c = $Text[$i]
        $depths[$i] = $depth
        if ($comment) { if ($c -eq "`n") { $comment = $false }; continue }
        if ($inString) { if ($c -eq '\') { $i++; if ($i -lt $Text.Length) { $depths[$i] = $depth } } elseif ($c -eq '"') { $inString = $false }; continue }
        if ($c -eq '"') { $inString = $true; continue }
        if ($c -eq '/' -and $i + 1 -lt $Text.Length -and $Text[$i + 1] -eq '/') { $comment = $true; continue }
        if ($c -eq '{') { $depth++ } elseif ($c -eq '}') { $depth--; $depths[$i] = $depth }
    }
    $script:DepthCache[$Text] = $depths
    return ,$depths
}
# The body between the braces that follow the unique signature (outer braces excluded).
function Get-Body([string]$Text, [string]$Signature) {
    $at = $Text.IndexOf($Signature, [StringComparison]::Ordinal)
    if ($at -lt 0 -or $Text.IndexOf($Signature, $at + 1, [StringComparison]::Ordinal) -ge 0) { throw "Signature not found exactly once: $Signature" }
    $open = $Text.IndexOf('{', $at + $Signature.Length)
    $depths = Get-Depths $Text
    $base = $depths[$open]
    for ($i = $open + 1; $i -lt $Text.Length; $i++) {
        if ($Text[$i] -eq '}' -and $depths[$i] -eq $base) { return $Text.Substring($open + 1, $i - $open - 1) }
    }
    throw "Unbalanced body: $Signature"
}
function Squash([string]$Text) { return ($Text -replace '\s+', ' ').Trim() }
function Count-Of([string]$Text, [string]$Needle) { return ([regex]::Matches($Text, [regex]::Escape($Needle))).Count }
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }

foreach ($path in @($intelPath, $authorsPath, $adapterPath, $PSCommandPath)) {
    $bytes = [IO.File]::ReadAllBytes($path)
    Assert (!@($bytes | Where-Object { $_ -gt 126 -or ($_ -lt 32 -and $_ -ne 9 -and $_ -ne 10) }).Count) "$path must be printable ASCII with LF line endings."
    $text = [IO.File]::ReadAllText($path)
    Assert ($text -notmatch '\b(int|float|bool|string|vector|auto|IEntity|set<IEntity>)\s+(owned|Sleep|Wait|external|native)\b') "Reserved Enforce name used as a variable in $path"
    Assert ($text -notmatch 'Math\.RandomFloat\(') "Math.RandomFloat in $path"
}
$intel = [IO.File]::ReadAllText($intelPath)
$authors = [IO.File]::ReadAllText($authorsPath)
$adapter = [IO.File]::ReadAllText($adapterPath)

# 1. Whitelist prefilter: exact for every input.
$maybe = Squash (Get-Body $intel 'static bool EII_MaybeExpii(string resource)')
Assert ($maybe -ceq 'return resource.Length() == 18 || resource.Contains("/EXPII/");') "EII_MaybeExpii changed: $maybe"
$whitelists = [ordered]@{
    'GetIntelGuid' = @{ Signature = 'static string GetIntelGuid(string resource)'; Filter = 'if (!EII_MaybeExpii(resource)) return string.Empty;'; Count = 7 }
    'GetRackGuid'  = @{ Signature = 'static string GetRackGuid(string resource)';  Filter = 'if (!EII_CDFItem.EII_MaybeExpii(resource)) return string.Empty;'; Count = 3 }
    'GetDriveGuid' = @{ Signature = 'static string GetDriveGuid(string resource)'; Filter = 'if (!EII_CDFItem.EII_MaybeExpii(resource)) return string.Empty;'; Count = 1 }
}
$known = [ordered]@{}
foreach ($name in $whitelists.Keys) {
    $spec = $whitelists[$name]
    $body = Get-Body $intel $spec.Signature
    Assert ((Squash $body).StartsWith($spec.Filter + ' array<string> prefabs = {', [StringComparison]::Ordinal)) "$name must check EII_MaybeExpii before it builds its literal list."
    $literals = @([regex]::Matches($body, '"([^"\\]*)"') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -cne '/EXPII/' })
    Assert ($literals.Count -eq $spec.Count) "$name lists $($literals.Count) prefabs, expected $($spec.Count); update this guard with the whitelist."
    foreach ($path in $literals) {
        Assert ($path -cmatch '^\{[0-9A-F]{16}\}[\x21-\x7E]+\.et$') "$name entry is not an ASCII prefab path: $path"
        Assert ($path.Contains('/EXPII/')) "$name entry lacks /EXPII/, so EII_MaybeExpii would reject it: $path"
        Assert ($path.Substring(0, 18).Length -eq 18 -and $path.Substring(0, 18) -cmatch '^\{[0-9A-F]{16}\}$') "$name GUID form is not 18 characters: $path"
    }
    $known[$name] = $literals
}
# Model of the Enforce functions: == and Contains are case-sensitive.
function Old-Guid([string]$Resource, [string[]]$List) {
    foreach ($path in $List) { $guid = $path.Substring(0, 18); if ($Resource -ceq $path -or $Resource -ceq $guid) { return $guid } }
    return ''
}
function New-Guid([string]$Resource, [string[]]$List) {
    if (!($Resource.Length -eq 18 -or $Resource.Contains('/EXPII/'))) { return '' }
    return Old-Guid $Resource $List
}
$inputs = [Collections.Generic.List[string]]::new()
foreach ($list in $known.Values) { foreach ($path in $list) { $inputs.Add($path); $inputs.Add($path.Substring(0, 18)); $inputs.Add($path.ToLowerInvariant()); $inputs.Add($path.Substring(0, 18).ToLowerInvariant()); $inputs.Add($path.Substring(18)); $inputs.Add($path.Substring(0, 17)); $inputs.Add($path + ' ') } }
foreach ($other in @('', ' ', '{0123456789ABCDEF}', '{0123456789ABCDE}', '{0123456789ABCDEF0}', '{9FBD9CE9F6D14DA5}Prefabs/Characters/Factions/BLUFOR/US_Army/Character_US_Rifleman.et',
    '{D3DCA7AB761413C6}PrefabsEditable/EXPII/EII_ManualUS.et.meta', '{D3DCA7AB761413C6}Prefabs/EXPII/EII_ManualUS.et', 'PrefabsEditable/EXPII/EII_ManualUS.et', '{74CA7EB748CF82EC}', '{74CA7EB748CF82EC}PrefabsEditable/expii/EIR_USBDrive.et', 'abcdefghijklmnopqr')) { $inputs.Add($other) }
$compared = 0
foreach ($name in $known.Keys) {
    foreach ($path in $known[$name]) { Assert ((New-Guid $path $known[$name]) -ceq $path.Substring(0, 18) -and (New-Guid $path.Substring(0, 18) $known[$name]) -ceq $path.Substring(0, 18)) "$name no longer matches $path" }
    foreach ($resource in $inputs) {
        Assert ((Old-Guid $resource $known[$name]) -ceq (New-Guid $resource $known[$name])) "$name prefilter changes the result for '$resource'"
        $compared++
    }
}

# 2. Capture: one inventory pass, allocation on a hit, same order and refusal precedence.
Assert ($intel -cnotmatch '\bCaptureInventory\(') 'The two inventory walks (CaptureInventory) must stay merged into one pass in Capture.'
$capture = Get-Body $intel 'override static string Capture(IEntity entity)'
$flat = Squash $capture
Assert ((Count-Of $capture '.GetItems(') -eq 1) 'Capture must call GetItems exactly once.'
Assert ((Count-Of $capture '.FindComponent(') -eq 6) 'Capture resolves rack, drive, Intel and manager once, then Intel and drive per carried item.'
Assert ((Count-Of $capture 'new EII_CDFPayload()') -eq 2 -and $flat.Contains('if (intel) { payload = new EII_CDFPayload();') -and $flat.Contains('if (!payload) payload = new EII_CDFPayload(); if (!payload.AddCarried(carriedIntel))')) 'The Intel payload must be allocated only for Intel on the entity or a carried Intel hit.'
Assert ((Count-Of $capture 'new EII_CDFRackPayload()') -eq 2 -and $flat.Contains('if (rackComponent || driveComponent) { storage = new EII_CDFRackPayload(); storage.CaptureEntity(entity, rackComponent, driveComponent); }') -and $flat.Contains('if (!storage) storage = new EII_CDFRackPayload(); if (!storage.AddCarried(resource, carriedDrive))')) 'The rack/drive payload must be allocated only for a rack, a drive or a carried drive hit.'
Assert ((Count-Of $capture 'new set<IEntity>') -eq 2 -and $flat.Contains('if (!intelSeen) intelSeen = new set<IEntity>();') -and $flat.Contains('if (!drivesSeen) drivesSeen = new set<IEntity>();')) 'Duplicate sets must be created on their first hit only.'
# The pass itself is pinned exactly: per item Intel first (same dedupe, limit and immediate
# eiiIntel marker), then drives until the drive limit (same checks, refused after the pass).
$pass = 'set<IEntity> intelSeen; set<IEntity> drivesSeen; bool tooManyDrives = false; foreach (IEntity carried : items) { if (!carried) continue; ' +
    'EII_IntelComponent carriedIntel = EII_IntelComponent.Cast(carried.FindComponent(EII_IntelComponent)); if (carriedIntel && (!intelSeen || !intelSeen.Contains(carried))) { ' +
    'if (!payload) payload = new EII_CDFPayload(); if (!payload.AddCarried(carriedIntel)) return "{\"eiiIntel\":{\"version\":0}}"; if (!intelSeen) intelSeen = new set<IEntity>(); intelSeen.Insert(carried); } ' +
    'if (tooManyDrives) continue; EIR_DriveComponent carriedDrive = EIR_DriveComponent.Cast(carried.FindComponent(EIR_DriveComponent)); ' +
    'if (!carriedDrive || carriedDrive.IsEmpty() || (drivesSeen && drivesSeen.Contains(carried))) continue; string resource = EII_CDFRackBridge.PrefabOf(carried); ' +
    'if (EII_CDFRackBridge.GetDriveGuid(resource).IsEmpty()) continue; if (!storage) storage = new EII_CDFRackPayload(); if (!storage.AddCarried(resource, carriedDrive)) { tooManyDrives = true; continue; } ' +
    'if (!drivesSeen) drivesSeen = new set<IEntity>(); drivesSeen.Insert(carried); } ' +
    'if (tooManyDrives) { EII_CDFLoad.Refuse("more than 400 USB drives holding intel in one inventory"); return "{\"eirIntel\":{\"version\":0}}"; } }'
$order = @('if (!entity) return original;', 'EII_CDFPayload payload; EII_CDFRackPayload storage;', 'storage.CaptureEntity(', 'payload.item.Capture(intel)', 'CDF_GMSaveConfig.GetInstance()',
    'if (manager && config.m_bSaveInventories && CanCaptureInventory(entity, config)) { array<IEntity> items = {}; manager.GetItems(items, EStoragePurpose.PURPOSE_ANY);', $pass,
    'if (!payload || (!payload.item && payload.inventory.IsEmpty())) return EII_CDFRackPayload.Wrap(storage, original);', 'return EII_CDFRackPayload.Wrap(storage, encoded);')
$from = 0
foreach ($step in $order) {
    $at = $flat.IndexOf($step, $from, [StringComparison]::Ordinal)
    Assert ($at -ge 0) "Capture lost or reordered: $step"
    $from = $at + $step.Length
}
Assert ((Count-Of $capture 'Refuse(') -eq 1) 'Capture refuses only for too many drives, after the pass.'
Assert ((Squash (Get-Body $intel 'static string Wrap(EII_CDFRackPayload payload, string inner)')).StartsWith('if (!payload || (!payload.rack && !payload.drive && payload.drives.IsEmpty())) return inner;', [StringComparison]::Ordinal)) 'Wrap(null, inner) must return inner unchanged.'
Assert ((Squash (Get-Body $intel 'bool AddCarried(EII_IntelComponent intel)')) -ceq 'if (inventory.Count() >= INVENTORY_LIMIT) return false; EII_CDFItem record = new EII_CDFItem(); record.Capture(intel); inventory.Insert(record); return true;') 'Carried Intel: reject at the limit, never truncate.'
Assert ((Squash (Get-Body $intel 'bool AddCarried(string resource, EIR_DriveComponent component)')) -ceq 'if (drives.Count() >= INVENTORY_LIMIT) return false; EII_CDFDrive record = new EII_CDFDrive(); record.Capture(resource, component); drives.Insert(record); return true;') 'Carried drives: reject at the limit, never truncate.'
# Saved classes keep exactly their serialized fields, so the JSON stays the same.
$fields = [ordered]@{
    'class EII_CDFItem' = @('string prefab;', 'string title;', 'string content;', 'bool spent;', 'bool diagnostics;')
    'class EII_CDFPayload' = @('static const int INVENTORY_LIMIT = 400;', 'int version = 1;', 'ref EII_CDFItem item;', 'ref array<ref EII_CDFItem> inventory = {};')
    'class EII_CDFRack' = @('string prefab;', 'string title;', 'string content;', 'int seconds;', 'bool diagnostics;')
    'class EII_CDFDrive' = @('string prefab;', 'string title;', 'string content;')
    'class EII_CDFRackPayload' = @('static const int INVENTORY_LIMIT = 400;', 'int version = 1;', 'ref EII_CDFRack rack;', 'ref EII_CDFDrive drive;', 'ref array<ref EII_CDFDrive> drives = {};')
}
foreach ($class in $fields.Keys) {
    $body = Get-Body $intel ("`n" + $class + "`n")
    $depths = Get-Depths $body
    $found = @()
    $start = 0
    foreach ($line in $body.Split("`n")) {
        $trim = $line.Trim()
        if ($trim -and $depths[$start] -eq 0 -and $trim.EndsWith(';') -and !$trim.StartsWith('//')) { $found += $trim }
        $start += $line.Length + 1
    }
    Assert (($found -join '|') -ceq ($fields[$class] -join '|')) "$class fields changed (saved JSON shape): $($found -join ' ')"
}

# 3. Unregistered-author summary: a 30 s window, cancelled and flushed at world cleanup.
Assert ($authors.Contains('static const int UNREGISTERED_WINDOW_MS = 30000;')) 'The unregistered-author window must stay 30 s.'
$note = Squash (Get-Body $authors 'static void NoteUnregisteredDelete(string key, string prefab)')
Assert ($note.StartsWith('if (Unregistered.IsEmpty() && GetGame()) GetGame().GetCallqueue().CallLater(PrintUnregistered, UNREGISTERED_WINDOW_MS, false);', [StringComparison]::Ordinal)) 'The first unregistered delete must schedule one summary for the window.'
Assert ($authors -notmatch 'CallLater\(PrintUnregistered,\s*0\b') 'The per-frame unregistered-author summary must not return.'
$print = Squash (Get-Body $authors 'static void PrintUnregistered()')
Assert ($print.Contains('Unregistered.Clear(); UnregisteredPrefab.Clear();') -and $print.Contains('(within %2 s)", total, UNREGISTERED_WINDOW_MS / 1000)')) 'The summary must clear both maps and name its window.'
$shutdown = Get-Body $adapter 'static void EBG_ShutdownForWorldCleanup()'
$remove = $shutdown.IndexOf('GetGame().GetCallqueue().Remove(EBG_CDFAuthors.PrintUnregistered);', [StringComparison]::Ordinal)
$flush = $shutdown.IndexOf('EBG_CDFAuthors.PrintUnregistered();', [StringComparison]::Ordinal)
Assert ($remove -ge 0 -and $flush -gt $remove -and (Get-Depths $shutdown)[$flush] -eq 0) 'World cleanup must remove the pending summary, then print it unconditionally (outside the GetGame block).'

"PASS: EII_MaybeExpii exact over $($known.Values | ForEach-Object { $_.Count } | Measure-Object -Sum | Select-Object -ExpandProperty Sum) whitelisted prefabs ($compared model comparisons); Capture walks inventories once with lazy payloads, unchanged order, refusals and saved fields; unregistered-author summary windowed and flushed at world cleanup."
