// Optional CDF 1.4.1 adapter. No CDF types or dependency in the standalone EAD addon.
class EAD_CDF
{
 static bool CaptureFailed, Failed, RepairHookRan;
 static string Reason;
 static ref CDF_GMSaveDocument Pending, Recovery;
 static ref EAD_BuildingRestore Buildings;
 static BaseWorld ActiveWorld;
 static SCR_BaseGameMode Mode;
 static int FinishAttempts;
 static bool IsZone(ResourceName prefab) { return prefab.Contains("EAD1000000000010"); }
 static bool Declared(string text, string key) { return text.Contains("\"" + key + "\""); }
 static string Wrap(string key, string payload, string original)
 {
  JsonSaveContext ctx = new JsonSaveContext();
  ctx.WriteValue(key, payload); ctx.WriteValue("cdfOriginal", original);
  return ctx.SaveToString();
 }
 static bool Unwrap(string text, string key, out string payload, out string original)
 {
  JsonLoadContext ctx = new JsonLoadContext();
  return ctx.LoadFromString(text) && ctx.ReadValue(key, payload) && ctx.ReadValue("cdfOriginal", original);
 }
 // First refusal of the current request, shown to the requesting GM with CDF's feedback.
 static string LastRefusal;
 static int LastRefusalTick;
 static const int REFUSAL_WINDOW_MS = 2000;
 static void Reject(string reason)
 {
  Print("[EAD CDF HOLD] " + reason, LogLevel.ERROR);
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
 static void Fail(string reason)
 {
  Failed = true; Reason = reason; Reject(reason);
 }
 static bool CurrentWorld()
 {
  return GetGame() && GetGame().GetWorld() == ActiveWorld && GetGame().GetGameMode() == Mode;
 }
 static bool ManagedBuilding(EAD_BuildingChoice choice, IEntity entity)
 {
  if (choice.Authored) return true;
  SCR_EditableEntityComponent editable;
  if (entity) editable = SCR_EditableEntityComponent.Cast(entity.FindComponent(SCR_EditableEntityComponent));
  return editable && !editable.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE) && CDF_GMSaveCapture.IsManaged(editable);
 }
}
modded class CDF_GMSaveState
{
 override static string Capture(IEntity entity)
 {
  string original = super.Capture(entity);
  EAD_Zone zone = EAD_Zone.Cast(entity);
  if (!zone) return original;
  string payload;
  if (!EAD_Snapshot.WriteZone(zone, payload))
  {
   EAD_CDF.CaptureFailed = true;
   EAD_CDF.Reject("Zone not ready for exact capture; wait for scenery generation");
   return original;
  }
  return EAD_CDF.Wrap("eadZone", payload, original);
 }
 override static void Apply(IEntity entity, string state)
 {
  EAD_Zone zone = EAD_Zone.Cast(entity);
  if (!zone || !EAD_CDF.Declared(state, "eadZone")) { super.Apply(entity, state); return; }
  string payload, original;
  if (!EAD_CDF.Unwrap(state, "eadZone", payload, original)) { EAD_CDF.Fail("Invalid zone envelope"); return; }
  EAD_ZoneSnapshot data = EAD_Snapshot.ReadZone(payload);
  if (!data) { EAD_CDF.Fail("Invalid zone snapshot"); return; }
  super.Apply(entity, original);
  if (!zone.ImportSnapshot(data)) EAD_CDF.Fail("Zone snapshot import rejected");
 }
}
modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  if (!Replication.IsServer()) return null;
  if (EAD_CDF.ActiveWorld && !EAD_CDF.CurrentWorld()) CDF_GMSaveRestore.EAD_ResetForWorld();
  if (EAD_Snapshot.Loading || EAD_CDF.Pending || EAD_CDF.Recovery)
  {
   EAD_CDF.Reject("Import pending/failed; original save retained, overwrite blocked"); return null;
  }
  array<EAD_Zone> zones = {}; EAD_World.GetZones(zones);
  foreach (EAD_Zone zone : zones)
  {
   if (zone && !zone.CanCapture()) { EAD_CDF.Reject("Wait for scenery generation before saving"); return null; }
  }
  array<ref EAD_BuildingChoice> choices = {}; EAD_Buildings.EnsureWorld(GetGame().GetWorld()); EAD_Buildings.GetLedger(choices);
  if (zones.IsEmpty() && choices.IsEmpty()) return super.Capture(displayName, author);
  foreach (EAD_BuildingChoice choice : choices)
  {
   if (EAD_CDF.ManagedBuilding(choice, choice.Entity)) { EAD_CDF.Reject("Exact map ledger cannot include CDF-managed buildings; save rejected before writing"); return null; }
  }
  string buildings;
  if (!EAD_BuildingSnapshot.Write(buildings)) { EAD_CDF.Reject("Building ledger is incomplete or exceeds its save limit"); return null; }
  EAD_CDF.CaptureFailed = false;
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  if (!document || EAD_CDF.CaptureFailed) return null;
  foreach (EAD_Zone zone : zones)
  {
   if (!zone) continue;
   bool found;
   foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
   {
    if (!record.m_Entity || record.m_Entity.GetOwner() != zone) continue;
    string payload, original;
    found = EAD_CDF.Unwrap(record.m_sState, "eadZone", payload, original);
    break;
   }
   if (!found) { EAD_CDF.Reject("CDF omitted an Ambient Destruction module; save rejected"); return null; }
  }
  document.m_sWorldState = EAD_CDF.Wrap("eadBuildings", buildings, document.m_sWorldState);
  return document;
 }
}
modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  if (EAD_CDF.ActiveWorld && !EAD_CDF.CurrentWorld()) EAD_ResetForWorld();
  if (!Replication.IsServer()) return false;
  if (EAD_Snapshot.Loading || EAD_CDF.Pending) { EAD_CDF.Reject("A previous Ambient Destruction import is still running or failed; wait, or restart the mission before loading"); return false; }
  bool envelope = EAD_CDF.Declared(document.m_sWorldState, "eadBuildings");
  bool hasZone;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (EAD_CDF.IsZone(record.m_sPrefab)) { hasZone = true; break; }
  }
  // Preserve ordinary CDF handling (including skipped missing prefabs) when this
  // document has no EAD state. Strict validation protects only our transaction.
  if (!envelope && !hasZone) return super.Restore(document);
  array<vector> zones = {};
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   // Missing any prefab must be rejected before CDF clears the current scene.
   Resource prefab = Resource.Load(record.m_sPrefab);
   if (!prefab || !prefab.IsValid()) { EAD_CDF.Reject("Missing saved prefab: " + record.m_sPrefab); return false; }
   if (!EAD_CDF.IsZone(record.m_sPrefab)) continue;
   string payload, original;
   if (!envelope || !EAD_CDF.Unwrap(record.m_sState, "eadZone", payload, original)) { EAD_CDF.Reject("Save has no exact EAD snapshot; cannot promise layout restoration"); return false; }
   EAD_ZoneSnapshot data = EAD_Snapshot.ReadZone(payload);
   if (!data || vector.DistanceSq(record.m_vPosition, data.Origin) > 0.01) { EAD_CDF.Reject("Saved zone position/schema/resources are invalid"); return false; }
   foreach (vector origin : zones)
   {
    if (vector.DistanceSq(origin, data.Origin) < 0.01) { EAD_CDF.Reject("Duplicate saved module origin"); return false; }
   }
   zones.Insert(data.Origin);
   if (zones.Count() > EAD_World.MAX_ZONES) { EAD_CDF.Reject("Save exceeds the zone scheduler limit"); return false; }
  }
  if (!CDF_GMSaveConfig.GetInstance().m_bClearBeforeLoad) { EAD_CDF.Reject("Exact EAD restore requires CDF clearBeforeLoad"); return false; }
  if (!CDF_GMSaveConfig.GetInstance().m_bRepairDuplicatesOnLoad) { EAD_CDF.Reject("Exact EAD restore requires CDF repairDuplicatesOnLoad"); return false; }
  string payload, original;
  if (!EAD_CDF.Unwrap(document.m_sWorldState, "eadBuildings", payload, original)) { EAD_CDF.Reject("Invalid building envelope"); return false; }
  EAD_BuildingSnapshot data = EAD_BuildingSnapshot.Read(payload);
  if (!data) { EAD_CDF.Reject("Invalid building snapshot or different world"); return false; }
  EAD_BuildingRestore buildings = new EAD_BuildingRestore(data);
  if (!buildings.Preflight()) { EAD_CDF.Reject(buildings.Reason); return false; }
  foreach (EAD_BuildingChoice choice : data.Choices)
  {
   if (EAD_CDF.ManagedBuilding(choice, buildings.Find(choice))) { EAD_CDF.Reject("Saved map building would be deleted by CDF Clear"); return false; }
  }
  EAD_CDF.ActiveWorld = GetGame().GetWorld(); EAD_CDF.Mode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  EAD_Snapshot.ImportWorld = EAD_CDF.ActiveWorld;
  EAD_CDF.Pending = document; EAD_CDF.Buildings = buildings;
  EAD_CDF.Failed = false; EAD_CDF.RepairHookRan = false; EAD_CDF.Reason = ""; EAD_CDF.FinishAttempts = 0;
  EAD_Snapshot.Loading = true;
  // CDF restores world attributes synchronously inside Restore; pass its exact opaque input.
  string wrappedWorld = document.m_sWorldState;
  string factionState = document.m_sFactionState;
  array<ref CDF_GMSaveEntityRecord> originalRecords = document.m_aEntities;
  document.m_sWorldState = original;
  bool started = super.Restore(document);
  document.m_sWorldState = wrappedWorld;
  if (EAD_CDF.Failed)
  {
   document.m_aEntities = originalRecords;
   document.m_sFactionState = factionState;
  }
  PrintFormat("[EAD CDF DISPATCH] repairHook=%1 records=%2", EAD_CDF.RepairHookRan, document.m_aEntities.Count());
  if (!started && !EAD_CDF.RepairHookRan)
  {
   // An inner companion rejected preflight: no EAD/CDF clear occurred, so keep the
   // existing scene usable. Do not turn another bridge's rejection into an EAD hold.
   EAD_CDF.Pending = null; EAD_CDF.Buildings = null;
   EAD_Snapshot.Loading = false; EAD_Snapshot.ImportWorld = null;
   EAD_CDF.Reject("Companion preflight rejected before clearing; existing EAD state retained");
   return false;
  }
  if (!started || EAD_CDF.Failed || !EAD_CDF.RepairHookRan)
  {
   EAD_CDF.Fail("CDF restore failed; original document retained"); EAD_CDF.Recovery = document; EAD_CDF.Pending = null;
   // Keep EAD stopped after a partial import; a fresh world is the recovery boundary.
   return false;
  }
  GetGame().GetCallqueue().CallLater(EAD_FinishImport, 600, false);
  return true;
 }
 protected static void EAD_FinishImport()
 {
  if (!EAD_CDF.Pending) return;
  if (!EAD_CDF.CurrentWorld()) { EAD_ResetForWorld(); return; }
  bool nativeComplete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty() && s_aGuardedGroups && s_aGuardedGroups.IsEmpty() && s_aPendingMembers && s_aPendingMembers.IsEmpty();
  if (!nativeComplete)
  {
   EAD_CDF.FinishAttempts++;
   if (EAD_CDF.FinishAttempts < 20) { GetGame().GetCallqueue().CallLater(EAD_FinishImport, 100, false); return; }
   EAD_CDF.Fail("CDF deferred finalization timed out");
  }
  if (!EAD_CDF.Failed)
  {
   foreach (CDF_GMSaveEntityRecord record : EAD_CDF.Pending.m_aEntities)
   {
    if (!EAD_CDF.IsZone(record.m_sPrefab)) continue;
    EAD_Zone zone;
    if (record.m_Entity) zone = EAD_Zone.Cast(record.m_Entity.GetOwner());
    if (!zone || !zone.HasImportedSnapshot()) { EAD_CDF.Fail("CDF did not restore every EAD module"); break; }
   }
  }
  EAD_CDF.Recovery = null;
  if (EAD_CDF.Failed) EAD_CDF.Recovery = EAD_CDF.Pending;
  EAD_CDF.Pending = null; EAD_CDF.Buildings = null;
  EAD_Snapshot.Loading = EAD_CDF.Failed;
  PrintFormat("[EAD CDF LOAD FINALIZED] success=%1 nativeComplete=%2 reason=%3", !EAD_CDF.Failed, nativeComplete, EAD_CDF.Reason);
 }
 static void EAD_ResetForWorld()
 {
  if (GetGame()) GetGame().GetCallqueue().Remove(EAD_FinishImport);
  EAD_CDF.Pending = null; EAD_CDF.Recovery = null; EAD_CDF.Buildings = null;
  EAD_CDF.ActiveWorld = null; EAD_CDF.Mode = null; EAD_CDF.Failed = false; EAD_CDF.CaptureFailed = false;
  EAD_CDF.RepairHookRan = false;
  EAD_Snapshot.Loading = false;
  EAD_Snapshot.ImportWorld = null;
 }
}
modded class CDF_GMSaveRepair
{
 override static int RemoveStackedDuplicates(notnull CDF_GMSaveDocument document)
 {
  if (EAD_CDF.Pending != document || !EAD_Snapshot.Loading || !EAD_CDF.CurrentWorld()) return super.RemoveStackedDuplicates(document);
  // CDF 1.4.1 calls this external class after Clear and before recording any spawn
  // count. Its own-class static Clear/SpawnRecord overrides are bypassed by Restore.
  EAD_CDF.RepairHookRan = true;
  int started = System.GetTickCount();
  for (int work = 0; work <= EAD_BuildingSnapshot.MAX_SAVED; work++)
  {
   EAD_CDF.Buildings.Step();
   if (EAD_CDF.Buildings.Failed || EAD_CDF.Buildings.Finished) break;
  }
  if (EAD_CDF.Buildings.Failed || !EAD_CDF.Buildings.Finished) EAD_CDF.Fail("Saved building replay failed: " + EAD_CDF.Buildings.Reason);
  PrintFormat("[EAD CDF BUILDINGS] completed=%1 failed=%2 importMs=%3", EAD_CDF.Buildings.Finished, EAD_CDF.Failed, System.GetTickCount() - started);
  if (!EAD_CDF.Failed) return super.RemoveStackedDuplicates(document);
  // Preserve original records before native repair can mutate link indices.
  // Base Restore must receive neither actors nor world/faction edits after failure.
  document.m_aEntities = {};
  document.m_sWorldState = "";
  document.m_sFactionState = "";
  return 0;
 }
}
// GM-facing refusal: CDF reports save/load results as hints only (invisible with hints
// disabled) and never says why EAD refused. Show the EAD reason in a CDF-style dialog.
modded class SCR_PlayerController
{
 override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)
 {
  super.CDF_GMSave_Feedback(result);
  string action;
  if (result.m_sKey == "#CDF_GMSave_Msg_LoadAborted") action = "Load";
  else if (result.m_sKey == "#CDF_GMSave_Msg_CaptureFailed") action = "Save";
  else return;
  string reason = EAD_CDF.TakeRefusal();
  if (reason.IsEmpty()) return;
  string message = action + " refused by EXPBG Ambient Destruction:\n\n" + reason + "\n\nDetails: [EAD CDF HOLD] in the server log.";
  // A hosting GM owns this controller locally; an owner RPC would not reach it.
  if (GetGame().GetPlayerController() == this) EAD_CDF_RpcDo_Refused(message);
  else Rpc(EAD_CDF_RpcDo_Refused, message);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EAD_CDF_RpcDo_Refused(string message)
 {
  EAD_CDFRefusalDialog.Open(message);
 }
}
class EAD_CDFRefusalDialog : CDF_GMSaveBaseDialog
{
 static void Open(string message)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager()) return;
  SCR_ConfigurableDialogUiPreset preset = CDF_GMSaveDialogUtils.CreatePreset("EXPBG_EAD_CDF_REFUSED", "#CDF_GMSave_Hint_Title");
  preset.m_eVisualStyle = EDialogType.WARNING;
  preset.m_sMessage = message;
  preset.m_aButtons.Insert(CDF_GMSaveDialogUtils.CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "#CDF_GMSave_Btn_Close", EConfigurableDialogUiButtonAlign.LEFT, "MenuBack"));
  EAD_CDFRefusalDialog dialog = new EAD_CDFRefusalDialog();
  CreateByPreset(preset, dialog);
 }
}
