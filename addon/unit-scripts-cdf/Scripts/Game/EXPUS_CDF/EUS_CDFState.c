// EXPBG CDF Compat - Unit Scripts. Copyright 2026 ExpBG Tech.
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 support for EXPBG Unit Scripts (Hold position, Freeze and the
// ambient animations). CDF saves and respawns the AI characters themselves; this adapter
// carries each scripted soldier's script in that character's CDF state, through the
// versioned GM Tools state API (EUS_UnitState: script code, which is also the animation,
// held spot and heading), and hands it back in CDF's deferred state pass, after CDF
// repaired group membership. EUS_UnitState.Restore queues it with the Unit Scripts
// manager, which binds it once the soldier's AI is ready (bounded wait) on the saved spot.
//
// Envelope: {"eusScript":"<payload>","cdfState":"<state of the inner chain>"}; the payload
// is EUS_UnitState's JSON text (version 1), nested as a string so no key of it appears
// unescaped where other adapters search the state for their own keys.
//
// Order: CDF adapters wrap CDF_GMSaveState.Capture around super and unwrap in Apply before
// super, so states unwrap in reverse wrap order whatever the script order. Document checks
// read the stored state, whose outermost envelope depends on that order: this check finds
// its envelope under other adapters' "cdfState" envelopes, and when it hides an Intel Items
// envelope of the same character, the Intel Items document check (outermost state only) is
// re-run on the unwrapped state (Unit Dialog peels through this envelope itself).
//
// Refusals happen before anything changes: a save whose scripts cannot be encoded is not
// written, and a load whose envelope is structurally broken (its wrapped CDF state could
// not be handed on, which would lose the soldier's inventory and damage) is refused before
// CDF clears the scene. A well-formed envelope whose script is invalid or of an unknown
// version only loses that script (logged); the load goes on.
class EUS_CDF
{
 static const string KEY = "eusScript";
 static const string SOURCE = "CDF save";
 // Envelopes this one may sit under in a stored state.
 static const int PEEL_LIMIT = 6;

 static bool CaptureFailed;
 static string CaptureReason;

 // Per load. [CDF TIMING]: own time excludes super (the rest of the adapter chain and CDF).
 static int Expected, Queued, ApplyFailed, Skipped, FinishAttempts;
 static int ApplyCalls, ApplyPayload, ApplyOwnMs, ApplyInnerMs, ApplySlowestMs;

 //------------------------------------------------------------------------------------------------
 // 1: envelope found (payload and the state it wraps); 0: none; -1: envelope without its
 // wrapped state, or a declared key that is not a string. levels 1 reads only the
 // outermost state (Apply); the document checks also look under other adapters'
 // "cdfState" envelopes.
 static int Locate(string state, int levels, out string payload, out string original)
 {
  string text = state;
  for (int depth = 0; depth < levels; depth++)
  {
   if (!text.Contains(KEY))
   {
    return 0;
   }
   JsonLoadContext context = new JsonLoadContext();
   if (!context.LoadFromString(text))
   {
    return 0;
   }
   if (context.ReadValue(KEY, payload))
   {
    if (context.ReadValue("cdfState", original))
    {
     return 1;
    }
    return -1;
   }
   // A wrongly typed key is still this envelope: refuse it rather than hand the whole
   // envelope to CDF as plain state. Quotes inside JSON strings (nested states) are
   // escaped, so a quoted name followed by a colon at this level is a key.
   if (text.Contains("\"" + KEY + "\":"))
   {
    return -1;
   }
   string inner;
   if (!context.ReadValue("cdfState", inner))
   {
    return 0;
   }
   text = inner;
  }
  return 0;
 }

 //------------------------------------------------------------------------------------------------
 // Every envelope is structurally sound and sits on a character record; Intel Items state
 // hidden under it passes Intel's own check. A save also requires every script to decode;
 // a load counts undecodable scripts in skipped and goes on.
 static bool ValidDocument(CDF_GMSaveDocument document, bool forLoad, out int payloads, out int skipped, out string reason)
 {
  payloads = 0;
  skipped = 0;
  if (!document || !document.m_aEntities)
  {
   reason = "The save has no entity list";
   return false;
  }
  CDF_GMSaveDocument carriers = new CDF_GMSaveDocument();
  foreach (int index, CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record) continue;
   string payload;
   string original;
   int found = Locate(record.m_sState, PEEL_LIMIT, payload, original);
   if (found == 0) continue;
   if (found < 0)
   {
    reason = string.Format("Invalid unit script envelope on record %1 (%2)", index, record.m_sPrefab);
    return false;
   }
   string why;
   bool decoded = EUS_UnitState.Decode(payload, why) != null;
   bool character = record.m_iEntityType < 0 || record.m_iEntityType == EEditableEntityType.CHARACTER;
   if (!forLoad && (!decoded || !character))
   {
    reason = string.Format("Unit script on record %1 (%2) cannot be saved: %3", index, record.m_sPrefab, why);
    return false;
   }
   // CDF destroys a saved wreck or corpse at once and never applies its state.
   bool destroyed = SCR_Enum.HasFlag(record.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED);
   if (!destroyed && decoded && character) payloads++;
   else if (!destroyed) skipped++;
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
   reason = "Intel Items state (carried intel or USB drives) of a scripted unit failed Intel Items validation";
   return false;
  }
  return true;
 }

 // A scripted soldier outside the save keeps his script only while he stays in this world.
 static void ReportCapture(CDF_GMSaveDocument document, int payloads)
 {
  EUS_Manager manager = EUS_Manager.Current();
  array<EUS_UnitControl> controls = {};
  if ((!manager || manager.GetControls(controls) == 0) && payloads == 0)
  {
   return;
  }
  set<IEntity> saved = new set<IEntity>();
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (record && record.m_Entity && record.m_Entity.GetOwner()) saved.Insert(record.m_Entity.GetOwner());
  }
  int missing;
  foreach (EUS_UnitControl control : controls)
  {
   if (control && control.GetActor() && !saved.Contains(control.GetActor())) missing++;
  }
  if (missing > 0)
  {
   Print(string.Format("[EUS CDF] Saved %1 unit scripts; %2 scripted units are not in this CDF save (not GM-placed or left out by CDF); loading this save does not restore their scripts", payloads, missing), LogLevel.WARNING);
   return;
  }
  PrintFormat("[EUS CDF] Saved %1 unit scripts", payloads);
 }

 //------------------------------------------------------------------------------------------------
 // First refusal of the current request, shown to the requesting GM with CDF's feedback.
 static string LastRefusal;
 static int LastRefusalTick;
 static const int REFUSAL_WINDOW_MS = 2000;
 static void Reject(string reason)
 {
  Print("[EUS CDF HOLD] " + reason, LogLevel.ERROR);
  int now = System.GetTickCount();
  int age = now - LastRefusalTick;
  if (LastRefusal.IsEmpty() || age < 0 || age > REFUSAL_WINDOW_MS)
  {
   LastRefusal = reason;
   LastRefusalTick = now;
  }
 }
 // CDF sends its save/load result in the same server frame as the refused request.
 static string TakeRefusal()
 {
  string reason = LastRefusal;
  int age = System.GetTickCount() - LastRefusalTick;
  LastRefusal = "";
  if (age < 0 || age > REFUSAL_WINDOW_MS)
  {
   return "";
  }
  if (reason.Length() > 1024) reason = reason.Substring(0, 1024);
  return reason;
 }

 //------------------------------------------------------------------------------------------------
 static void ResetLoad()
 {
  Expected = 0;
  Queued = 0;
  ApplyFailed = 0;
  Skipped = 0;
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
  EUS_UnitState state = EUS_UnitState.Capture(entity);
  if (!state)
  {
   return original;
  }
  string payload = state.Encode();
  if (payload.IsEmpty() || !state.Validate().IsEmpty())
  {
   EUS_CDF.CaptureFailed = true;
   EUS_CDF.CaptureReason = "A unit's script failed validation or could not be encoded";
   return original;
  }
  JsonSaveContext context = new JsonSaveContext();
  string wrapped;
  if (context.WriteValue(EUS_CDF.KEY, payload) && context.WriteValue("cdfState", original)) wrapped = context.SaveToString();
  if (wrapped.IsEmpty())
  {
   EUS_CDF.CaptureFailed = true;
   EUS_CDF.CaptureReason = "A unit's script envelope could not be encoded";
   return original;
  }
  return wrapped;
 }
 override static void Apply(IEntity entity, string state)
 {
  int timing = System.GetTickCount();
  string payload;
  string original;
  int found;
  if (entity) found = EUS_CDF.Locate(state, 1, payload, original);
  if (found == 0)
  {
   int passInner = System.GetTickCount();
   super.Apply(entity, state);
   EUS_CDF.ApplyTiming(false, timing, passInner, System.GetTickCount());
   return;
  }
  if (found < 0)
  {
   // Restore refuses such a document before clearing; never guess the wrapped state.
   EUS_CDF.ApplyFailed++;
   Print("[EUS CDF] Invalid unit script envelope (wrongly typed or without its CDF state); unit state not applied", LogLevel.ERROR);
   EUS_CDF.ApplyTiming(true, timing, 0, 0);
   return;
  }
  int inner = System.GetTickCount();
  super.Apply(entity, original);
  int innerEnd = System.GetTickCount();
  string reason;
  EUS_UnitState saved = EUS_UnitState.Decode(payload, reason);
  if (!saved)
  {
   EUS_CDF.Skipped++;
   Print("[EUS CDF] Saved unit script skipped: " + reason, LogLevel.WARNING);
  }
  else if (saved.Restore(entity, EUS_CDF.SOURCE, reason))
  {
   EUS_CDF.Queued++;
  }
  else
  {
   EUS_CDF.ApplyFailed++;
   Print("[EUS CDF] Saved unit script refused: " + reason, LogLevel.ERROR);
  }
  EUS_CDF.ApplyTiming(true, timing, inner, innerEnd);
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  EUS_CDF.CaptureFailed = false;
  EUS_CDF.CaptureReason = "";
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  // Another adapter refused; keep its refusal.
  if (!document)
  {
   return null;
  }
  if (EUS_CDF.CaptureFailed)
  {
   EUS_CDF.Reject(EUS_CDF.CaptureReason + "; nothing was written");
   return null;
  }
  int payloads;
  int skipped;
  string reason;
  if (!EUS_CDF.ValidDocument(document, false, payloads, skipped, reason))
  {
   EUS_CDF.Reject(reason + "; nothing was written");
   return null;
  }
  EUS_CDF.ReportCapture(document, payloads);
  return document;
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  int timing = System.GetTickCount();
  int records = document.m_aEntities.Count();
  EUS_CDF.ResetLoad();
  int payloads;
  int skipped;
  string reason;
  if (!EUS_CDF.ValidDocument(document, true, payloads, skipped, reason))
  {
   EUS_CDF.Reject(reason + "; the scene was not cleared");
   return false;
  }
  EUS_CDF.Expected = payloads;
  int inner = System.GetTickCount();
  bool result = super.Restore(document);
  int innerEnd = System.GetTickCount();
  if (result)
  {
   GetGame().GetCallqueue().Remove(EUS_FinishLoad);
   GetGame().GetCallqueue().CallLater(EUS_FinishLoad, 600, false);
  }
  int before = inner - timing;
  int after = System.GetTickCount() - innerEnd;
  PrintFormat("[CDF TIMING] unit-scripts restore path=scan result=%1 records=%2 payload=%3 skipped=%4 ownMs=%5 beforeMs=%6 afterMs=%7 innerMs=%8", result, records, payloads, skipped, before + after, before, after, innerEnd - inner);
  return result;
 }
 // Once per load, after the CDF deferred state pass has run (or timed out). The scripts
 // themselves bind in the Unit Scripts pump, which logs "[EUS] restored N saved unit
 // script(s) from the CDF save".
 protected static void EUS_FinishLoad()
 {
  bool complete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty();
  if (!complete && GetGame())
  {
   EUS_CDF.FinishAttempts++;
   if (EUS_CDF.FinishAttempts < 20)
   {
    GetGame().GetCallqueue().CallLater(EUS_FinishLoad, 100, false);
    return;
   }
  }
  PrintFormat("[CDF TIMING] unit-scripts apply calls=%1 payload=%2 ownMs=%3 innerMs=%4 slowestOwnMs=%5 complete=%6", EUS_CDF.ApplyCalls, EUS_CDF.ApplyPayload, EUS_CDF.ApplyOwnMs, EUS_CDF.ApplyInnerMs, EUS_CDF.ApplySlowestMs, complete);
  if (EUS_CDF.Expected == 0 && EUS_CDF.Queued == 0 && EUS_CDF.ApplyFailed == 0 && EUS_CDF.Skipped == 0)
  {
   return;
  }
  LogLevel severity = LogLevel.NORMAL;
  if (EUS_CDF.ApplyFailed > 0 || EUS_CDF.Skipped > 0 || EUS_CDF.Queued != EUS_CDF.Expected || !complete) severity = LogLevel.WARNING;
  Print(string.Format("[EUS CDF LOAD] queued=%1 failed=%2 skipped=%3 expected=%4 complete=%5", EUS_CDF.Queued, EUS_CDF.ApplyFailed, EUS_CDF.Skipped, EUS_CDF.Expected, complete), severity);
 }
}

// GM-facing refusal: CDF reports save/load results as hints only and never says why an
// adapter refused. Show the Unit Scripts reason in a CDF-style dialog.
modded class SCR_PlayerController
{
 override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)
 {
  super.CDF_GMSave_Feedback(result);
  string action;
  if (result.m_sKey == "#CDF_GMSave_Msg_LoadAborted") action = "Load";
  else if (result.m_sKey == "#CDF_GMSave_Msg_CaptureFailed") action = "Save";
  string reason;
  if (!action.IsEmpty()) reason = EUS_CDF.TakeRefusal();
  if (reason.IsEmpty())
  {
   return;
  }
  string message = action + " refused by EXPBG Unit Scripts:\n\n" + reason + "\n\nDetails: [EUS CDF HOLD] in the server log.";
  // A hosting GM owns this controller locally; an owner RPC would not reach it.
  if (GetGame().GetPlayerController() == this) EUS_CDF_RpcDo_Refused(message);
  else Rpc(EUS_CDF_RpcDo_Refused, message);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EUS_CDF_RpcDo_Refused(string message)
 {
  EUS_CDFRefusalDialog.Open(message);
 }
}
class EUS_CDFRefusalDialog : CDF_GMSaveBaseDialog
{
 static void Open(string message)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager())
  {
   return;
  }
  SCR_ConfigurableDialogUiPreset preset = CDF_GMSaveDialogUtils.CreatePreset("EXPBG_EUS_CDF_REFUSED", "#CDF_GMSave_Hint_Title");
  preset.m_eVisualStyle = EDialogType.WARNING;
  preset.m_sMessage = message;
  preset.m_aButtons.Insert(CDF_GMSaveDialogUtils.CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "#CDF_GMSave_Btn_Close", EConfigurableDialogUiButtonAlign.LEFT, "MenuBack"));
  EUS_CDFRefusalDialog dialog = new EUS_CDFRefusalDialog();
  CreateByPreset(preset, dialog);
 }
}
