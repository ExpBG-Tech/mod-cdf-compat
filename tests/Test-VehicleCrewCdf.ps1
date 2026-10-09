#requires -Version 7.0
# Portable guard for the Vehicle Crew CDF bridge (addon/vehicle-crew-cdf). CDF 1.4.1 loses AI
# crews: the vanilla character crew link (Serialize/Deserialize target and seat) is
# commented out, so a saved crew member comes back standing inside his vehicle, and the
# default crew the game spawns with a GM "crewed" vehicle has no author, so CDF skips its
# group. The bridge adds those crews as ordinary CDF records, stores each seat as one extra
# top-level state key plus CDF's own crew link, and after a load moves each restored crew
# member straight into his seat. Checks the saved key, the lossless tag, refusals before
# CDF changes the scene, crew links hidden from CDF while it restores (states untouched),
# the exclusions (players, prisoners, garrison, reserved groups, ACE animation helpers),
# the Clear predicate, bounded seating without any spawn, the summary line, Enforce
# gotchas and the native round-trip fixture wiring. No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bridgePath = "$repo/addon/vehicle-crew-cdf/Scripts/Game/EXPVC_CDF/EVC_CDFCrew.c"
$fixturePath = "$repo/tests/EVC_CDFCrewRoundTrip.c"
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
# Code without comments; string literals blanked.
function Get-Code([string]$Text) {
 [regex]::Replace($Text, '"(?:\\.|[^"\\\r\n])*"|//[^\r\n]*|/\*[\s\S]*?\*/', [Text.RegularExpressions.MatchEvaluator]{
  param($m)
  if ($m.Value[0] -eq '"') { return '"' + [string]::new(' ', $m.Value.Length - 2) + '"' }
  return [regex]::Replace($m.Value, '[^\r\n]', ' ')
 })
}
# Body of a unique signature: braces counted on the code (string literals blanked, same
# offsets), the original text returned.
function Get-Body([string]$Text, [string]$Signature) {
 $scan = Get-Code $Text
 $match = [regex]::Match($scan, $Signature)
 Assert $match.Success "signature not found: $Signature"
 Assert (![regex]::Match($scan.Substring($match.Index + $match.Length), $Signature).Success) "signature not unique: $Signature"
 $open = $scan.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $scan.Length; $i++) {
  if ($scan[$i] -eq '{') { $depth++ } elseif ($scan[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
function Assert-Order([string]$Text, [string[]]$Steps, [string]$Label) {
 $from = 0
 foreach ($step in $Steps) { $at = $Text.IndexOf($step, $from); Assert ($at -ge 0) "${Label}: lost or reordered: $step"; $from = $at + $step.Length }
}
$bridge = Read-Text $bridgePath
$code = Get-Code $bridge
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
$entry = @($pack.modules | Where-Object { $_.name -ceq 'vehicle-crew-cdf' })
Assert ($entry.Count -eq 1 -and $entry[0].origin.repository -ceq 'ExpBG-Tech/mod-cdf-compat' -and !$entry[0].files) 'vehicle-crew-cdf is a module developed in this repository'
Assert (@(Get-ChildItem -LiteralPath "$repo/addon/vehicle-crew-cdf" -Recurse -File).Count -eq 1) 'the module holds only its bridge script'

# Saved format: one extra top-level key, versioned payload; CDF's own crew link for vehicles.
foreach ($needle in 'static const string KEY = "evcSeat";', 'static const int VERSION = 1;', 'context.WriteValue("v", VERSION);', 'context.WriteValue("t", type);', 'context.WriteValue("i", index);', 'context.WriteValue("veh", linked);', 'context.WriteValue("p", path);', 'context.WriteValue("f", faction);') { Assert $bridge.Contains($needle) "seat payload: $needle" }
$tag = Get-Body $bridge 'static\s+string\s+Tag\s*\(\s*string\s+state,\s*string\s+payload\s*\)'
Assert-Order $tag @('string head = Head(payload);', 'tagged = head + "}";', 'tagged = head + "," + inner;', 'check.ReadValue(KEY, back) || back != payload', 'bool untagged = Untag(tagged, payload, original);', 'if (!untagged || !same)', 'return tagged;') 'Tag'
$untag = Get-Body $bridge 'static\s+bool\s+Untag\s*\('
Assert ($untag.Contains('tagged.IndexOf(head) != 0') -and $untag.Contains('original = "{" + rest.Substring(1, rest.Length() - 1);')) 'the tag is lossless: the inner chain''s state is exactly what follows the key'
$crew = Get-Body $bridge 'static\s+bool\s+CaptureCrew\s*\('
Assert-Order $crew @('int firstAdded = document.m_aEntities.Count();', 'if (!IsVehicle(vehicle)) continue;', 'SCR_AIGroup group = Adoptable(slot.GetOccupant(), saved);', 'int added = Adopt(document, group, vehicleRecord, saved);', 'crew.m_iTarget != CDF_GMSaveEntityRecord.TARGET_NONE || crew.m_iEntityType != EEditableEntityType.CHARACTER', 'BaseCompartmentSlot seat = SeatOf(crew.m_Entity.GetOwner(), holder, seatIndex);', 'if (IsVehicle(holder)) saved.Find(holder, holderIndex);', 'string tagged = Tag(crew.m_sState, payload);', 'crew.m_sState = tagged;', 'crew.m_iTarget = holderIndex;', 'crew.m_iTargetValue = seatIndex;', 'ValidAdded(document, firstAdded, reason)', '[EXPBG CDF CREW SAVE] seats=%1') 'CaptureCrew'
$adopt = Get-Body $bridge 'static\s+int\s+Adopt\s*\('
Assert ($adopt.Contains('groupRecord.m_sAuthorUID = vehicleRecord.m_sAuthorUID;') -and $adopt.Contains('NewRecord(editable, groupIndex)') -and $adopt.IndexOf('document.m_aEntities.Insert(groupRecord);') -lt $adopt.IndexOf('document.m_aEntities.Insert(added);')) 'an added crew is its group (author of the vehicle) before its members, parented to it'
$adoptable = Get-Body $bridge 'static\s+SCR_AIGroup\s+Adoptable\s*\('
Assert ($adoptable.Contains('!group.m_bEVC_DefaultCrew || saved.Contains(group) || EXPG_GarrisonPersistence.OwnsForSave(group)')) 'only default crews CDF did not save are added (never scenario squads, garrisons)'
$newRecord = Get-Body $bridge 'static\s+CDF_GMSaveEntityRecord\s+NewRecord\s*\('
Assert ($newRecord.Contains('record.m_sState = CDF_GMSaveState.Capture(owner);') -and $newRecord.Contains('CDF_GMSaveAttributes.Read(editable,')) 'added records use CDF''s own state capture (the whole adapter chain) and attributes'
$validAdded = Get-Body $bridge 'static\s+bool\s+ValidAdded\s*\('
foreach ($check in 'EUS_CDF.ValidDocument(added, false,', 'EUD_CDF.ValidDocument(added, false,', 'EII_CDFPayload.ValidDocument(added)') { Assert $validAdded.Contains($check) "added records pass the other adapters' save checks: $check" }
$captureDoc = Get-Body $bridge 'override\s+static\s+CDF_GMSaveDocument\s+Capture\s*\(\s*string\s+displayName,\s*string\s+author\s*\)'
Assert-Order $captureDoc @('CDF_GMSaveDocument document = super.Capture(displayName, author);', 'return null;', 'EVC_CDF.CaptureCrew(document, reason)', 'EVC_CDF.Reject(reason + "; nothing was written");', 'return null;', 'return document;') 'Capture'

# Exclusions: players, prisoners, garrison guards, reserved groups, ACE animation helpers.
$keep = Get-Body $bridge 'static\s+bool\s+Keepable\s*\('
foreach ($needle in 'IsPlayer(character)', 'ECharacterLifeState.DEAD', 'EPR_CDF.IsCaptiveSeat(character)', 'EXPG_GarrisonPersistence.OwnsForSave(character)', 'EBG_CacheManager.Instance.IsReserved(group)', 'IsHelper(access.GetCompartment().GetVehicle())') { Assert $keep.Contains($needle) "exclusion: $needle" }
$helper = Get-Body $bridge 'static\s+bool\s+IsHelper\s*\('
Assert ($helper.Contains('"helpercompartment"') -and $helper.Contains('"/helpers/"') -and $code -notmatch '\bACE_\w+') 'ACE captive and carrying helpers are recognised by name only (no compile dependency)'
$seatOf = Get-Body $bridge 'static\s+BaseCompartmentSlot\s+SeatOf\s*\('
Assert ($seatOf.Contains('access.IsGettingIn() || access.IsGettingOut()') -and $seatOf.Contains('IEntity top = slot.GetVehicle(found);') -and $seatOf.Contains('found < 0 || IsHelper(top)')) 'only a full seat with a stable index in its holder counts'
Assert ($bridge.Contains('holder.FindComponent(SCR_EditableVehicleComponent) != null')) 'a real vehicle (crew link) is an editable vehicle; any other holder is a prop'
$heine = @(Select-String -LiteralPath $bridgePath -Pattern 'Heine|Sentar|Office_chair' -SimpleMatch:$false)
Assert ($heine.Count -eq 0 -or @($heine | Where-Object { $_.Line -notmatch '^\s*//' }).Count -eq 0) 'no hard reference to Heine prefabs or classes'

# Clear: a saved vehicle's default crew goes with it; super first.
$managed = Get-Body $bridge 'override\s+static\s+bool\s+IsManaged\s*\('
Assert-Order $managed @('if (super.IsManaged(entity))', 'return true;', 'return EVC_CDF.ClearedWithVehicle(entity);') 'IsManaged'
$cleared = Get-Body $bridge 'static\s+bool\s+ClearedWithVehicle\s*\('
Assert ($cleared.Contains('EEditableEntityType.GROUP') -and $cleared.Contains('group.m_bEVC_DefaultCrew') -and $cleared.Contains('CDF_GMSaveCapture.IsManaged(SCR_EditableEntityComponent.GetEditableEntity(holder))')) 'only default crew groups seated in a managed vehicle are cleared'
$marker = Get-Body $bridge 'override\s+protected\s+void\s+FinishedSpawningDefaultOccupants\s*\(\s*bool\s+wasCanceled\s*\)'
Assert-Order $marker @('SCR_AIGroup.Cast(m_SpawnedOccupantsAIGroup)', 'crew.m_bEVC_DefaultCrew = true;', 'super.FinishedSpawningDefaultOccupants(wasCanceled);') 'default crew marker'

# Load: read and refuse before CDF clears; crew link hidden, states untouched; seat after.
$restore = Get-Body $bridge 'override\s+static\s+bool\s+Restore\s*\(\s*notnull\s+CDF_GMSaveDocument\s+document\s*\)'
Assert-Order $restore @('EVC_CDF.ReadDocument(document, seats, skipped, reason)', 'EVC_CDF.Reject(reason + "; the scene was not cleared");', 'return false;', 'EVC_CDF.Hide(seats);', 'bool result = super.Restore(document);', 'EVC_CDF.Reveal(document, seats, count);', 'EVC_CDF.EndLoad();', 'EVC_CDF.BeginLoad(document, seats, skipped);', 'CallLater(EVC_PumpCrew, EVC_CDF.PUMP_MS, false);', '[CDF TIMING] vehicle-crew restore') 'Restore'
$read = Get-Body $bridge 'static\s+bool\s+ReadDocument\s*\('
Assert ($read.Contains('!context.LoadFromString(record.m_sState) || !context.ReadValue(KEY, payload)') -and $read.Contains('return false;') -and $read.Contains('EVC_Seat seat = Decode(payload, why);') -and $read.Contains('skipped++;')) 'an unreadable state refuses the load; an unknown seat version only loses that seat'
$hide = Get-Body $bridge 'static\s+void\s+Hide\s*\('
Assert ($hide.Contains('seat.m_Record.m_iTarget = CDF_GMSaveEntityRecord.TARGET_NONE;') -and !$hide.Contains('m_sState')) 'CDF does not replay the crew link (first free seat); stored states stay exact (Intel Items matches them by text)'
Assert (![regex]::IsMatch((Get-Body $bridge 'static\s+void\s+Reveal\s*\('), 'm_sState')) 'Reveal restores only crew links'
$pump = Get-Body $bridge 'static\s+void\s+EVC_PumpCrew\s*\(\s*\)'
Assert ($pump.Contains('!s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty() && s_aGuardedGroups && s_aGuardedGroups.IsEmpty() && s_aPendingMembers && s_aPendingMembers.IsEmpty()') -and $pump.Contains('EVC_CDF.Pump(complete)')) 'group work waits for CDF''s deferred finalization'
foreach ($bound in 'static const int IMMEDIATE_SEATS = 32;', 'static const int SEATS_PER_PUMP = 16;', 'static const int PUMP_MS = 100;', 'static const int MAX_ATTEMPTS = 4;', 'static const int MAX_POLLS = 600;', 'static const float PROP_RADIUS = 0.35;', 'static const int MAX_SEATS = 1024;') { Assert $bridge.Contains($bound) "bounded work: $bound" }
$request = Get-Body $bridge 'protected\s+static\s+void\s+Request\s*\('
Assert ($request.Contains('access.GetInVehicle(slot.GetOwner(), slot, true, -1, ECloseDoorAfterActions.INVALID, true);') -and $request.Contains('occupant || !access || !slot.IsCompartmentAccessible() || slot.GetType() != seat.m_iType')) 'instant move into the exact saved seat; a taken, closed or retyped seat is not forced'
$advance = Get-Body $bridge 'protected\s+static\s+void\s+Advance\s*\('
Assert-Order $advance @('seat.m_iResult = RESULT_FAILED;', 'Request(seat, character, now);', 'if (!cdfDone)', 'Settle(seat, character);', 'seat.m_iResult = RESULT_SEATED;', 'if (seat.m_iAttempts < MAX_ATTEMPTS)', 'Stand(seat, character);', 'seat.m_iResult = RESULT_STANDING;') 'Advance'
$settle = Get-Body $bridge 'protected\s+static\s+void\s+Settle\s*\('
Assert ($settle.Contains('group.InitFactionKey(seat.m_sFaction)') -and $settle.Contains('utility.AddUsableVehicle(usage)') -and $settle.Contains('if (!IsVehicle(seat.m_HolderEntity))')) 'a seated crew''s group knows its vehicle (never a chair) and keeps its faction'
Assert ($code -notmatch 'SpawnEntityPrefab|Resource\.Load') 'the bridge never spawns anything (CDF restores each crew member once)'
$finish = Get-Body $bridge 'static\s+void\s+Finish\s*\(\s*bool\s+interrupted\s*\)'
Assert ($finish.Contains('"[EXPBG CDF CREW] restored=%1 seated=%2 fallbackStanding=%3 failed=%4"') -and $finish.Contains('Print(first + second, severity);')) 'one summary line per load'
Assert ($bridge.Contains('override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)') -and $bridge.Contains('class EVC_CDFRefusalDialog : CDF_GMSaveBaseDialog')) 'the Game Master sees why a save or load was refused'
# GM Tools API used: existing public seams only.
$symbols = @([regex]::Matches($code, '\b(?:EBG|EII|EAD|EXPG|EUD|EIR|ESR|EGS|EAU|EUS|EBM)_\w+(?:\.\w+)?') | ForEach-Object Value | Sort-Object -Unique)
$allowed = 'EBG_CacheManager', 'EBG_CacheManager.Instance', 'EXPG_GarrisonPersistence.OwnsForSave', 'ESR_SurrenderManager.FindPrisoner', 'EUS_CDF.ValidDocument', 'EUD_CDF.ValidDocument', 'EII_CDFPayload.ValidDocument'
foreach ($symbol in $symbols) { Assert ($symbol -cin $allowed) "bridge uses only existing seams: $symbol" }

# Enforce gotchas in the bridge and its fixture.
foreach ($path in @($bridgePath, $fixturePath)) {
 $text = Read-Text $path
 $code = Get-Code $text
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external|native)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($code, '\b\w+(?:<[\w<>, ]+>)?\s+vanilla\s*[=;]')) "a local named vanilla in $path"
 Assert (!$code.Contains('Math.RandomFloat(')) "Math.RandomFloat in $path"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "static initializer in $path"
 Assert (![regex]::IsMatch($code, '\bif\s*\([^\r\n]*\)\s*return\b|\belse\s+return\b|\{\s*return\b[^\r\n]*\}')) "each return on its own line in $path"
 Assert (![regex]::IsMatch($code, '\bint\s+\w+\s*=\s*[^;]*(?:==|!=|&&|\|\|)[^;]*;')) "no implicit bool to int in $path"
 Assert (![regex]::IsMatch($code, 'SpawnEntityPrefab\s*\(\s*Resource\.Load')) "Resource.Load results kept in locals in $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "$path must be ASCII with LF line endings"
 # Fields: m_ (members) and s_ (statics) prefixes; constants upper case.
 foreach ($class in [regex]::Matches($code, '(?ms)^(?:modded\s+)?class\s+(\w+)[^{]*\{(.*?)^\}')) {
  foreach ($field in [regex]::Matches($class.Groups[2].Value, '(?m)^ (?!\s)(?:(static)\s+)?(?:(const)\s+)?(?:ref\s+|protected\s+)*(?:[A-Za-z_]\w*(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:=[^;]*)?;')) {
   $name = $field.Groups[3].Value
   if ($field.Groups[2].Success) { Assert ($name -cmatch '^[A-Z][A-Z0-9_]*$') "constant $name in $($class.Groups[1].Value) is upper case"; continue }
   if ($field.Groups[1].Success) { Assert ($name -cmatch '^s_') "static field $name in $($class.Groups[1].Value) has the s_ prefix"; continue }
   Assert ($name -cmatch '^m_') "field $name in $($class.Groups[1].Value) has the m_ prefix"
  }
 }
 foreach ($method in [regex]::Matches($code, '(?m)^\s*(?:(?:static|protected|override|private)\s+)*(?:void|bool|int|float|string|vector|IEntity|\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*\([^;{]*\)\s*\{')) {
  $open = $method.Index + $method.Length - 1; $depth = 0; $end = $open
  for ($i = $open; $i -lt $code.Length; $i++) { if ($code[$i] -eq '{') { $depth++ } elseif ($code[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } } }
  $body = $code.Substring($open, $end - $open)
  $declared = @([regex]::Matches($body, '(?m)(?:^\s*|[;{(]\s*|\bforeach\s*\(\s*)(?:ref\s+)?(?:int|float|bool|string|vector|IEntity|[A-Z]\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:\[\d+\])?\s*(?:=|;|:)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin 'return', 'new' })
  $dupes = @($declared | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Assert ($dupes.Count -eq 0) "variable declared twice in $($method.Groups[1].Value) of ${path}: $($dupes -join ', ')"
  foreach ($local in $declared) { Assert ($local -cmatch '^[a-z]') "local $local in $($method.Groups[1].Value) of $path starts lower case" }
 }
}
Assert (@([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'this guard is ASCII with LF line endings'

# Native round trip wiring (Run-CdfRoundTrip.ps1, orchestrator only).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG CREW CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 seats=[4-9] seated=[4-9] adopted=[2-9] linked=[4-9] chair=(1|skipped) items=[4-9] fallback=1 noDuplicates=1 legacy=1 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EVC_CDFCrewRoundTrip.c -ExpectResult '$expect' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'round-trip fixture header carries its runner command'
Assert ($fixture -match 'class\s+EXPG_CdfRoundTripClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_CdfRoundTrip\s*:\s*GenericEntity') 'round-trip fixture keeps the runner driver class name'
foreach ($needle in '{3EA6F47D95867114}Prefabs/Vehicles/Wheeled/M998/M1025_armed_M2HB.et', '{73530808E4B455BA}Prefabs/Weapons/Tripods/Tripod_M3_M2HB.et', '{4AC034517E87747E}Prefabs/GM_Props/Office_chair_GM.et', 'editable.OccupyVehicleWithDefaultCharacters(crew);', 'CDF_GMSaveCapture.Capture("evc-cdf-roundtrip", "fixture")', 'document.SaveToFile(FILE)', 'm_Loaded.LoadFromFile(FILE)', 'CDF_GMSaveRestore.Restore(m_Loaded)', 'CDF_GMSaveRestore.Restore(m_Moved)', 'CDF_GMSaveRestore.Restore(m_Legacy)', 'cfg.m_bSaveCharacterInventories = true;', 'cfg.m_bClearBeforeLoad = true;', 'cfg.m_bCaptureOnlyAuthored = true;', 'EVC_CDF.Encode(seat.m_iType, 99, true,', 'record.m_iTarget = CDF_GMSaveEntityRecord.TARGET_NONE;', 'NoDuplicates("nobody duplicated by the load");', 'KnowsVehicle(group, holder)', 'if (chairResource && chairResource.IsValid())') {
 Assert $fixture.Contains($needle) "round-trip fixture must drive: $needle"
}
foreach ($field in 'ref CDF_GMSaveDocument m_Loaded;', 'ref CDF_GMSaveDocument m_Moved;', 'ref CDF_GMSaveDocument m_Legacy;', 'ref array<ref EVC_CdfExpect> m_aExpect') { Assert $fixture.Contains($field) "fixture keeps released objects alive: $field" }
$capturePhase = Get-Body $fixture 'void\s+CaptureAndLoad\s*\(\s*\)'
Assert ($capturePhase.IndexOf('Expect(m_Vehicle, false);') -ge 0 -and $capturePhase.IndexOf('Expect(m_Vehicle, false);') -lt $capturePhase.IndexOf('CDF_GMSaveCapture.Capture(')) 'every seat and its items are snapshotted in the frame of the save'
Assert ($fixture.Contains('PrintFormat("[EXPG CREW CDF ROUNDTRIP RESULT] %1 %2", first, second);')) 'round-trip fixture prints its result line'
'PASS: Vehicle Crew CDF bridge: evcSeat key (lossless, ignored by older readers) and CDF crew link, default crews added as CDF records with the vehicle''s author, refusals before CDF changes the scene, states untouched at load, exclusions (players, prisoners, garrison, reserved groups, ACE helpers), Clear predicate, bounded seating without spawning, summary line, Enforce gotchas; round-trip fixture wired.'
