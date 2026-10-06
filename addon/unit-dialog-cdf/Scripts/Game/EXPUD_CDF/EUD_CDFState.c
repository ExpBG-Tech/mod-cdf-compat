// EXPBG CDF Compat - Unit Dialog. Copyright 2026 ExpBG Tech (M.Pac and K.Edgar).
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 support for EXPBG Unit Dialog. CDF saves and respawns the AI
// characters themselves; this adapter carries each configured unit's speaker name, line
// slots and talking gesture in that character's CDF state and restores them with
// EUD_RestoreState (validated, registered, replicated) in CDF's deferred state pass.
//
// Envelope: {"eudDialog":"<payload>","cdfState":"<state of the inner chain>"} with payload
// {"version":1,"name":"","lines":[0 or 10 slots],"gesture":0}. The payload is a nested JSON
// string, so GM-written text never appears unescaped where other adapters search the
// state for their own keys.
//
// Order: CDF adapters wrap CDF_GMSaveState.Capture around super and unwrap in Apply before
// super, so states unwrap in reverse wrap order whatever the script order. Document checks
// read the stored state, whose outermost envelope depends on that order: the load check
// also finds this envelope under another adapter's "cdfState" envelope, and when this
// envelope hides an Intel Items envelope of the same character, the Intel Items document
// check (outermost state only) is re-run on the unwrapped state.
class EUD_CDF
{
 static const string KEY = "eudDialog";
 static const int VERSION = 1;
 // Envelopes a dialog envelope may sit under in a stored state.
 static const int PEEL_LIMIT = 4;

 static bool CaptureFailed;
 static string CaptureReason;

 // Per load. [CDF TIMING]: own time excludes super (the rest of the adapter chain and CDF).
 static int Expected, Restored, ApplyFailed, FinishAttempts;
 static int ApplyCalls, ApplyPayload, ApplyOwnMs, ApplyInnerMs, ApplySlowestMs;

 //------------------------------------------------------------------------------------------------
 static bool Encode(EUD_DialogRecord row, out string payload)
 {
  if (!row || !row.Valid()) return false;
  JsonSaveContext context = new JsonSaveContext();
  if (!context.WriteValue("version", VERSION) || !context.WriteValue("name", row.name) || !context.WriteValue("lines", row.lines) || !context.WriteValue("gesture", row.gesture)) return false;
  payload = context.SaveToString();
  return !payload.IsEmpty();
 }
 // Null unless the payload has this version and passes EUD_Dialog.ValidState.
 static EUD_DialogRecord Decode(string payload)
 {
  JsonLoadContext context = new JsonLoadContext();
  int version, gesture;
  string name;
  array<string> lines = {};
  if (!context.LoadFromString(payload) || !context.ReadValue("version", version) || version != VERSION) return null;
  if (!context.ReadValue("name", name) || !context.ReadValue("lines", lines) || !context.ReadValue("gesture", gesture)) return null;
  // ValidState also rejects lines that a failed read left null.
  if (!EUD_Dialog.ValidState(name, lines, gesture)) return null;
  EUD_DialogRecord row = new EUD_DialogRecord();
  row.name = name;
  row.lines = lines;
  row.gesture = gesture;
  return row;
 }
 // 1: envelope found (payload and the state it wraps); 0: none; -1: envelope without its
 // wrapped state, or a declared key that is not a string. levels 1 reads only the
 // outermost state (Apply); the document checks also look under other adapters'
 // "cdfState" envelopes.
 static int Locate(string state, int levels, out string payload, out string original)
 {
  string text = state;
  for (int depth = 0; depth < levels; depth++)
  {
   if (!text.Contains(KEY)) return 0;
   JsonLoadContext context = new JsonLoadContext();
   if (!context.LoadFromString(text)) return 0;
   if (context.ReadValue(KEY, payload))
   {
    if (context.ReadValue("cdfState", original)) return 1;
    return -1;
   }
   // A wrongly typed key is still this envelope: refuse it rather than hand the whole
   // envelope to CDF as plain state. Quotes inside JSON strings (nested states, GM text)
   // are escaped, so a quoted name followed by a colon at this level is a key.
   if (text.Contains("\"" + KEY + "\":")) return -1;
   string inner;
   if (!context.ReadValue("cdfState", inner)) return 0;
   text = inner;
  }
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 // Every dialog envelope decodes and validates and sits on a character record; Intel Items
 // state hidden under a dialog envelope passes Intel's own check; for a load, the saved
 // units plus the configured units that survive CDF's clear fit EUD_Dialog.MAX_UNITS.
 static bool ValidDocument(CDF_GMSaveDocument document, bool forLoad, out int payloads, out string reason)
 {
  payloads = 0;
  if (!document || !document.m_aEntities)
  {
   reason = "The save has no entity list";
   return false;
  }
  CDF_GMSaveDocument carriers = new CDF_GMSaveDocument();
  foreach (int index, CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record) continue;
   string payload, original;
   int found = Locate(record.m_sState, PEEL_LIMIT, payload, original);
   if (found == 0) continue;
   if (found < 0 || !Decode(payload))
   {
    reason = string.Format("Invalid dialog state on record %1 (%2)", index, record.m_sPrefab);
    return false;
   }
   if (record.m_iEntityType >= 0 && record.m_iEntityType != EEditableEntityType.CHARACTER)
   {
    reason = string.Format("Dialog state on record %1 is not on a character (%2)", index, record.m_sPrefab);
    return false;
   }
   // CDF destroys a saved wreck or corpse at once and never applies its state.
   if (!SCR_Enum.HasFlag(record.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED)) payloads++;
   // Intel Items reads only the stored (outermost) state. Its check skips a character
   // state without its envelopes, so every unwrapped state can go through it.
   if (original.IsEmpty()) continue;
   CDF_GMSaveEntityRecord carried = new CDF_GMSaveEntityRecord();
   carried.m_sPrefab = record.m_sPrefab;
   carried.m_sState = original;
   carriers.m_aEntities.Insert(carried);
  }
  if (!carriers.m_aEntities.IsEmpty() && !EII_CDFPayload.ValidDocument(carriers))
  {
   reason = "Intel Items state (carried intel or USB drives) of a dialog unit failed Intel Items validation";
   return false;
  }
  if (!forLoad || payloads == 0) return true;
  int kept = Survivors();
  if (payloads + kept > EUD_Dialog.MAX_UNITS)
  {
   reason = string.Format("%1 saved dialog units plus %2 that stay in the world exceed the limit of %3 per mission", payloads, kept, EUD_Dialog.MAX_UNITS);
   return false;
  }
  return true;
 }
 // Configured units CDF's clear leaves in the world: all of them without clearBeforeLoad.
 static int Survivors()
 {
  array<SCR_EditableCharacterComponent> units = {};
  EUD_Dialog.GetConfigured(units);
  bool clears = CDF_GMSaveConfig.GetInstance().m_bClearBeforeLoad;
  int kept;
  foreach (SCR_EditableCharacterComponent unit : units)
  {
   if (!unit || !unit.EUD_IsConfigured()) continue;
   if (!clears || !Cleared(unit)) kept++;
  }
  return kept;
 }
 // Same rule as CDF's clear: the topmost deletable managed entity goes with its children.
 static bool Cleared(SCR_EditableEntityComponent entity)
 {
  SCR_EditableEntityComponent node = entity;
  for (int depth = 0; node && depth < 16; depth++)
  {
   if (!node.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE) && CDF_GMSaveCapture.IsManaged(node)) return true;
   node = node.GetParentEntity();
  }
  return false;
 }
 // A configured unit outside the save keeps its dialog only while it stays in this world.
 static void ReportCapture(CDF_GMSaveDocument document, int payloads)
 {
  array<SCR_EditableCharacterComponent> units = {};
  if (EUD_Dialog.GetConfigured(units) == 0 && payloads == 0) return;
  set<IEntity> saved = new set<IEntity>();
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (record && record.m_Entity && record.m_Entity.GetOwner()) saved.Insert(record.m_Entity.GetOwner());
  }
  int missing;
  foreach (SCR_EditableCharacterComponent unit : units)
  {
   if (unit && unit.EUD_IsConfigured() && unit.GetOwner() && !saved.Contains(unit.GetOwner())) missing++;
  }
  if (missing > 0)
   Print(string.Format("[EUD CDF] Saved dialog of %1 units; %2 configured units are not in this CDF save (not GM-placed, player-controlled, prisoners or left out by CDF); loading this save does not restore their dialog", payloads, missing), LogLevel.WARNING);
  else PrintFormat("[EUD CDF] Saved dialog of %1 units", payloads);
 }

 //------------------------------------------------------------------------------------------------
 // First refusal of the current request, shown to the requesting GM with CDF's feedback.
 static string LastRefusal;
 static int LastRefusalTick;
 static const int REFUSAL_WINDOW_MS = 2000;
 static void Reject(string reason)
 {
  Print("[EUD CDF HOLD] " + reason, LogLevel.ERROR);
  int now = System.GetTickCount();
  int age = now - LastRefusalTick;
  if (LastRefusal.IsEmpty() || age < 0 || age > REFUSAL_WINDOW_MS) { LastRefusal = reason; LastRefusalTick = now; }
 }
 // CDF sends its save/load result in the same server frame as the refused request.
 static string TakeRefusal()
 {
  string reason = LastRefusal;
  int age = System.GetTickCount() - LastRefusalTick;
  LastRefusal = "";
  if (age < 0 || age > REFUSAL_WINDOW_MS) return "";
  if (reason.Length() > 1024) reason = reason.Substring(0, 1024);
  return reason;
 }

 //------------------------------------------------------------------------------------------------
 static void ResetLoad()
 {
  Expected = 0;
  Restored = 0;
  ApplyFailed = 0;
  FinishAttempts = 0;
  ApplyCalls = 0;
  ApplyPayload = 0;
  ApplyOwnMs = 0;
  ApplyInnerMs = 0;
  ApplySlowestMs = 0;
 }
 static void ApplyTiming(bool payload, int start, int innerStart, int innerEnd)
 {
  int inner = innerEnd - innerStart;
  int own = System.GetTickCount() - start - inner;
  ApplyCalls++;
  if (payload) ApplyPayload++;
  ApplyOwnMs += own;
  ApplyInnerMs += inner;
  if (own > ApplySlowestMs) ApplySlowestMs = own;
 }
}

modded class CDF_GMSaveState
{
 override static string Capture(IEntity entity)
 {
  string original = super.Capture(entity);
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(entity);
  if (!unit || !unit.EUD_IsConfigured()) return original;
  string payload;
  if (!EUD_CDF.Encode(unit.EUD_Capture(), payload))
  {
   EUD_CDF.CaptureFailed = true;
   EUD_CDF.CaptureReason = "A unit's dialog failed validation or could not be encoded";
   return original;
  }
  JsonSaveContext context = new JsonSaveContext();
  string wrapped;
  if (context.WriteValue(EUD_CDF.KEY, payload) && context.WriteValue("cdfState", original)) wrapped = context.SaveToString();
  if (wrapped.IsEmpty())
  {
   EUD_CDF.CaptureFailed = true;
   EUD_CDF.CaptureReason = "A unit's dialog envelope could not be encoded";
   return original;
  }
  return wrapped;
 }
 override static void Apply(IEntity entity, string state)
 {
  int timing = System.GetTickCount();
  string payload, original;
  int found;
  if (entity) found = EUD_CDF.Locate(state, 1, payload, original);
  if (found == 0)
  {
   int passInner = System.GetTickCount();
   super.Apply(entity, state);
   EUD_CDF.ApplyTiming(false, timing, passInner, System.GetTickCount());
   return;
  }
  if (found < 0)
  {
   // Restore refuses such a document before clearing; never guess the wrapped state.
   EUD_CDF.ApplyFailed++;
   Print("[EUD CDF] Invalid dialog envelope (wrongly typed or without its CDF state); unit state not applied", LogLevel.ERROR);
   EUD_CDF.ApplyTiming(true, timing, 0, 0);
   return;
  }
  int inner = System.GetTickCount();
  super.Apply(entity, original);
  int innerEnd = System.GetTickCount();
  EUD_DialogRecord row = EUD_CDF.Decode(payload);
  SCR_EditableCharacterComponent unit = EUD_Dialog.Find(entity);
  bool restored;
  if (!row) Print("[EUD CDF] Saved dialog failed validation; not restored", LogLevel.ERROR);
  else if (!unit) Print("[EUD CDF] Saved dialog has no editable character to restore onto", LogLevel.ERROR);
  else if (!unit.EUD_RestoreState(row.name, row.lines, row.gesture)) Print("[EUD CDF] Saved dialog was refused by its unit (invalid or unit limit reached)", LogLevel.ERROR);
  else restored = true;
  if (restored) EUD_CDF.Restored++;
  else EUD_CDF.ApplyFailed++;
  EUD_CDF.ApplyTiming(true, timing, inner, innerEnd);
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  EUD_CDF.CaptureFailed = false;
  EUD_CDF.CaptureReason = "";
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  // Another adapter refused; keep its refusal.
  if (!document) return null;
  if (EUD_CDF.CaptureFailed)
  {
   EUD_CDF.Reject(EUD_CDF.CaptureReason + "; nothing was written");
   return null;
  }
  int payloads;
  string reason;
  if (!EUD_CDF.ValidDocument(document, false, payloads, reason))
  {
   EUD_CDF.Reject(reason + "; nothing was written");
   return null;
  }
  EUD_CDF.ReportCapture(document, payloads);
  return document;
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  int timing = System.GetTickCount();
  int records = document.m_aEntities.Count();
  EUD_CDF.ResetLoad();
  int payloads;
  string reason;
  if (!EUD_CDF.ValidDocument(document, true, payloads, reason))
  {
   EUD_CDF.Reject(reason + "; the scene was not cleared");
   return false;
  }
  EUD_CDF.Expected = payloads;
  int inner = System.GetTickCount();
  bool result = super.Restore(document);
  int innerEnd = System.GetTickCount();
  if (result)
  {
   GetGame().GetCallqueue().Remove(EUD_FinishLoad);
   GetGame().GetCallqueue().CallLater(EUD_FinishLoad, 600, false);
  }
  int before = inner - timing;
  int after = System.GetTickCount() - innerEnd;
  PrintFormat("[CDF TIMING] unit-dialog restore path=scan result=%1 records=%2 payload=%3 ownMs=%4 beforeMs=%5 afterMs=%6 innerMs=%7", result, records, payloads, before + after, before, after, innerEnd - inner);
  return result;
 }
 // Once per load, after the CDF deferred state pass has run (or timed out).
 protected static void EUD_FinishLoad()
 {
  bool complete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty();
  if (!complete && GetGame())
  {
   EUD_CDF.FinishAttempts++;
   if (EUD_CDF.FinishAttempts < 20) { GetGame().GetCallqueue().CallLater(EUD_FinishLoad, 100, false); return; }
  }
  PrintFormat("[CDF TIMING] unit-dialog apply calls=%1 payload=%2 ownMs=%3 innerMs=%4 slowestOwnMs=%5 complete=%6", EUD_CDF.ApplyCalls, EUD_CDF.ApplyPayload, EUD_CDF.ApplyOwnMs, EUD_CDF.ApplyInnerMs, EUD_CDF.ApplySlowestMs, complete);
  if (EUD_CDF.Expected == 0 && EUD_CDF.Restored == 0 && EUD_CDF.ApplyFailed == 0) return;
  LogLevel severity = LogLevel.NORMAL;
  if (EUD_CDF.ApplyFailed > 0 || EUD_CDF.Restored != EUD_CDF.Expected || !complete) severity = LogLevel.WARNING;
  Print(string.Format("[EUD CDF LOAD] restored=%1 failed=%2 expected=%3 complete=%4", EUD_CDF.Restored, EUD_CDF.ApplyFailed, EUD_CDF.Expected, complete), severity);
 }
}

// GM-facing refusal: CDF reports save/load results as hints only and never says why an
// adapter refused. Show the Unit Dialog reason in a CDF-style dialog.
modded class SCR_PlayerController
{
 override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)
 {
  super.CDF_GMSave_Feedback(result);
  string action;
  if (result.m_sKey == "#CDF_GMSave_Msg_LoadAborted") action = "Load";
  else if (result.m_sKey == "#CDF_GMSave_Msg_CaptureFailed") action = "Save";
  else return;
  string reason = EUD_CDF.TakeRefusal();
  if (reason.IsEmpty()) return;
  string message = action + " refused by EXPBG Unit Dialog:\n\n" + reason + "\n\nDetails: [EUD CDF HOLD] in the server log.";
  // A hosting GM owns this controller locally; an owner RPC would not reach it.
  if (GetGame().GetPlayerController() == this) EUD_CDF_RpcDo_Refused(message);
  else Rpc(EUD_CDF_RpcDo_Refused, message);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EUD_CDF_RpcDo_Refused(string message)
 {
  EUD_CDFRefusalDialog.Open(message);
 }
}
class EUD_CDFRefusalDialog : CDF_GMSaveBaseDialog
{
 static void Open(string message)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager()) return;
  SCR_ConfigurableDialogUiPreset preset = CDF_GMSaveDialogUtils.CreatePreset("EXPBG_EUD_CDF_REFUSED", "#CDF_GMSave_Hint_Title");
  preset.m_eVisualStyle = EDialogType.WARNING;
  preset.m_sMessage = message;
  preset.m_aButtons.Insert(CDF_GMSaveDialogUtils.CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "#CDF_GMSave_Btn_Close", EConfigurableDialogUiButtonAlign.LEFT, "MenuBack"));
  EUD_CDFRefusalDialog dialog = new EUD_CDFRefusalDialog();
  CreateByPreset(preset, dialog);
 }
}
