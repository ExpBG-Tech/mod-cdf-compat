#requires -Version 7.0
# Portable guard for the prisoner bridge (addon/ai-surrender-cdf, EPR_CDFPrisoners.c): EXPBG AI
# Surrender prisoners and ACE Captives surrendered/handcuffed soldiers are saved with CDF and come
# back as prisoners (until 0.1.10 prisoners were session-only: left out of every save and removed
# by a load). Checks the envelope keys and order, unwrap before super, refusals before CDF clears
# the scene, the record added inside CDF's Capture, the spawn-time hold, the bounded pump after
# CDF's deferred pass, the one-line load summary, legacy clearing, players never touched, ACE
# reached by name only, the GM Tools API surface, Enforce gotchas and the native fixture wiring.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$moduleRoot = "$repo/addon/ai-surrender-cdf"
$bridgePath = "$moduleRoot/Scripts/Game/EXPSR_CDF/EPR_CDFPrisoners.c"
$fixturePath = "$repo/tests/EPR_CDFPrisonersRoundTrip.c"
function Assert([bool]$Condition, [string]$Message) { if (!$Condition) { throw "FAIL: $Message" } }
function Read-Text([string]$Path) { [IO.File]::ReadAllText($Path) }
function Get-Body([string]$Text, [string]$Signature) {
 $match = [regex]::Match($Text, $Signature)
 Assert $match.Success "signature not found: $Signature"
 $open = $Text.IndexOf('{', $match.Index); $depth = 0
 for ($i = $open; $i -lt $Text.Length; $i++) {
  if ($Text[$i] -eq '{') { $depth++ } elseif ($Text[$i] -eq '}') { $depth--; if ($depth -eq 0) { return $Text.Substring($open + 1, $i - $open - 1) } }
 }
 throw "FAIL: unbalanced body for $Signature"
}
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

# Module identity: developed here, one bridge script, the session-only file retired.
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
$entry = @($pack.modules | Where-Object { $_.name -ceq 'ai-surrender-cdf' })
Assert ($entry.Count -eq 1 -and $entry[0].origin.repository -ceq 'ExpBG-Tech/mod-cdf-compat' -and !$entry[0].files) 'ai-surrender-cdf is a module developed in this repository'
Assert ($entry[0].origin.note.Contains('eprPrisoner') -and $entry[0].origin.note.Contains('IsCaptiveSeat') -and !$entry[0].origin.note.Contains('session-only under CDF')) 'pack.json describes prisoner persistence, not the retired session-only rule'
$files = @(Get-ChildItem -LiteralPath $moduleRoot -Recurse -File | ForEach-Object { $_.FullName.Substring($moduleRoot.Length + 1).Replace('\', '/') })
Assert ($files.Count -eq 1 -and $files[0] -ceq 'Scripts/Game/EXPSR_CDF/EPR_CDFPrisoners.c') "the module holds only its bridge script: $($files -join ', ')"
$bridge = Read-Text $bridgePath
$code = Get-Code $bridge
Assert (!$bridge.Contains('RemovePrisoners') -and !$bridge.Contains('session-only)')) 'prisoners are no longer stripped from saves'

# Envelope keys and versions (saved-data keys: never rename).
foreach ($needle in 'static const string KEY = "eprPrisoner";', 'static const string SQUAD_KEY = "eprSquad";', 'static const string INNER = "cdfState";', 'static const int VERSION = 1;', 'static const int SQUAD_VERSION = 1;', 'static const int PEEL_LIMIT = 8;') {
 Assert $bridge.Contains($needle) "envelope: $needle"
}
foreach ($key in '"v"', '"esr"', '"side"', '"att"', '"out"', '"rc"', '"rd"', '"rb"', '"ir"', '"idist"', '"ibear"', '"sqo"', '"aceFail"', '"squad"', '"armed"', '"as"', '"ac"', '"acr"', '"ai"', '"dnf"', '"dn"', '"dbio"', '"dage"', '"dldr"', '"t"') {
 Assert $bridge.Contains("WriteValue($key") "payload key written: $key"
}
$decode = Get-Body $bridge 'static\s+EPR_State\s+Decode\s*\(\s*string\s+text,\s*out\s+string\s+reason\s*\)'
Assert ($decode.Contains('version != VERSION') -and $decode.Contains('reason = state.Validate();')) 'a payload of another version or out of range is not decoded'

# Per-entity capture: the inner chain first, then this envelope around it.
$stateClass = Get-Body $bridge 'modded\s+class\s+CDF_GMSaveState'
$capture = Get-Body $stateClass 'override\s+static\s+string\s+Capture\s*\(\s*IEntity\s+entity\s*\)'
Assert-Order $capture @('string original = super.Capture(entity);', 'EPR_CDF.EnvelopeFor(entity, key)', 'return original;', 'context.WriteValue(key, payload) && context.WriteValue(EPR_CDF.INNER, original)', 'EPR_CDF.FailCapture(', 'return wrapped;') 'Capture'
$apply = Get-Body $stateClass 'override\s+static\s+void\s+Apply\s*\(\s*IEntity\s+entity,\s*string\s+state\s*\)'
Assert ($apply.Contains('EPR_CDF.Locate(state, EPR_CDF.KEY, 1, payload, original)') -and $apply.Contains('EPR_CDF.Locate(state, EPR_CDF.SQUAD_KEY, 1, payload, original)')) 'Apply unwraps only its outermost envelope'
Assert ($apply.IndexOf('super.Apply(entity, original);') -ge 0 -and $apply.IndexOf('super.Apply(entity, original);') -lt $apply.IndexOf('EPR_CDF.Queue(entity, payload);') -and $apply.IndexOf('super.Apply(entity, original);') -lt $apply.IndexOf('EPR_CDF.RegisterSquad(entity, payload);')) 'inventory, damage and other envelopes reach CDF before the prisoner is queued'
$broken = [regex]::Match($apply, '(?s)if \(found < 0\)\s*\{(.*?)\n\s*\}')
Assert ($broken.Success -and !$broken.Groups[1].Value.Contains('super.Apply') -and $broken.Groups[1].Value.Contains('LogLevel.WARNING')) 'a broken envelope is never handed to CDF as plain state (warning only)'
$locate = Get-Body $bridge 'static\s+int\s+Locate\s*\(\s*string\s+state,\s*string\s+key,\s*int\s+levels,\s*out\s+string\s+payload,\s*out\s+string\s+original\s*\)'
Assert ($locate.Contains('context.ReadValue(INNER, original)') -and $locate.Contains('text.Contains("\"" + key + "\":")') -and $locate.Contains('context.ReadValue(INNER, inner)')) 'Locate peels other adapters'' cdfState envelopes and refuses a wrongly typed key'

# Document capture: records added inside CDF's Capture, refusals return no document.
$captureClass = Get-Body $bridge 'modded\s+class\s+CDF_GMSaveCapture'
$captureDoc = Get-Body $captureClass 'override\s+static\s+CDF_GMSaveDocument\s+Capture\s*\(\s*string\s+displayName,\s*string\s+author\s*\)'
Assert-Order $captureDoc @('EPR_CDF.BeginCapture();', 'super.Capture(displayName, author);', 'EPR_CDF.EndCapture(document);', 'return null;', 'EPR_CDF.s_bCaptureFailed', 'EPR_CDF.ValidDocument(document, false, prisoners, squads, reason)', 'return null;', 'EPR_CDF.MoveOutOfVehicles(document);', 'return document;') 'document Capture'
$managed = Get-Body $captureClass 'override\s+static\s+bool\s+IsManaged\s*\(\s*SCR_EditableEntityComponent\s+entity\s*\)'
Assert ($managed.Contains('super.IsManaged(entity)') -and $managed.Contains('EPR_CDF.ManagedPrisoner(entity)')) 'Clear removes exactly the prisoners a save keeps'
$document = Get-Body $bridge 'modded\s+class\s+CDF_GMSaveDocument'
Assert ($document.Contains('override int GetEntityCount()') -and $document.Contains('if (EPR_CDF.s_bInjectArmed) EPR_CDF.Inject(this);') -and $document.Contains('return super.GetEntityCount();')) 'prisoner records are added at CDF Capture''s closing GetEntityCount call'
$endCapture = Get-Body $bridge 'static\s+void\s+EndCapture\s*\(\s*CDF_GMSaveDocument\s+document\s*\)'
Assert ($endCapture.Contains('if (s_bInjectArmed && document) Inject(document);') -and $endCapture.Contains('s_bInjectArmed = false;')) 'the records are still added if CDF never reached that call'
$inject = Get-Body $bridge 'static\s+void\s+Inject\s*\(\s*CDF_GMSaveDocument\s+document\s*\)'
Assert-Order $inject @('s_bInjectArmed = false;', 'present.Insert(record.m_Entity)', 'ESR_SurrenderManager.IsPlayerCharacter(prisoner.Character)', 'present.Contains(editable)', 'CDF_GMSaveCapture.IsManaged(editable)', 'BuildRecord(editable)', 'document.m_aEntities.Insert(added);') 'Inject'
$record = Get-Body $bridge 'static\s+CDF_GMSaveEntityRecord\s+BuildRecord\s*\(\s*SCR_EditableEntityComponent\s+editable\s*\)'
foreach ($field in 'editable.Serialize(target, targetValue, saveFlags)', 'record.m_iParent = -1;', 'record.m_sPrefab = editable.GetPrefab(true);', 'record.m_iEntityType = editable.GetEntityType();', 'Math3D.MatrixToQuat(transform, quat);', 'record.m_sAuthorUID = editable.GetAuthorUID();', 'CDF_GMSaveAttributes.Read(editable,', 'record.m_sState = CDF_GMSaveState.Capture(owner);') {
 Assert $record.Contains($field) "added records are built like CDF's own: $field"
}

# Load: refusal before the clear, spawn-time hold, bounded pass after CDF's deferred pass.
$restoreClass = Get-Body $bridge 'modded\s+class\s+CDF_GMSaveRestore'
$restore = Get-Body $restoreClass 'override\s+static\s+bool\s+Restore\s*\(\s*notnull\s+CDF_GMSaveDocument\s+document\s*\)'
Assert-Order $restore @('EPR_CDF.PrepareLoad(document, reason)', 'EPR_CDF.Reject(reason + "; the scene was not cleared");', 'return false;', 'int live = EPR_CDF.LiveCount();', 'EPR_CDF.s_bSpawning = true;', 'bool result = super.Restore(document);', 'EPR_CDF.s_bSpawning = false;', 'EPR_CDF.ReleaseRemoved(live);', 'EPR_CDF.EndSpawn(result);') 'Restore'
$deferred = Get-Body $restoreClass 'static\s+bool\s+EPR_DeferredDone\s*\(\s*\)'
Assert ($deferred.Contains('!s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty()')) 'the pass waits for CDF''s deferred state pass (FinishRestore)'
$valid = Get-Body $bridge 'static\s+bool\s+ValidDocument\s*\(\s*CDF_GMSaveDocument\s+document,\s*bool\s+forLoad,\s*out\s+int\s+prisoners,\s*out\s+int\s+squads,\s*out\s+string\s+reason\s*\)'
Assert ($valid.Contains('Locate(record.m_sState, KEY, PEEL_LIMIT, payload, original)') -and $valid.Contains('if (found < 0)') -and $valid.Contains('if (!forLoad && (!state || !character))') -and $valid.Contains('EEditableEntitySaveFlag.DESTROYED') -and $valid.Contains('if (forLoad) s_aHold.Insert(record);')) 'a save refuses any undecodable prisoner; a load refuses only a broken envelope'
Assert ($valid.Contains('EII_CDFPayload.ValidDocument(carriers)')) 'Intel Items state hidden under the envelope still passes Intel''s own check'
$hook = Get-Body $bridge 'modded\s+class\s+SCR_EditableCharacterComponent'
Assert-Order $hook @('override void EOnEditorSessionLoad(SCR_EditableEntityComponent parent)', 'super.EOnEditorSessionLoad(parent);', 'if (EPR_CDF.s_bSpawning) EPR_CDF.HoldSpawned(this);') 'spawn-time hold'
$hold = Get-Body $bridge 'static\s+bool\s+Hold\s*\(\s*IEntity\s+entity\s*\)'
Assert-Order $hold @('ESR_SurrenderManager.IsPlayerCharacter(character)', 'return false;', 'control.DeactivateAI();', 'SetCivilian(character);') 'Hold'
$pump = Get-Body $bridge 'static\s+void\s+Pump\s*\(\s*\)'
Assert-Order $pump @('CDF_GMSaveRestore.EPR_DeferredDone()', 's_iWaits < MAX_WAITS', 'worked < PER_PUMP', 'RestoreEntry(entry, reason)', 'entry.m_iTries < MAX_TRIES', 'Finish(false);', 'Schedule(PUMP_MS);') 'Pump'
foreach ($needle in 'static const int PER_PUMP = 4;', 'static const int PUMP_MS = 100;', 'static const int MAX_TRIES = 60;', 'static const int MAX_WAITS = 300;') { Assert $bridge.Contains($needle) "bounded work: $needle" }
Assert ($bridge.Contains('"[EXPBG CDF PRISONERS] restored=%1 esr=%2 ace=%3 failed=%4 expected=%5 held=%6 squads=%7 released=%8 interrupted=%9"')) 'one load summary line in the agreed format'
$finish = Get-Body $bridge 'static\s+void\s+Finish\s*\(\s*bool\s+interrupted\s*\)'
Assert ($finish.Contains('severity = LogLevel.WARNING;') -and !$code.Contains('LogLevel.ERROR')) 'expected refusals and failures log as warnings (the round-trip runner fails on SCRIPT (E))'
$prepare = Get-Body $bridge 'static\s+bool\s+PrepareLoad\s*\(\s*CDF_GMSaveDocument\s+document,\s*out\s+string\s+reason\s*\)'
Assert-Order $prepare @('if (s_bActive) Finish(true);', 'ResetCounters();', 'ValidDocument(document, true, prisoners, squads, reason)') 'a new load ends the previous pass before checking'

# Restoring: AI Surrender's own surrender, no re-arming, saved record, players never touched.
$esr = Get-Body $bridge 'protected\s+static\s+int\s+RestoreEsr\s*\('
Assert-Order $esr @('ESR_SurrenderManager.FindPrisoner(character)', 'RemoveWeapons(character, state.m_aArmed);', 'ESR_SurrenderManager.Surrender(character, ESR_SurrenderManager.GroupOf(character))', 'ApplyEsr(character, prisoner, state);', 'Track(character);', 'SetAceCaptive(character)') 'RestoreEsr'
$applyEsr = Get-Body $bridge 'protected\s+static\s+void\s+ApplyEsr\s*\('
foreach ($field in 'prisoner.Attempts = state.m_iAttempts;', 'prisoner.Outcome = state.m_iOutcome;', 'prisoner.MarkerId = -1;', 'prisoner.IntelMarkers.Insert(-1);', 'prisoner.SquadOverrides.Insert(value);', 'prisoner.Group = squad;', 'prisoner.Point.Setup(rpl.Id(), prisoner.Dossier);') { Assert $applyEsr.Contains($field) "saved prisoner record restored: $field" }
$weapons = Get-Body $bridge 'static\s+int\s+RemoveWeapons\s*\('
Assert ($weapons.Contains('inventory.TryDeleteItem(weapon)') -and !$weapons.Contains('TryRemoveItemFromStorage')) 'weapons beyond the save are deleted, never dropped as loot'
$entryBody = Get-Body $bridge 'protected\s+static\s+int\s+RestoreEntry\s*\('
Assert ($entryBody.Contains('ESR_SurrenderManager.IsPlayerCharacter(character)') -and $entryBody.IndexOf('IsPlayerCharacter') -lt $entryBody.IndexOf('RestoreEsr(')) 'a player-controlled body is never restored as a prisoner'
foreach ($name in 'CaptureState', 'Unhold') {
 $body = Get-Body $bridge ('static\s+\w+\s+' + $name + '\s*\(')
 Assert $body.Contains('ESR_SurrenderManager.IsPlayerCharacter(character)') "players are never touched in $name"
}
$legacy = Get-Body $bridge 'static\s+void\s+ReleaseRemoved\s*\(\s*int\s+liveBefore\s*\)'
Assert ($legacy.Contains('ESR_SurrenderManager.Release(prisoner, "removed by a CDF load")')) 'saves without prisoner data: removed prisoners release their interrogation points'
$seat = Get-Body $bridge 'static\s+bool\s+IsCaptiveSeat\s*\(\s*IEntity\s+character\s*\)'
Assert-Order $seat @('InAceHelper(chimera)', 'ESR_SurrenderManager.FindPrisoner(chimera)', 'ESR_AceCaptives.ReadState(chimera, surrendered, captive, carried)') 'IsCaptiveSeat (shared rule with the vehicle crew bridge)'

# ACE stays optional: no ACE class, method or enum is named in compiled code.
$aceWords = @([regex]::Matches($code, '\bACE_\w+') | ForEach-Object Value | Sort-Object -Unique)
Assert ($aceWords.Count -eq 0) "ACE is reached by name only: $($aceWords -join ', ')"
foreach ($needle in 'static const string FN_SET_CAPTIVE = "ACE_Captives_SetCaptive";', 'scripts.Call(controller, FN_SET_CAPTIVE, false, unused, requested);', 'string helperName = ESR_AceCaptives.TYPE_HELPER;', 's_tHelper = helperName.ToType();') {
 Assert $bridge.Contains($needle) "ACE runtime lookup: $needle"
}
# GM Tools API used: the public AI Surrender manager, prisoner record and ACE adapter only.
$allowed = 'ESR_SurrenderManager.PrisonerCount', 'ESR_SurrenderManager.FindPrisoner', 'ESR_SurrenderManager.GetPrisonerAt', 'ESR_SurrenderManager.IsPlayerCharacter', 'ESR_SurrenderManager.Surrender', 'ESR_SurrenderManager.GroupOf', 'ESR_SurrenderManager.Release', 'ESR_SurrenderManager.CIVILIAN_FACTION', 'ESR_SurrenderManager.OUTCOME_NO_SQUAD', 'ESR_AceCaptives.Available', 'ESR_AceCaptives.ReadState', 'ESR_AceCaptives.SetSurrender', 'ESR_AceCaptives.TYPE_HELPER', 'ESR_Overrides.COUNT', 'ESR_Prisoner', 'ESR_Dossier', 'ESR_InterrogationPoint'
foreach ($symbol in @([regex]::Matches($code, '\bESR_\w+(?:\.\w+)?') | ForEach-Object Value | Sort-Object -Unique)) {
 Assert ($symbol -cin $allowed) "bridge uses only public AI Surrender API: $symbol"
}

# Enforce gotchas in the bridge and its fixture.
foreach ($path in @($bridgePath, $fixturePath)) {
 $text = Read-Text $path
 $plain = Get-Code $text
 Assert (![regex]::IsMatch($plain, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external|native)\b')) "reserved Enforce name used as a variable in $path"
 Assert (![regex]::IsMatch($plain, '\b[A-Za-z_]\w*(?:<[^>]*>)?\s+vanilla\s*[=;,)]')) "a local named vanilla in $path"
 Assert (!$plain.Contains('Math.RandomFloat(')) "Math.RandomFloat in $path"
 Assert (![regex]::IsMatch($plain, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "static initializer in $path"
 Assert (![regex]::IsMatch($plain, '\bif\s*\([^\r\n]*\)\s*return\b|\belse\s+return\b')) "each return on its own line in $path"
 Assert (![regex]::IsMatch($plain, 'SpawnEntityPrefab\s*\(\s*Resource\.Load')) "Resource.Load result kept in a local in $path"
 Assert (![regex]::IsMatch($plain, '(?m)\bint\s+\w+\s*=\s*[^;\r\n]*(?:&&|\|\||==|!=|\btrue\b|\bfalse\b)[^;\r\n]*;')) "implicit bool to int in $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "$path must be ASCII with LF line endings"
 foreach ($method in [regex]::Matches($plain, '(?m)^\s*(?:(?:static|protected|override|private)\s+)*(?:void|bool|int|float|string|vector|IEntity|\w+)\s+(\w+)\s*\([^;{]*\)\s*\{')) {
  $open = $method.Index + $method.Length - 1; $depth = 0; $end = $open
  for ($i = $open; $i -lt $plain.Length; $i++) { if ($plain[$i] -eq '{') { $depth++ } elseif ($plain[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } } }
  $body = $plain.Substring($open, $end - $open)
  $declared = @([regex]::Matches($body, '(?m)(?:^\s*|[;{(]\s*|\bforeach\s*\(\s*)(?:ref\s+)?(?:int|float|bool|string|vector|IEntity|[A-Z]\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:\[\d+\])?\s*(?:=|;|:)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin 'return', 'new' })
  $dupes = @($declared | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Assert ($dupes.Count -eq 0) "variable declared twice in $($method.Groups[1].Value) of ${path}: $($dupes -join ', ')"
 }
}
# Members and statics keep their prefixes.
foreach ($match in [regex]::Matches((Get-Body $bridge 'class\s+EPR_CDF\s*'), '(?m)^ (?:protected\s+)?static\s+(?!const\b)(?:ref\s+)?[\w<>, ]+?\s+(\w+)\s*[;,]')) {
 Assert ($match.Groups[1].Value -cmatch '^s_') "static field without s_ prefix: $($match.Groups[1].Value)"
}
Assert (@([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'this guard is ASCII with LF line endings'

# Native round trip wiring (Run-CdfRoundTrip.ps1, orchestrator only).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG PRISONER CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 saved=(?:2 restored=2 esr=2 ace=skipped held=2|3 restored=3 esr=2 ace=1 held=3) duplicates=0 brokenRefused=1 legacyCleared=1 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EPR_CDFPrisonersRoundTrip.c -ExpectResult '$expect' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'round-trip fixture header carries its runner command'
$runner = Read-Text "$repo/tests/Run-CdfRoundTrip.ps1"
Assert ($runner -match 'ValidateRange\(300,\s*600\)') 'the runner keeps a timeout of at least 300 seconds'
Assert ($fixture -match 'class\s+EXPG_CdfRoundTripClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_CdfRoundTrip\s*:\s*GenericEntity') 'round-trip fixture keeps the runner driver class name'
Assert ($fixture.Contains('static const float FIXTURE_SECONDS = 300;')) 'fixture deadline of 300 seconds'
foreach ($field in 'ref CDF_GMSaveDocument m_Loaded;', 'ref CDF_GMSaveDocument m_Broken;', 'ref CDF_GMSaveDocument m_Stripped;', 'ref array<ref EPR_FixtureExpect> m_aExpect') { Assert $fixture.Contains($field) "fixture keeps released objects by ref: $field" }
Assert (![regex]::IsMatch((Get-Code $fixture), '(?m)^\s*ESR_Prisoner\s+m_')) 'the fixture never keeps a prisoner record the manager may release'
foreach ($needle in 'ESR_SurrenderManager.Surrender(plain, m_Team)', 'ESR_SurrenderManager.Surrender(questioned, m_Team)', 'prisoner.Outcome = ESR_SurrenderManager.OUTCOME_REVEAL;', 'CDF_GMSaveCapture.Capture("epr-cdf-roundtrip", "fixture")', 'document.SaveToFile(FILE)', 'm_Loaded.LoadFromFile(FILE)', 'brokenRecord.m_sState = "{\"eprPrisoner\":\"x\"}";', 'bool refused = !CDF_GMSaveRestore.Restore(m_Broken);', 'CDF_GMSaveRestore.Restore(m_Loaded)', 'm_iHeld = EPR_CDF.s_iHeld;', 'CDF_GMSaveRestore.Restore(m_Stripped)', 'EPR_CDF.IsCaptiveSeat(', 'cfg.m_bClearBeforeLoad = true;', 'm_Team.EBG_Exclude = true;') {
 Assert $fixture.Contains($needle) "round-trip fixture must drive: $needle"
}
Assert ($fixture.Contains('m_bAce = ESR_AceCaptives.Available();') -and $fixture.Contains('[EXPG PRISONER CDF ROUNDTRIP ACE] skipped:')) 'the ACE case skips itself with one line when ACE Captives is not loaded'
Assert ($fixture.Contains('PrintFormat("[EXPG PRISONER CDF ROUNDTRIP RESULT] %1 %2", first, second);')) 'round-trip fixture prints its result line'
'PASS: prisoner bridge: eprPrisoner/eprSquad envelopes, unwrap before super, records added inside CDF Capture, refusals before the clear, spawn-time hold, bounded pass after CDF''s deferred pass, one summary line, legacy clearing, players untouched, ACE by name only, public AI Surrender API, Enforce gotchas; round-trip fixture wired (ACE case self-skipping).'
