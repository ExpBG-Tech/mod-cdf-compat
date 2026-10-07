// EXPBG CDF Compat - Garrison bridge. Copyright 2026 ExpBG Tech (M.Pac and K.Edgar).
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// Garrisons are saved and loaded with CDF Game Master Save 1.4.1 through the EXPBG GM
// Tools garrison ledger (GM Tools 0.1.11, EXPG_Snapshot.c, EXPG_GarrisonPersistence).
// With this bridge GM Tools runs "CDF bridged": every garrison-owned squad, living
// guard and waypoint is NON_SERIALIZABLE, so CDF never captures it, and Full caching
// is allowed under CDF. The ledger carries every garrison instead (building, squad,
// members including casualties, posts and patrol stops in building-local
// coordinates, cache state, settings, order, overrides), in an envelope around the
// document's world state, as Ambient Destruction does:
//   m_sWorldState = {"expgGarrisons": <ledger JSON>, "cdfOriginal": <inner state>}
//
// Save (CDF_GMSaveCapture.Capture): the ledger is exported first (this synchronises the
// exclusion); a refusal returns no document and tells the Game Master why. After CDF's
// capture, any record whose entity the garrison still owns is removed (backstop) and
// parent/target indices are remapped. A save without garrisons writes no envelope.
//
// Load (CDF_GMSaveRestore.Restore), all checks before CDF changes the scene:
//  - the envelope is peeled (top level, or nested inside the Ambient Destruction
//    layer) and the ledger read and validated: schema, world, limits, every soldier
//    and squad prefab, faction and order;
//  - an unreadable ledger refuses the load with the reason; loading the same save
//    again within two minutes loads it without its garrisons (recovery);
//  - a ledger with garrisons requires clearBeforeLoad (otherwise every garrison
//    would exist twice);
//  - BeginImport: Add Garrison is refused until the garrisons are back.
// CDF's Clear deletes the garrisons of the current scene (IsManaged is true for
// garrison-owned entities). When CDF changed the scene (it returned true, or its
// restore set was replaced), the old garrisons are discarded without waking or
// respawning anyone (clearBeforeLoad) and the ledger is queued. Once CDF's deferred
// finalization has run (its protected statics are idle, polled every 100 ms for at
// most 10 s), FinishImport creates the loaded garrisons: each waits for its
// building's analysis, takes its posts back and wakes with AI pinned until bound
// (saved awake or Simulation), or stays Full cached. No duplicate under clearBeforeLoad:
// the old garrisons are cleared and discarded, CDF holds no garrison entity, and the
// ledger tokens are unique.
class EXPG_CDFGarrison
{
 static const string KEY = "expgGarrisons";
 static const string INNER = "cdfOriginal";
 // Other bridges' world-state layers this bridge may find above its own.
 static const string EAD_KEY = "eadBuildings";
 static const int SKIP_WINDOW_MS = 120000;
 static const int REFUSAL_WINDOW_MS = 2000;
 static ref CDF_GMSaveDocument Pending;
 static BaseWorld ImportWorld;
 static int FinishAttempts;
 static bool LoadedWithoutGarrisons;
 // Recovery: the document whose ledger could not be read, armed by a refused load.
 static string SkipKey;
 static int SkipTick;
 // First refusal of the current request, shown to the requesting Game Master.
 static string LastRefusal;
 static int LastRefusalTick;

 static bool Declared(string text)
 {
  return text.Contains("\"" + KEY + "\"");
 }

 static string Wrap(string key, string payload, string original)
 {
  JsonSaveContext context = new JsonSaveContext();
  context.WriteValue(key, payload);
  context.WriteValue(INNER, original);
  return context.SaveToString();
 }

 // Finds this bridge's layer and returns the world state without it. Bridges peel
 // their own layer in their own Restore (outermost first), so the layer is normally at
 // the top; it is also found under the Ambient Destruction layer, which is rebuilt.
 static bool Peel(string text, out string payload, out string remainder, out string reason, int depth = 0)
 {
  payload = "";
  remainder = text;
  reason = "the garrison envelope is not valid JSON";
  JsonLoadContext context = new JsonLoadContext();
  if (!context.LoadFromString(text)) { return false; }
  string inner;
  if (context.ReadValue(KEY, payload))
  {
   reason = "the garrison envelope has no original CDF world state";
   if (!context.ReadValue(INNER, inner)) { return false; }
   remainder = inner;
   reason = "";
   return true;
  }
  string other;
  reason = "the garrison envelope is nested deeper than four layers";
  if (depth >= 4 || !context.ReadValue(EAD_KEY, other) || !context.ReadValue(INNER, inner)) { return false; }
  string nested;
  if (!Peel(inner, payload, nested, reason, depth + 1)) { return false; }
  remainder = Wrap(EAD_KEY, other, nested);
  return true;
 }

 static string DocumentKey(CDF_GMSaveDocument document)
 {
  return string.Format("%1|%2|%3|%4|%5", document.m_sWorld, document.m_sDisplayName, document.m_iStamp, document.m_aEntities.Count(), document.m_sWorldState.Length());
 }

 // A second load of the same unreadable save within two minutes loads it without garrisons.
 static bool SkipConfirmed(string key)
 {
  int age = System.GetTickCount() - SkipTick;
  return !SkipKey.IsEmpty() && SkipKey == key && age >= 0 && age <= SKIP_WINDOW_MS;
 }

 static void ArmSkip(string key)
 {
  SkipKey = key;
  SkipTick = System.GetTickCount();
 }

 static void Reject(string reason)
 {
  Print("[EXPG CDF HOLD] " + reason, LogLevel.WARNING);
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
  if (age < 0 || age > REFUSAL_WINDOW_MS) { return ""; }
  if (reason.Length() > 1024) { reason = reason.Substring(0, 1024); }
  return reason;
 }

 // Backstop after capture: a record whose entity the garrison still owns is removed;
 // parent and target indices are remapped. A child of a removed record becomes a root.
 static int StripOwned(CDF_GMSaveDocument document)
 {
  array<ref CDF_GMSaveEntityRecord> records = document.m_aEntities;
  array<int> remap = {};
  int removed = 0;
  int nextIndex = 0;
  foreach (CDF_GMSaveEntityRecord record : records)
  {
   bool garrisoned = record.m_Entity && EXPG_GarrisonPersistence.OwnsForSave(record.m_Entity.GetOwner());
   if (garrisoned) { remap.Insert(-1); removed++; continue; }
   remap.Insert(nextIndex);
   nextIndex++;
  }
  if (removed == 0) { return 0; }
  array<ref CDF_GMSaveEntityRecord> kept = {};
  foreach (int index, CDF_GMSaveEntityRecord record : records)
  {
   if (remap[index] < 0) { continue; }
   if (record.m_iParent >= 0)
   {
    if (record.m_iParent < remap.Count()) { record.m_iParent = remap[record.m_iParent]; }
    else { record.m_iParent = -1; }
   }
   if (record.m_iTarget >= 0)
   {
    int target = -1;
    if (record.m_iTarget < remap.Count()) { target = remap[record.m_iTarget]; }
    if (target < 0) { record.m_iTarget = CDF_GMSaveEntityRecord.TARGET_NONE; }
    else { record.m_iTarget = target; }
   }
   kept.Insert(record);
  }
  document.m_aEntities = kept;
  return removed;
 }

 static void Reset()
 {
  // A pending finalization callback sees no Pending document and returns.
  Pending = null;
  ImportWorld = null;
  FinishAttempts = 0;
  LoadedWithoutGarrisons = false;
 }
}

// The handshake: GM Tools runs "CDF bridged" with this bridge loaded.
modded class EXPG_GarrisonManager
{
 override protected int CdfBridgeVersion()
 {
  return EXPG_GarrisonPersistence.BRIDGE_API;
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  if (!Replication.IsServer()) { return super.Capture(displayName, author); }
  string json, reason;
  // Exporting synchronises the exclusion: every garrison-owned entity is
  // NON_SERIALIZABLE before CDF walks the scene.
  if (!EXPG_GarrisonPersistence.ExportJson(json, reason))
  {
   EXPG_CDFGarrison.Reject("Garrisons cannot be saved now: " + reason);
   return null;
  }
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  if (!document) { return null; }
  int stripped = EXPG_CDFGarrison.StripOwned(document);
  if (stripped > 0) { PrintFormat("[EXPG CDF SAVE] %1 garrison-owned records reached the document and were removed (the ledger carries them)", stripped, level: LogLevel.WARNING); }
  if (!json.IsEmpty()) { document.m_sWorldState = EXPG_CDFGarrison.Wrap(EXPG_CDFGarrison.KEY, json, document.m_sWorldState); }
  PrintFormat("[EXPG CDF SAVE] ledgerBytes=%1 strippedRecords=%2 records=%3", json.Length(), stripped, document.m_aEntities.Count());
  return document;
 }

 // CDF's Clear deletes what it manages: garrison squads and guards are cleared by a
 // load although CDF never captures them (the ledger restores them).
 override static bool IsManaged(SCR_EditableEntityComponent entity)
 {
  if (entity && EXPG_GarrisonPersistence.OwnsForSave(entity.GetOwner())) { return true; }
  return super.IsManaged(entity);
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  if (!Replication.IsServer()) { return super.Restore(document); }
  if (EXPG_CDFGarrison.ImportWorld && GetGame() && EXPG_CDFGarrison.ImportWorld != GetGame().GetWorld()) { EXPG_CDFGarrison.Reset(); }
  bool clear = CDF_GMSaveConfig.GetInstance().m_bClearBeforeLoad;
  string wrapped = document.m_sWorldState;
  string inner = wrapped;
  array<ref EXPG_GarrisonSnapshot> ledger = {};
  bool skipped = false;
  if (EXPG_CDFGarrison.Declared(wrapped))
  {
   string payload, reason;
   bool peeled = EXPG_CDFGarrison.Peel(wrapped, payload, inner, reason);
   bool parsed = peeled && EXPG_GarrisonPersistence.ParseJson(payload, ledger, reason);
   if (!parsed)
   {
    string key = EXPG_CDFGarrison.DocumentKey(document);
    if (!EXPG_CDFGarrison.SkipConfirmed(key))
    {
     EXPG_CDFGarrison.ArmSkip(key);
     EXPG_CDFGarrison.Reject("This save's garrison ledger cannot be loaded: " + reason + ". Nothing was changed. To load this save without its garrisons, load it again within two minutes.");
     return false;
    }
    EXPG_CDFGarrison.SkipKey = "";
    skipped = true;
    ledger.Clear();
    // An envelope that cannot be peeled cannot give CDF its world state back.
    if (!peeled) { inner = ""; }
    Print("[EXPG CDF LOAD] Loading this save without its garrisons, as confirmed by the Game Master: " + reason, LogLevel.WARNING);
   }
  }
  if (!ledger.IsEmpty() && !clear)
  {
   EXPG_CDFGarrison.Reject(string.Format("This save holds %1 garrisons. Loading it requires CDF clearBeforeLoad; otherwise every garrison would exist twice. Nothing was changed.", ledger.Count()));
   return false;
  }
  if (EXPG_CDFGarrison.Pending || EXPG_GarrisonPersistence.Importing())
  {
   EXPG_CDFGarrison.Reject("Garrisons from the previous load are still being placed; load again in a moment. Nothing was changed.");
   return false;
  }
  string why;
  if (!EXPG_GarrisonPersistence.BeginImport(why))
  {
   EXPG_CDFGarrison.Reject("Garrisons cannot be loaded now: " + why + ". Nothing was changed.");
   return false;
  }
  ref set<SCR_EditableEntityComponent> previous = s_RestoredEntities;
  document.m_sWorldState = inner;
  bool result = super.Restore(document);
  document.m_sWorldState = wrapped;
  bool mutated = result || s_RestoredEntities != previous;
  if (!mutated)
  {
   // Another bridge or CDF refused before Clear: the scene and its garrisons are untouched.
   EXPG_GarrisonPersistence.EndImport();
   return result;
  }
  if (clear) { EXPG_GarrisonPersistence.DiscardForImport("a CDF save was loaded"); }
  string queued;
  if (!EXPG_GarrisonPersistence.QueueImport(ledger, queued))
  {
   // Validated before the clear; a failure here keeps the scene CDF restored.
   Print("[EXPG CDF LOAD] Garrison ledger could not be queued: " + queued, LogLevel.ERROR);
   EXPG_GarrisonPersistence.EndImport();
   EXPG_GarrisonPersistence.Notify("The garrisons of this save were not loaded: " + queued);
   return result;
  }
  EXPG_CDFGarrison.Pending = document;
  EXPG_CDFGarrison.ImportWorld = GetGame().GetWorld();
  EXPG_CDFGarrison.FinishAttempts = 0;
  EXPG_CDFGarrison.LoadedWithoutGarrisons = skipped;
  GetGame().GetCallqueue().Remove(EXPG_CDFFinishImport);
  GetGame().GetCallqueue().CallLater(EXPG_CDFFinishImport, 600, false);
  PrintFormat("[EXPG CDF LOAD] result=%1 garrisons=%2 clearBeforeLoad=%3 withoutGarrisons=%4", result, ledger.Count(), clear, skipped);
  return result;
 }

 // CDF 1.4.1 applies deep state in its deferred finalization (+500 ms). Garrisons are
 // created once it is done: buildings, compositions and the Ambient Destruction replay
 // are final by then.
 static void EXPG_CDFFinishImport()
 {
  if (!EXPG_CDFGarrison.Pending) { return; }
  if (!GetGame() || GetGame().GetWorld() != EXPG_CDFGarrison.ImportWorld)
  {
   EXPG_GarrisonPersistence.EndImport();
   EXPG_CDFGarrison.Reset();
   return;
  }
  bool complete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty() && s_aGuardedGroups && s_aGuardedGroups.IsEmpty() && s_aPendingMembers && s_aPendingMembers.IsEmpty();
  if (!complete)
  {
   EXPG_CDFGarrison.FinishAttempts++;
   if (EXPG_CDFGarrison.FinishAttempts < 100)
   {
    GetGame().GetCallqueue().CallLater(EXPG_CDFFinishImport, 100, false);
    return;
   }
   Print("[EXPG CDF LOAD] CDF deferred finalization did not complete within 10 s; garrisons are created now", LogLevel.WARNING);
  }
  bool withoutGarrisons = EXPG_CDFGarrison.LoadedWithoutGarrisons;
  EXPG_CDFGarrison.Pending = null;
  EXPG_CDFGarrison.LoadedWithoutGarrisons = false;
  EXPG_GarrisonPersistence.FinishImport();
  if (withoutGarrisons) { EXPG_GarrisonPersistence.Notify("This save was loaded without its garrisons, as confirmed: its garrison ledger could not be read (see [EXPG CDF LOAD] in the server log)."); }
  PrintFormat("[EXPG CDF LOAD FINALIZED] nativeComplete=%1 attempts=%2", complete, EXPG_CDFGarrison.FinishAttempts);
 }
}

// GM-facing refusal: CDF reports save/load results as hints only and never says why a
// garrison refused. Show the reason in a CDF-style dialog (Ambient Destruction pattern).
modded class SCR_PlayerController
{
 override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)
 {
  super.CDF_GMSave_Feedback(result);
  string action;
  if (result.m_sKey == "#CDF_GMSave_Msg_LoadAborted") { action = "Load"; }
  else if (result.m_sKey == "#CDF_GMSave_Msg_CaptureFailed") { action = "Save"; }
  else { return; }
  string reason = EXPG_CDFGarrison.TakeRefusal();
  if (reason.IsEmpty()) { return; }
  string message = action + " refused by EXPBG Garrison:\n\n" + reason + "\n\nDetails: [EXPG CDF HOLD] in the server log.";
  // A hosting GM owns this controller locally; an owner RPC would not reach it.
  if (GetGame().GetPlayerController() == this) { EXPG_CDF_RpcDo_Refused(message); }
  else { Rpc(EXPG_CDF_RpcDo_Refused, message); }
 }

 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EXPG_CDF_RpcDo_Refused(string message)
 {
  EXPG_CDFRefusalDialog.Open(message);
 }
}

class EXPG_CDFRefusalDialog : CDF_GMSaveBaseDialog
{
 static void Open(string message)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager()) { return; }
  SCR_ConfigurableDialogUiPreset preset = CDF_GMSaveDialogUtils.CreatePreset("EXPBG_EXPG_CDF_REFUSED", "#CDF_GMSave_Hint_Title");
  preset.m_eVisualStyle = EDialogType.WARNING;
  preset.m_sMessage = message;
  preset.m_aButtons.Insert(CDF_GMSaveDialogUtils.CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "#CDF_GMSave_Btn_Close", EConfigurableDialogUiButtonAlign.LEFT, "MenuBack"));
  EXPG_CDFRefusalDialog dialog = new EXPG_CDFRefusalDialog();
  CreateByPreset(preset, dialog);
 }
}

modded class EBG_CacheSnapshot
{
 override static void ShutdownForWorldCleanup()
 {
  EXPG_CDFGarrison.Reset();
  super.ShutdownForWorldCleanup();
 }
}
