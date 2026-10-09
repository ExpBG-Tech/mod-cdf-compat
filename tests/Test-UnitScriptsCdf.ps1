#requires -Version 7.0
# Portable guard for the Unit Scripts CDF bridge (addon/unit-scripts-cdf): every scripted
# soldier's Hold, Freeze or ambient animation travels in his CDF state through the
# versioned GM Tools state API (EUS_UnitState) and comes back after CDF's deferred state
# pass (production 2026-10-08: after a CDF load every frozen, held and animated soldier
# ran around). Checks the envelope keys and order, unwrap before super, refusals before
# CDF changes anything, peeling under other adapters, the Intel Items re-check, the
# finalization polling, Enforce gotchas and the native round-trip fixture wiring.
# No engine is launched.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bridgePath = "$repo/addon/unit-scripts-cdf/Scripts/Game/EXPUS_CDF/EUS_CDFState.c"
$fixturePath = "$repo/tests/EUS_CDFScriptsRoundTrip.c"
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
$bridge = Read-Text $bridgePath
$pack = Get-Content -LiteralPath "$repo/tools/pack.json" -Raw | ConvertFrom-Json -AsHashtable
$entry = @($pack.modules | Where-Object { $_.name -ceq 'unit-scripts-cdf' })
Assert ($entry.Count -eq 1 -and $entry[0].origin.repository -ceq 'ExpBG-Tech/mod-cdf-compat' -and !$entry[0].files) 'unit-scripts-cdf is a module developed in this repository'
Assert (@(Get-ChildItem -LiteralPath "$repo/addon/unit-scripts-cdf" -Recurse -File).Count -eq 1) 'the module holds only its bridge script'

# Envelope and API.
foreach ($needle in 'static const string KEY = "eusScript";', 'static const string SOURCE = "CDF save";', 'static const int PEEL_LIMIT = 6;') { Assert $bridge.Contains($needle) "envelope: $needle" }
$capture = Get-Body $bridge 'override\s+static\s+string\s+Capture\s*\(\s*IEntity\s+entity\s*\)'
$order = @('string original = super.Capture(entity);', 'EUS_UnitState state = EUS_UnitState.Capture(entity);', 'return original;', 'string payload = state.Encode();', 'EUS_CDF.CaptureFailed = true;', 'context.WriteValue(EUS_CDF.KEY, payload) && context.WriteValue("cdfState", original)', 'return wrapped;')
$from = 0
foreach ($step in $order) { $at = $capture.IndexOf($step, $from); Assert ($at -ge 0) "Capture lost or reordered: $step"; $from = $at + $step.Length }
$apply = Get-Body $bridge 'override\s+static\s+void\s+Apply\s*\(\s*IEntity\s+entity,\s*string\s+state\s*\)'
Assert ($apply.Contains('found = EUS_CDF.Locate(state, 1, payload, original);') -and $apply.Contains('super.Apply(entity, state);') -and $apply.Contains('super.Apply(entity, original);')) 'Apply unwraps only its outermost envelope and hands the rest on'
Assert ($apply.IndexOf('super.Apply(entity, original);') -lt $apply.IndexOf('EUS_UnitState.Decode(payload, reason)') -and $apply.Contains('saved.Restore(entity, EUS_CDF.SOURCE, reason)')) 'the inner chain (inventory, damage, dialog, intel) is applied before the script is queued'
$broken = [regex]::Match($apply, '(?s)if \(found < 0\)\s*\{(.*?)\n\s*\}')
Assert ($broken.Success -and !$broken.Groups[1].Value.Contains('super.Apply')) 'a broken envelope is never handed to CDF as plain state'
$locate = Get-Body $bridge 'static\s+int\s+Locate\s*\(\s*string\s+state,\s*int\s+levels,\s*out\s+string\s+payload,\s*out\s+string\s+original\s*\)'
Assert ($locate.Contains('context.ReadValue("cdfState", original)') -and $locate.Contains('text.Contains("\"" + KEY + "\":")') -and $locate.Contains('context.ReadValue("cdfState", inner)')) 'Locate peels other adapters'' cdfState envelopes and refuses a wrongly typed key'

# Document checks and refusals.
$valid = Get-Body $bridge 'static\s+bool\s+ValidDocument\s*\(\s*CDF_GMSaveDocument\s+document,\s*bool\s+forLoad,\s*out\s+int\s+payloads,\s*out\s+int\s+skipped,\s*out\s+string\s+reason\s*\)'
Assert ($valid.Contains('Locate(record.m_sState, PEEL_LIMIT, payload, original)') -and $valid.Contains('if (found < 0)') -and $valid.Contains('if (!forLoad && (!decoded || !character))') -and $valid.Contains('EEditableEntitySaveFlag.DESTROYED')) 'a save refuses any undecodable script; a load refuses only a broken envelope and skips an unreadable script'
Assert ($valid.Contains('EII_CDFPayload.ValidDocument(carriers)')) 'Intel Items state hidden under the envelope still passes Intel''s own check'
$restore = Get-Body $bridge 'override\s+static\s+bool\s+Restore\s*\(\s*notnull\s+CDF_GMSaveDocument\s+document\s*\)'
$super = $restore.IndexOf('bool result = super.Restore(document);')
Assert ($super -gt 0 -and $restore.IndexOf('EUS_CDF.ValidDocument(document, true, payloads, skipped, reason)') -lt $super -and $restore.IndexOf('return false;') -lt $super -and $restore.IndexOf('EUS_CDF.ResetLoad();') -lt $super) 'a load refusal comes before CDF clears the scene'
$captureDoc = Get-Body $bridge 'override\s+static\s+CDF_GMSaveDocument\s+Capture\s*\(\s*string\s+displayName,\s*string\s+author\s*\)'
Assert ($captureDoc.IndexOf('EUS_CDF.CaptureFailed = false;') -lt $captureDoc.IndexOf('super.Capture(displayName, author)') -and $captureDoc.Contains('EUS_CDF.ValidDocument(document, false, payloads, skipped, reason)') -and $captureDoc.Contains('return null;')) 'a save with an unencodable script is not written'
$finish = Get-Body $bridge 'protected\s+static\s+void\s+EUS_FinishLoad\s*\(\s*\)'
Assert ($finish.Contains('!s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty()') -and $finish.Contains('EUS_CDF.FinishAttempts < 20') -and $finish.Contains('[EUS CDF LOAD] queued=%1 failed=%2 skipped=%3 expected=%4 complete=%5')) 'the load summary waits (bounded) for CDF''s deferred state pass'
Assert ($bridge.Contains('[CDF TIMING] unit-scripts restore path=scan') -and $bridge.Contains('[CDF TIMING] unit-scripts apply calls=')) 'CDF timing lines like the other adapters'
Assert ($bridge.Contains('override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)') -and $bridge.Contains('refused by EXPBG Unit Scripts') -and $bridge.Contains('class EUS_CDFRefusalDialog : CDF_GMSaveBaseDialog')) 'the Game Master sees why a save or load was refused'
# GM Tools API used: the versioned state API and existing manager getters only.
$symbols = @([regex]::Matches((Get-Code $bridge), '\bEUS_\w+(?:\.\w+)?') | ForEach-Object Value | Sort-Object -Unique)
$allowed = 'EUS_CDF', 'EUS_CDFRefusalDialog', 'EUS_UnitState', 'EUS_UnitState.Capture', 'EUS_UnitState.Decode', 'EUS_Manager', 'EUS_Manager.Current', 'EUS_UnitControl', 'EUS_FinishLoad', 'EUS_CDF_RpcDo_Refused'
foreach ($symbol in $symbols) {
 $root = $symbol.Split('.')[0]
 Assert ($symbol -cin $allowed -or $root -ceq 'EUS_CDF' -or $root -ceq 'EUS_CDFRefusalDialog') "bridge uses only the state API and existing manager getters: $symbol"
}

# Enforce gotchas in the bridge and its fixture.
foreach ($path in @($bridgePath, $fixturePath)) {
 $text = Read-Text $path
 $code = Get-Code $text
 Assert (![regex]::IsMatch($code, '\b(int|float|bool|string|vector|auto|IEntity)\s+(owned|Sleep|Wait|external|native)\b')) "reserved Enforce name used as a variable in $path"
 Assert (!$code.Contains('Math.RandomFloat(')) "Math.RandomFloat in $path"
 Assert (![regex]::IsMatch($code, 'static\s+(const\s+)?ref\s+[^;=]+=|static\s+const\s+array<')) "static initializer in $path"
 Assert (![regex]::IsMatch($code, '\bif\s*\([^\r\n]*\)\s*return\b')) "each return on its own line in $path"
 Assert (@([IO.File]::ReadAllBytes($path) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) "$path must be ASCII with LF line endings"
 foreach ($method in [regex]::Matches($code, '(?m)^\s*(?:(?:static|protected|override|private)\s+)*(?:void|bool|int|float|string|vector|IEntity|\w+)\s+(\w+)\s*\([^;{]*\)\s*\{')) {
  $open = $method.Index + $method.Length - 1; $depth = 0; $end = $open
  for ($i = $open; $i -lt $code.Length; $i++) { if ($code[$i] -eq '{') { $depth++ } elseif ($code[$i] -eq '}') { $depth--; if ($depth -eq 0) { $end = $i; break } } }
  $body = $code.Substring($open, $end - $open)
  $declared = @([regex]::Matches($body, '(?m)(?:^\s*|[;{(]\s*|\bforeach\s*\(\s*)(?:ref\s+)?(?:int|float|bool|string|vector|IEntity|[A-Z]\w+(?:<[\w<>, ]+>)?)\s+(\w+)\s*(?:\[\d+\])?\s*(?:=|;|:)') | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin 'return', 'new' })
  $dupes = @($declared | Group-Object | Where-Object Count -GT 1 | ForEach-Object Name)
  Assert ($dupes.Count -eq 0) "variable declared twice in $($method.Groups[1].Value) of ${path}: $($dupes -join ', ')"
 }
}
Assert (@([IO.File]::ReadAllBytes($PSCommandPath) | Where-Object { $_ -gt 127 -or $_ -eq 13 }).Count -eq 0) 'this guard is ASCII with LF line endings'

# Native round trip wiring (Run-CdfRoundTrip.ps1, orchestrator only).
$fixture = Read-Text $fixturePath
$expect = '\[EXPG EUS CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 saved=3 restored=3 posed=1 plain=1 inventories=4 cdfLoss=\d+ brokenRefused=1 invalidSkipped=1 reason=completed'
Assert ($fixture.Contains("-FixturePath tests/EUS_CDFScriptsRoundTrip.c -ExpectResult '$expect' -TimeoutSeconds 600 -OrchestratorSlotGranted")) 'round-trip fixture header carries its runner command'
Assert ($fixture -match 'class\s+EXPG_CdfRoundTripClass\s*:\s*GenericEntityClass' -and $fixture -match 'class\s+EXPG_CdfRoundTrip\s*:\s*GenericEntity') 'round-trip fixture keeps the runner driver class name'
foreach ($needle in 'CDF_GMSaveCapture.Capture("eus-cdf-roundtrip", "fixture")', 'document.SaveToFile(FILE)', 'Loaded.LoadFromFile(FILE)', 'CDF_GMSaveRestore.Restore(Loaded)', 'cfg.m_bSaveCharacterInventories = true;', 'cfg.m_bClearBeforeLoad = true;', 'bool refused = !CDF_GMSaveRestore.Restore(Broken);', 'CDF_GMSaveRestore.Restore(Invalid)', 'EUS_CDF.Skipped == 1', 'Resource teamResource = Resource.Load(teamName);', 'm_Stripped.LoadFromFile(FILE)', 'CDF_GMSaveRestore.Restore(m_Stripped)', 'expect.m_sLoaded == expect.m_sControl) Inventories++;', 'Check(Bound() == 0 && Manager.CountPendingRestores() == 0, "the control load restores no unit script");') {
 Assert $fixture.Contains($needle) "round-trip fixture must drive: $needle"
}
# Inventory oracle (native run 2026-10-09): CDF 1.4.1 alone loses some pouch items of this
# vanilla fire team, scripted or not, so each soldier is compared with the same soldier of a
# control load without envelopes, both captured the same way and settled the same time.
$strip = Get-Body $fixture 'void\s+CheckInvalid\s*\(\s*\)'
Assert ($strip.Contains('EUS_CDF.Locate(record.m_sState, 1, payload, original) != 1') -and $strip.Contains('record.m_sState = original;') -and $strip.IndexOf('record.m_sState = original;') -lt $strip.IndexOf('CDF_GMSaveRestore.Restore(m_Stripped)')) 'the control document hands CDF each inner state without its envelope'
$capturePhase = Get-Body $fixture 'void\s+CaptureAndLoad\s*\(\s*\)'
Assert ($capturePhase.IndexOf('expect.m_sItems = Items(soldier);') -ge 0 -and $capturePhase.IndexOf('expect.m_sItems = Items(soldier);') -lt $capturePhase.IndexOf('CDF_GMSaveCapture.Capture(')) 'every soldier (plain too) is snapshotted in the frame of the save'
$wrapper = Get-Body $fixture 'modded\s+class\s+CDF_GMSaveState'
Assert ($wrapper.IndexOf('super.Apply(entity, state);') -ge 0 -and $wrapper.IndexOf('super.Apply(entity, state);') -lt $wrapper.IndexOf('EXPG_CdfRoundTrip.NoteApplied(entity);')) 'the fixture notes items only after CDF applied the state'
foreach ($settle in 'Settle(false) == 4', 'Settle(true) == 4') { Assert $fixture.Contains($settle) "both loads are compared on the same four saved soldiers: $settle" }
Assert (!$fixture.Contains('PlainItems')) 'no inventory snapshot taken at another moment than the save'
Assert ($fixture.Contains('PrintFormat("[EXPG EUS CDF ROUNDTRIP RESULT] %1 %2", first, second);')) 'round-trip fixture prints its result line'
'PASS: Unit Scripts CDF bridge: eusScript envelope through EUS_UnitState, unwrap before super, refusals before CDF changes the scene, unreadable scripts skipped, peeling and Intel re-check, bounded load summary, GM refusal dialog, Enforce gotchas; round-trip fixture wired with its CDF-alone inventory control.'
