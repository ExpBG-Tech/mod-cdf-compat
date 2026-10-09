#requires -Version 7.0
# Portable guard for the Inventory CDF completion pass (addon/inventory-cdf). CDF 1.4.1
# empties each cargo storage and respawns its saved items; items its engine fallback
# re-places can vanish (native run 2026-10-09: magazines and M67 grenades of a vanilla fire
# team). The pass gives back only the missing items after CDF's deferred state pass. Checks
# the CDF keys it reads (no saved data of its own), the "really applied" and refusal
# attribution around super.Apply, the multiset plan (refusals out, CDF's own traversal for
# what is held), duplicate safety (retries bounded by failures, loose items deleted,
# prefabs that spawn with contents skipped), players skipped, bounded pump without frame
# events, the summary line, Enforce gotchas and the native round-trip fixture wiring.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bridgePath = "$repo/addon/inventory-cdf/Scripts/Game/EXPINV_CDF/EINV_CDFInventory.c"
$fixturePath = "$repo/tests/EINV_CDFInventoryRoundTrip.c"
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 Assert (![regex]::Match($Text.Substring($match.Index + $match.Length), $Signature).Success) "signature not unique: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
# Code without comments; string literals blanked.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
function Assert-Order([string]$Text, [string[]]$Steps, [string]$Label) {
 $from = 0
 foreach ($step in $Steps) { $at = $Text.IndexOf($step, $from); Assert ($at -ge 0) "${Label}: lost or reordered: $step"; $from = $at + $step.Length }
}
$bridge = Read-Text $bridgePath
$code = Get-Code $bridge
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
$entry = @($pack.modules | Where-Object { $_.name -ceq 'inventory-cdf' })
Assert ($entry.Count -eq 1 -and $entry[0].origin.repository -ceq 'ExpBG-Tech/mod-cdf-compat' -and !$entry[0].files) 'inventory-cdf is a module developed in this repository'
Assert (@(Get-ChildItem -LiteralPath "$repo/addon/inventory-cdf" -Recurse -File).Count -eq 1) 'the module holds only its bridge script'

# Save format unchanged: CDF's own keys are only read; nothing is written or captured.
foreach ($key in '"ivp"', '"ivo"', '"ivs"', '"ivl"', '"iva"', '"ivx"', '"cdfState"') { Assert $bridge.Contains("context.ReadValue($key") "reads CDF inventory key $key" }
Assert (!$code.Contains('JsonSaveContext') -and !$code.Contains('WriteValue') -and $code -notmatch 'override\s+static\s+string\s+Capture') 'the pass saves nothing (existing saves load as they are)'
$read = Get-Body $bridge 'static\s+bool\s+ReadSaved\s*\('
Assert ($read.Contains('for (int depth = 0; depth < PEEL_LIMIT; depth++)') -and $read.Contains('context.ReadValue("cdfState", inner)') -and $read.Contains('text = inner;')) 'saved lists are read under any EXPBG cdfState envelope (bounded)'
Assert ($read.Contains('while (structural.Count() < count) structural.Insert(0);') -and $read.Contains('if (owners.Count() < count || storages.Count() < count)')) 'older saves read like CDF reads them; truncated lists are refused'

# Apply wrapper: "really applied" and refusal attribution around super.
$apply = Get-Body $bridge 'override\s+static\s+void\s+Apply\s*\(\s*IEntity\s+entity,\s*string\s+state\s*\)'
Assert-Order $apply @('int handled = s_iItemsKept + s_iItemsPlaced + s_iItemsFallback + s_iItemsRefused;', 'int refusedBefore = s_iItemsRefused;', 'EINV_CDF.BeginRefusals();', 'super.Apply(entity, state);', 'EINV_CDF.EndRefusals(refused);', 'int applied = s_iItemsKept + s_iItemsPlaced + s_iItemsFallback + s_iItemsRefused - handled;', 'if (applied <= 0)', 'EINV_CDF.Register(entity, state, applied, s_iItemsRefused - refusedBefore, refused);') 'Apply'
Assert (([regex]::Matches($apply, 'super\.Apply\(entity, state\);')).Count -eq 2 -and !$apply.Contains('super.Apply(entity, original')) 'Apply hands the state on unchanged on every path'
$register = Get-Body $bridge 'static\s+void\s+Register\s*\('
Assert ($register.Contains('entry.m_aPrefabs.Count() != applied || refused.Count() != refusedCount') -and $register.Contains('s_iSkipped++;')) 'an entity CDF did not fully apply, or whose refusals cannot all be named, is left alone'
Assert ($bridge.Contains('static const string REFUSAL_TRACE = "Objet refuse par l''inventaire : ";')) 'refusals are named by CDF 1.4.1''s own trace text'
$trace = Get-Body $bridge 'override\s+static\s+void\s+LogDebug\s*\(\s*string\s+message\s*\)'
Assert ($trace.Contains('EINV_CDF.NoteTrace(message);') -and $trace.Contains('super.LogDebug(message);')) 'CDF''s debug trace is observed and still printed by CDF'

# Plan: saved multiset minus refusals minus what is held, walked by CDF's own capture.
$plan = Get-Body $bridge 'protected\s+static\s+void\s+Plan\s*\('
Assert-Order $plan @('foreach (string savedPrefab : entry.m_aPrefabs)', 'foreach (string refusedPrefab : entry.m_aRefused) Take(need, refusedPrefab);', 'CDF_GMSaveState.EINV_Live(entity, held);', 'foreach (string heldPrefab : held) Take(need, heldPrefab);', 'if (Take(need, entry.m_aPrefabs[i])) missing.Insert(i);', 'missing.Sort();') 'Plan'
$live = Get-Body $bridge 'static\s+void\s+EINV_Live\s*\('
Assert ($live.Contains('CaptureInventory(entity, prefabs, owners, storages, slots, ammo, structural);')) 'what an entity holds is walked like a CDF save'

# Duplicate safety and placement.
$step = Get-Body $bridge 'protected\s+static\s+bool\s+Step\s*\('
Assert ($step.Contains('int failed = DropSpawned(entry);') -and $step.Contains('if (missing.IsEmpty() || failed == 0 || entry.m_iRound >= MAX_ROUNDS)') -and $step.Contains('if (retry.Count() < failed) retry.Insert(savedIndex);')) 'a retry never exceeds the items that left the inventory or vanished'
Assert ($step.IndexOf('DropSpawned(entry);') -lt $step.IndexOf('Plan(entry, entity, missing);')) 'loose items are deleted before the inventory is walked again'
Assert ($bridge.Contains('static const int MAX_ROUNDS = 2;')) 'at most one retry'
$drop = Get-Body $bridge 'protected\s+static\s+int\s+DropSpawned\s*\('
Assert ($drop.Contains('inventoryItem.GetParentSlot()') -and $drop.Contains('SCR_EntityHelper.DeleteEntityAndChildren(spawned);')) 'a spawned item outside any slot is deleted'
$place = Get-Body $bridge 'protected\s+static\s+void\s+Place\s*\('
Assert-Order $place @('Resource loaded = Resource.Load(prefab);', 'if (!loaded || !loaded.IsValid())', 'GetGame().SpawnEntityPrefab(loaded,', 'if (HoldsItems(item) || !Insert(entry, manager, index, item))', 'SCR_EntityHelper.DeleteEntityAndChildren(item);', 'magazine.SetAmmoCount(rounds);', 'entry.m_aSpawned.Insert(item);') 'Place'
$insert = Get-Body $bridge 'protected\s+static\s+bool\s+Insert\s*\('
Assert-Order $insert @('Candidates(entry, manager, index, candidates);', 'manager.TryInsertItemInStorage(item, candidate, slot)', 'manager.TryInsertItemInStorage(item, candidate, -1)', 'if (equipment)', 'return manager.TryInsertItem(item, EStoragePurpose.PURPOSE_DEPOSIT);') 'Insert (saved storage first; equipment never dumped into cargo)'

# Players, bounds, scheduling, summary.
foreach ($signature in 'static\s+void\s+Register\s*\(', 'static\s+bool\s+Track\s*\(', 'protected\s+static\s+bool\s+Step\s*\(') { Assert ((Get-Body $bridge $signature).Contains('IsPlayer(entity)')) "players are never touched: $signature" }
Assert ((Get-Body $bridge 'static\s+bool\s+IsPlayer\s*\(').Contains('GetPlayerIdFromControlledEntity(entity) != 0')) 'player check uses the controlled entity'
foreach ($needle in 'static const int SCANS_PER_PUMP = 4;', 'static const int SPAWNS_PER_PUMP = 8;', 'static const int PUMP_MS = 100;', 'static const int SETTLE_MS = 2000;', 'static const int VERIFY_MS = 1000;') { Assert $bridge.Contains($needle) "bounded pump: $needle" }
$pump = Get-Body $bridge 'static\s+void\s+Pump\s*\(\s*\)'
Assert ($pump.Contains('while (index < s_aEntries.Count() && scans < SCANS_PER_PUMP && spawns < SPAWNS_PER_PUMP)') -and $pump.Contains('Schedule(PUMP_MS);') -and $pump.Contains('s_aEntries.RemoveOrdered(index);')) 'the pump stops at its budget and reschedules itself'
Assert ($code -notmatch 'EOnFrame|SetEventMask|EntityEvent\.FRAME|ScriptCallQueue\.Call\(|CallLater\([^)]*,\s*true\)') 'no per-frame script and no repeating timer'
$restore = Get-Body $bridge 'override\s+static\s+bool\s+Restore\s*\(\s*notnull\s+CDF_GMSaveDocument\s+document\s*\)'
Assert-Order $restore @('bool result = super.Restore(document);', 'if (result) EINV_CDF.BeginLoad();', 'return result;') 'Restore (a refused load keeps the running pass)'
Assert ($bridge.Contains('"[EXPBG CDF INV] completed=%1 items on %2 entities; unplaced=%3; %4%5"') -and $bridge.Contains('static const int NAMES_LIMIT = 4;')) 'one bounded summary line per load'

# Enforce gotchas in the bridge and its fixture.
foreach ($path in @($bridgePath, $fixturePath)) {
 $text = Read-Text $path
 $src = Get-Code $text
 Assert (![regex]::IsMatch($src, '\b(int|float|bool|string|vector|auto|IEntity|ResourceName)\s+(owned|Sleep|Wait|external|native|vanilla)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($src, '\bvanilla\b')) "keyword vanilla used in $path"
 Assert (![regex]::IsMatch($src, '\b[A-Za-z_]\w*(?:<[^>]*>)?\s+(Resource|ResourceName|IEntity|World|BaseWorld|string|vector|array|set|map|ref|typename|func)\s*(=|;)')) "a variable named like a type in $path"
 Assert (!$src.Contains('Math.RandomFloat(')) "Math.RandomFloat in $path"
 Assert (![regex]::IsMatch($src, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "static initializer in $path"
 Assert (![regex]::IsMatch($src, '\bif\s*\([^\r\n]*\)\s*return\b')) "each return on its own line in $path"
 Assert (![regex]::IsMatch($src, '\bint\s+\w+\s*=\s*(true|false|\w+\s*(==|!=|<=|>=|&&|\|\|))') -and ![regex]::IsMatch($src, '(\+|-)=\s*(true|false)\b')) "implicit bool to int in $path"
 Assert (![regex]::IsMatch($src, 'Resource\.Load\([^)]*\)\s*\.')) "Resource.Load result kept in a local in $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "$path must be ASCII with LF line endings"
 # A bool variable or parameter never initialises or adds to an int.
 $bools = @([regex]::Matches($src, '\bbool\s+(\w+)\s*[=;,)]') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
 foreach ($name in $bools) { Assert (![regex]::IsMatch($src, '\bint\s+\w+\s*=\s*!?' + $name + '\s*;|(\+|-)=\s*!?' + $name + '\s*;')) "implicit bool to int ($name) in $path" }
 foreach ($method in [regex]::Matches($src, '(?m)^\s*(?:(?:static|protected|override|private)\s+)*(?:void|bool|int|float|string|vector|IEntity|\w+)\s+(\w+)\s*\(([^;{]*)\)\s*\{')) {
  $open = $method.Index + $method.Length - 1; $depth = 0; $end = $open
  $parameters = @([regex]::Matches($method.Groups[2].Value, '(\w+)\s*(?:=[^,]*)?(?:,|$)') | ForEach-Object { $_.Groups[1].Value })
  for ($i = $open; $i -lt $src.Length; $i++) { if ($src[$i] -eq '{') { $depth++ } elseif ($src[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } } }
  $body = $src.Substring($open, $end - $open)
  $declared = @([regex]::Matches($body, '(?m)(?:^\s*|[;{(]\s*|\bforeach\s*\(\s*)(?:ref\s+)?(?:int|float|bool|string|vector|IEntity|[A-Z]\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:\[\d+\])?\s*(?:=|;|:)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin 'return', 'new' })
  $dupes = @(@($declared) + @($parameters) | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Assert ($dupes.Count -eq 0) "variable declared twice in $($method.Groups[1].Value) of ${path}: $($dupes -join ', ')"
  foreach ($name in $declared) { Assert ($name -cmatch '^[a-z]') "local $name in $($method.Groups[1].Value) of $path must start lower-case" }
 }
}
# Bridge members: m_ on entries, s_ on statics (constants upper-case).
foreach ($field in [regex]::Matches($code, '(?m)^ (static\s+)?(?:ref\s+)?(?!const\b)(?:int|bool|string|IEntity|array<[^>]+>+|map<[^>]+>)\s+([\w, ]+?)\s*(?:=[^;]*)?;')) {
 foreach ($name in ($field.Groups[2].Value -split ',\s*')) {
  if ($field.Groups[1].Success) { Assert ($name -cmatch '^s_') "static $name must use the s_ prefix" } else { Assert ($name -cmatch '^m_') "member $name must use the m_ prefix" }
 }
}
Assert (@([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'this guard is ASCII with LF line endings'

# Native round trip wiring (Run-CdfRoundTrip.ps1, orchestrator only).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG EINV CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 soldiers=4 inventories=4 cdfLoss=\d+ completed=\d+ unplaced=0 idempotent=1 refusalKept=1 removedRestored=1 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EINV_CDFInventoryRoundTrip.c -ExpectResult '$expect' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'round-trip fixture header carries its runner command'
Assert ($fixture -match 'class\s+EXPG_CdfRoundTripClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_CdfRoundTrip\s*:\s*GenericEntity') 'round-trip fixture keeps the runner driver class name'
foreach ($needle in 'CDF_GMSaveCapture.Capture("einv-cdf-roundtrip", "fixture")', 'document.SaveToFile(FILE)', 'm_Loaded.LoadFromFile(FILE)', 'CDF_GMSaveRestore.Restore(m_Loaded)', 'cfg.m_bSaveCharacterInventories = true;', 'cfg.m_bClearBeforeLoad = true;', 'Resource teamResource = Resource.Load(teamName);', 'CDF_GMSaveState.EINV_Live(entity, names);', 'if (expect.m_sLoaded == expect.m_sItems)', 'EINV_CDF.Track(expect.m_Actor, prefabs, owners, storages, slots, ammo, structural, refused)', 'manager.TryDeleteItem(item)', 'refused.Insert(m_sRemoved);', 'EINV_CDF.s_iLastRefused == 1') {
 Assert $fixture.Contains($needle) "round-trip fixture must drive: $needle"
}
# The before-save snapshot is taken in the frame of the save, and is the saved list itself.
$capturePhase = Get-Body $fixture 'void\s+CaptureAndLoad\s*\(\s*\)'
Assert ($capturePhase.IndexOf('expect.m_sItems = Items(soldier);') -ge 0 -and $capturePhase.IndexOf('expect.m_sItems = Items(soldier);') -lt $capturePhase.IndexOf('CDF_GMSaveCapture.Capture(')) 'every soldier is snapshotted in the frame of the save'
Assert ($capturePhase.Contains('if (read && Join(prefabs) == candidate.m_sItems) matched++;') -and $capturePhase.Contains('Check(matched == 4,')) 'the snapshot equals each soldier''s saved CDF list'
$wrapper = Get-Body $fixture 'modded\s+class\s+CDF_GMSaveState'
Assert ($wrapper.IndexOf('super.Apply(entity, state);') -ge 0 -and $wrapper.IndexOf('super.Apply(entity, state);') -lt $wrapper.IndexOf('EXPG_CdfRoundTrip.NoteApplied(entity);')) 'the fixture notes items only after CDF applied the state'
Assert ($fixture.Contains('PrintFormat("[EXPG EINV CDF ROUNDTRIP RESULT] %1 %2", first, second);')) 'round-trip fixture prints its result line'
'PASS: Inventory CDF completion pass: CDF keys read only (save format unchanged), applied/refusal attribution around super.Apply, multiset plan through CDF''s own traversal, duplicate-safe placement and bounded retries, players skipped, bounded pump without frame events, one summary line, Enforce gotchas; round-trip fixture wired against the before-save snapshot.'
