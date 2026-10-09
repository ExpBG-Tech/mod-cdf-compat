// TEST ONLY. CDF Game Master Save round trip of AI loadouts through the EXPBG CDF Compat
// inventory completion pass (addon/inventory-cdf, EINV_CDFInventory.c), in one diagnostic
// server.
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EINV_CDFInventoryRoundTrip.c -ExpectResult '\[EXPG EINV CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 soldiers=4 inventories=4 cdfLoss=\d+ completed=\d+ unplaced=0 idempotent=1 refusalKept=1 removedRestored=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file into its fixture addon; the driver class name
// EXPG_CdfRoundTrip is fixed by the runner's layer.
// Real CDF 1.4.1 code throughout: CDF_GMSaveCapture.Capture, SaveToFile/LoadFromFile,
// CDF_GMSaveRestore.Restore (Clear, spawning, deferred state pass) with character
// inventories on. One authored vanilla US fire team (no unit script). Inventories are
// compared as sorted prefab multisets walked by CDF's own capture traversal
// (CDF_GMSaveState.EINV_Live: the list a CDF save writes). Cases, in order:
//  1. Capture: each soldier's inventory in the frame of the save; his CDF record's saved
//     list ("ivp", read by EINV_CDF.ReadSaved) is that same multiset.
//  2. Load (clearBeforeLoad on): once the completion pass of the load finished and the
//     inventories settled, every soldier holds exactly the items he carried at the save
//     (inventories=4) and nothing stayed unplaced. cdfLoss counts the soldiers whose items
//     differed right after CDF applied their state (reported: native run 2026-10-09 lost
//     magazines and M67 grenades later, through CDF's engine fallback); completed is the
//     pass's own count of items it gave back.
//  3. Idempotence: the pass run again on the four complete soldiers adds nothing.
//  4. Refusals: one cargo item is deleted from a soldier; a pass that lists it among CDF's
//     refusals leaves it out; a pass without that refusal gives exactly it back.
// Not covered: GM UI, a cold server restart, multiplayer and JIP, vehicles and crates,
// player characters (CDF does not restore them; the pass skips any player entity).
class EINV_CdfExpect
{
 vector m_vSpot;
 string m_sItems;
 string m_sState;
 string m_sApplied;
 string m_sLoaded;
 IEntity m_Actor;
}

class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 // Seconds after the completion pass before inventories are compared.
 static const float SETTLE = 3;
 static const string FILE = "$profile:einv-cdf-roundtrip.json";
 // Characters CDF applied a state to in the current load, where and with what items.
 static ref array<IEntity> s_aApplied;
 static ref array<vector> s_aAppliedSpots;
 static ref array<string> s_aAppliedItems;
 vector m_vPoint = "4773.46 0 7094.57";
 SCR_AIGroup m_Team;
 ref array<ref EINV_CdfExpect> m_aExpect = {};
 ref CDF_GMSaveDocument m_Loaded;
 int m_iPhase;
 int m_iChecks;
 int m_iFailures;
 int m_iSoldiers;
 int m_iInventories;
 int m_iCdfLoss;
 int m_iCompleted;
 int m_iUnplaced;
 int m_iIdempotent;
 int m_iRefusalKept;
 int m_iRemovedRestored;
 int m_iPasses;
 string m_sRemoved;
 EINV_CdfExpect m_Target;
 float m_fStarted;
 float m_fNext;
 float m_fPhaseStarted;
 float m_fIdleSince;
 bool m_bFinished;

 void EXPG_CdfRoundTrip(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  m_fStarted = Now();
  m_fNext = m_fStarted + 15;
  PrintFormat("[EXPG EINV CDF ROUNDTRIP BEGIN] cdf=%1 deadline=%2", CDF_GMSave.VERSION, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  m_iChecks++;
  if (!value) m_iFailures++;
  PrintFormat("[EXPG EINV CDF ROUNDTRIP CHECK] pass=%1 %2", value, label);
  return value;
 }
 void Finish(string reason)
 {
  if (m_bFinished)
  {
   return;
  }
  m_bFinished = true;
  ClearEventMask(EntityEvent.FRAME);
  string first = string.Format("checks=%1 failures=%2 soldiers=%3 inventories=%4 cdfLoss=%5 completed=%6", m_iChecks, m_iFailures, m_iSoldiers, m_iInventories, m_iCdfLoss, m_iCompleted);
  string second = string.Format("unplaced=%1 idempotent=%2 refusalKept=%3 removedRestored=%4 reason=%5", m_iUnplaced, m_iIdempotent, m_iRefusalKept, m_iRemovedRestored, reason);
  PrintFormat("[EXPG EINV CDF ROUNDTRIP RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  m_iPhase = phase;
  m_fPhaseStarted = Now();
  m_fIdleSince = 0;
  m_iPasses = EINV_CDF.s_iPasses;
  PrintFormat("[EXPG EINV CDF ROUNDTRIP PHASE] phase=%1 elapsed=%2", m_iPhase, Now() - m_fStarted);
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - m_fPhaseStarted <= seconds)
  {
   return false;
  }
  Check(false, label);
  Finish("phase " + m_iPhase.ToString());
  return true;
 }
 // True once a completion pass finished after this phase began and SETTLE seconds passed.
 bool PassDone()
 {
  if (EINV_CDF.s_iPasses <= m_iPasses || !EINV_CDF.IsIdle())
  {
   m_fIdleSince = 0;
   return false;
  }
  if (m_fIdleSince <= 0) m_fIdleSince = Now();
  return Now() - m_fIdleSince >= SETTLE;
 }
 EntitySpawnParams Params(vector point)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + 0.3;
  spawn.Transform[3] = point;
  return spawn;
 }
 // A Game Master's author: CDF captures (and clears) authored entities only.
 void Author(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable)
  {
   return;
  }
  SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
  author.Initialize("einv-cdf-roundtrip", "", 0, -1);
  editable.SetAuthor(author);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 protected array<IEntity> m_aNear;
 bool CollectSoldier(IEntity entity)
 {
  if (m_aNear && SCR_ChimeraCharacter.Cast(entity)) m_aNear.Insert(entity);
  return true;
 }
 // Living AI soldiers within 30 m of the team's spot.
 int Soldiers(notnull array<SCR_ChimeraCharacter> found)
 {
  found.Clear();
  array<IEntity> near = {};
  m_aNear = near;
  GetGame().GetWorld().QueryEntitiesBySphere(m_vPoint, 30, CollectSoldier);
  m_aNear = null;
  foreach (IEntity entity : near)
  {
   SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
   if (soldier && soldier.GetCharacterController() && !soldier.GetCharacterController().IsDead()) found.Insert(soldier);
  }
  return found.Count();
 }
 static string Join(notnull array<string> names)
 {
  names.Sort();
  string joined;
  foreach (string name : names) joined += name + ";";
  return joined;
 }
 // Sorted prefab names of the entity's inventory as a CDF save walks it.
 static string Items(IEntity entity)
 {
  array<string> names = {};
  CDF_GMSaveState.EINV_Live(entity, names);
  return Join(names);
 }
 // Prefab names listed in from and not in to (multisets of Items lists).
 static string Missing(string from, string to)
 {
  array<string> wanted = {};
  array<string> left = {};
  from.Split(";", wanted, true);
  to.Split(";", left, true);
  string missing;
  foreach (string name : wanted)
  {
   int at = left.Find(name);
   if (at < 0)
   {
    missing += name + ";";
    continue;
   }
   left.Remove(at);
  }
  if (missing.IsEmpty())
  {
   return "none";
  }
  return missing;
 }
 static void ResetApplied()
 {
  s_aApplied = {};
  s_aAppliedSpots = {};
  s_aAppliedItems = {};
 }
 // Fixture CDF_GMSaveState.Apply wrapper (end of file): the character's items the moment
 // CDF applied his state, before the completion pass.
 static void NoteApplied(IEntity entity)
 {
  SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
  if (!soldier || !s_aApplied)
  {
   return;
  }
  s_aApplied.Insert(soldier);
  s_aAppliedSpots.Insert(soldier.GetOrigin());
  s_aAppliedItems.Insert(Items(soldier));
 }
 // The character CDF applied nearest the saved spot (within 1.5 m), or -1.
 static int AppliedAt(vector spot)
 {
  int best = -1;
  if (!s_aAppliedSpots)
  {
   return best;
  }
  float bestDistance = 1.5;
  foreach (int index, vector appliedSpot : s_aAppliedSpots)
  {
   float distance = vector.DistanceXZ(appliedSpot, spot);
   if (distance >= bestDistance) continue;
   best = index;
   bestDistance = distance;
  }
  return best;
 }
 // Completion pass on one soldier against his saved CDF lists, with optional refusals.
 bool Track(EINV_CdfExpect expect, notnull array<string> refused)
 {
  array<string> prefabs = {};
  array<int> owners = {};
  array<int> storages = {};
  array<int> slots = {};
  array<int> ammo = {};
  array<int> structural = {};
  if (!expect.m_Actor || !EINV_CDF.ReadSaved(expect.m_sState, prefabs, owners, storages, slots, ammo, structural))
  {
   return false;
  }
  return EINV_CDF.Track(expect.m_Actor, prefabs, owners, storages, slots, ammo, structural, refused);
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_bFinished || Now() < m_fNext)
  {
   return;
  }
  m_fNext = Now() + 0.5;
  if (Now() - m_fStarted > FIXTURE_SECONDS)
  {
   Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, m_iPhase));
   Finish("timeout");
   return;
  }
  Step();
 }
 void Step()
 {
  if (m_iPhase == 0)
  {
   Setup();
   return;
  }
  if (m_iPhase == 1)
  {
   CaptureAndLoad();
   return;
  }
  if (m_iPhase == 2)
  {
   CheckLoaded();
   return;
  }
  if (m_iPhase == 3)
  {
   CheckIdempotent();
   return;
  }
  if (m_iPhase == 4)
  {
   CheckRemoved();
   return;
  }
  if (m_iPhase == 5)
  {
   CheckRefusal();
   return;
  }
  if (m_iPhase == 6)
  {
   CheckRestored();
  }
 }
 void Setup()
 {
  CDF_GMSaveConfig cfg = CDF_GMSaveConfig.GetInstance();
  if (!Check(cfg != null, "CDF configuration"))
  {
   Finish("setup");
   return;
  }
  cfg.m_bClearBeforeLoad = true;
  cfg.m_bRepairDuplicatesOnLoad = true;
  cfg.m_bUsePersistenceBlob = false;
  cfg.m_bCaptureOnlyAuthored = true;
  cfg.m_bAutoSaveEnabled = false;
  cfg.m_bSaveInventories = true;
  cfg.m_bSaveCharacterInventories = true;
  m_vPoint[1] = GetGame().GetWorld().GetSurfaceY(m_vPoint[0], m_vPoint[2]);
  ResourceName teamName = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(teamName);
  IEntity teamEntity = GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(m_vPoint));
  m_Team = SCR_AIGroup.Cast(teamEntity);
  if (!Check(m_Team != null, "authored US fire team spawned"))
  {
   Finish("setup");
   return;
  }
  Author(m_Team);
  m_Team.EBG_Exclude = true;
  Advance(1);
 }
 void CaptureAndLoad()
 {
  if (m_Team.GetAgentsCount() < 4 || Now() - m_fPhaseStarted < 6)
  {
   Waited(40, "the team has four AI members within 40 seconds");
   return;
  }
  // 1. Capture: every soldier's spot and items in the same frame as the save.
  array<SCR_ChimeraCharacter> soldiers = {};
  Soldiers(soldiers);
  foreach (SCR_ChimeraCharacter soldier : soldiers)
  {
   EINV_CdfExpect expect = new EINV_CdfExpect();
   expect.m_vSpot = soldier.GetOrigin();
   expect.m_sItems = Items(soldier);
   expect.m_Actor = soldier;
   m_aExpect.Insert(expect);
  }
  CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("einv-cdf-roundtrip", "fixture");
  if (!Check(document != null && m_aExpect.Count() == 4, string.Format("CDF captured a document with the four soldiers (%1)", m_aExpect.Count())))
  {
   Finish("capture");
   return;
  }
  int matched;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   IEntity saved;
   if (record.m_Entity) saved = record.m_Entity.GetOwner();
   foreach (EINV_CdfExpect candidate : m_aExpect)
   {
    if (candidate.m_Actor != saved) continue;
    array<string> prefabs = {};
    array<int> owners = {};
    array<int> storages = {};
    array<int> slots = {};
    array<int> ammo = {};
    array<int> structural = {};
    bool read = EINV_CDF.ReadSaved(record.m_sState, prefabs, owners, storages, slots, ammo, structural);
    candidate.m_sState = record.m_sState;
    if (read && Join(prefabs) == candidate.m_sItems) matched++;
    else PrintFormat("[EXPG EINV CDF ROUNDTRIP ITEMS] saved list differs from the soldier at the save: read=%1 missing=%2 extra=%3", read, Missing(candidate.m_sItems, Join(prefabs)), Missing(Join(prefabs), candidate.m_sItems));
   }
  }
  Check(matched == 4, string.Format("each soldier's CDF record lists exactly his items (%1 of 4)", matched));
  if (!Check(document.SaveToFile(FILE), "document written to " + FILE))
  {
   Finish("file");
   return;
  }
  m_Loaded = new CDF_GMSaveDocument();
  if (!Check(m_Loaded.LoadFromFile(FILE), "document read back from the file"))
  {
   Finish("file");
   return;
  }
  // 2. Load.
  ResetApplied();
  Advance(2);
  if (!Check(CDF_GMSaveRestore.Restore(m_Loaded), "CDF restore with clearBeforeLoad"))
  {
   Finish("restore");
  }
 }
 void CheckLoaded()
 {
  int applied;
  if (s_aApplied) applied = s_aApplied.Count();
  if (applied < 4 || !PassDone())
  {
   Waited(40, string.Format("CDF applied four soldier states and the completion pass finished within 40 seconds (%1)", applied));
   return;
  }
  array<SCR_ChimeraCharacter> back = {};
  int found = Soldiers(back);
  Check(found == 4, string.Format("CDF restored the four soldiers once (%1)", found));
  m_iCompleted = EINV_CDF.s_iLastCompleted;
  m_iUnplaced = EINV_CDF.s_iLastUnplaced;
  Check(EINV_CDF.s_iLastChecked == 4, string.Format("the pass checked the four soldiers (%1)", EINV_CDF.s_iLastChecked));
  foreach (EINV_CdfExpect expect : m_aExpect)
  {
   expect.m_Actor = null;
   int index = AppliedAt(expect.m_vSpot);
   if (index < 0) continue;
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(s_aApplied[index]);
   if (!actor || actor.IsDeleted()) continue;
   m_iSoldiers++;
   expect.m_Actor = actor;
   expect.m_sApplied = s_aAppliedItems[index];
   expect.m_sLoaded = Items(actor);
   if (expect.m_sApplied != expect.m_sItems)
   {
    m_iCdfLoss++;
    PrintFormat("[EXPG EINV CDF ROUNDTRIP CDF ITEMS] right after CDF applied the state: missing=%1 extra=%2", Missing(expect.m_sItems, expect.m_sApplied), Missing(expect.m_sApplied, expect.m_sItems));
   }
   if (expect.m_sLoaded == expect.m_sItems)
   {
    m_iInventories++;
    continue;
   }
   PrintFormat("[EXPG EINV CDF ROUNDTRIP ITEMS] after the completion pass, against the save: missing=%1 extra=%2", Missing(expect.m_sItems, expect.m_sLoaded), Missing(expect.m_sLoaded, expect.m_sItems));
  }
  Check(m_iSoldiers == 4, string.Format("the four saved soldiers are found on their saved spots (%1)", m_iSoldiers));
  Check(m_iInventories == 4, string.Format("every soldier holds exactly the items he carried at the save (%1 of 4)", m_iInventories));
  Check(m_iUnplaced == 0, string.Format("no saved item stayed unplaced (%1)", m_iUnplaced));
  // 3. Idempotence: the pass again on complete soldiers.
  int tracked;
  array<string> none = {};
  Advance(3);
  foreach (EINV_CdfExpect again : m_aExpect)
  {
   if (Track(again, none)) tracked++;
  }
  if (!Check(tracked == 4, string.Format("the pass runs again on the four soldiers (%1)", tracked)))
  {
   Finish("idempotence");
  }
 }
 void CheckIdempotent()
 {
  if (!PassDone())
  {
   Waited(20, "the repeated pass finished within 20 seconds");
   return;
  }
  int same;
  foreach (EINV_CdfExpect expect : m_aExpect)
  {
   if (expect.m_Actor && Items(expect.m_Actor) == expect.m_sItems) same++;
  }
  if (Check(EINV_CDF.s_iLastCompleted == 0 && EINV_CDF.s_iLastUnplaced == 0 && same == 4, string.Format("a repeated pass adds nothing (completed %1, unchanged %2 of 4)", EINV_CDF.s_iLastCompleted, same))) m_iIdempotent = 1;
  // 4. Remove one cargo item of a soldier (the last cargo entry of his saved list).
  foreach (EINV_CdfExpect holder : m_aExpect)
  {
   if (!holder.m_Actor || m_Target) continue;
   array<string> prefabs = {};
   array<int> owners = {};
   array<int> storages = {};
   array<int> slots = {};
   array<int> ammo = {};
   array<int> structural = {};
   if (!EINV_CDF.ReadSaved(holder.m_sState, prefabs, owners, storages, slots, ammo, structural)) continue;
   for (int i = prefabs.Count() - 1; i >= 0 && !m_Target; i--)
   {
    if (structural[i] != 0 || !RemoveOne(holder.m_Actor, prefabs[i])) continue;
    m_Target = holder;
    m_sRemoved = prefabs[i];
   }
  }
  if (!Check(m_Target != null, "one cargo item removed from a soldier"))
  {
   Finish("remove");
   return;
  }
  Advance(4);
 }
 // Deletes one item of this prefab from a deposit (cargo) storage of the soldier.
 bool RemoveOne(IEntity soldier, string prefab)
 {
  InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(soldier.FindComponent(InventoryStorageManagerComponent));
  if (!manager)
  {
   return false;
  }
  array<IEntity> carried = {};
  manager.GetItems(carried, EStoragePurpose.PURPOSE_ANY);
  foreach (IEntity item : carried)
  {
   if (!item || CDF_GMSaveState.GetPrefabName(item) != prefab) continue;
   InventoryItemComponent inventoryItem = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
   if (!inventoryItem || !inventoryItem.GetParentSlot() || !inventoryItem.GetParentSlot().GetStorage()) continue;
   if (inventoryItem.GetParentSlot().GetStorage().GetPurpose() != EStoragePurpose.PURPOSE_DEPOSIT) continue;
   return manager.TryDeleteItem(item);
  }
  return false;
 }
 void CheckRemoved()
 {
  if (Now() - m_fPhaseStarted < 2)
  {
   return;
  }
  string now = Items(m_Target.m_Actor);
  Check(Missing(m_Target.m_sItems, now) == m_sRemoved + ";" && Missing(now, m_Target.m_sItems) == "none", "exactly the removed item is missing: " + m_sRemoved);
  // A pass where CDF refused that prefab leaves it out.
  array<string> refused = {};
  refused.Insert(m_sRemoved);
  Advance(5);
  if (!Check(Track(m_Target, refused), "the pass runs with the removed item among CDF's refusals"))
  {
   Finish("refusal");
  }
 }
 void CheckRefusal()
 {
  if (!PassDone())
  {
   Waited(20, "the refusal pass finished within 20 seconds");
   return;
  }
  string now = Items(m_Target.m_Actor);
  if (Check(EINV_CDF.s_iLastCompleted == 0 && EINV_CDF.s_iLastRefused == 1 && Missing(m_Target.m_sItems, now) == m_sRemoved + ";", string.Format("an item CDF refused stays out (completed %1, refused %2)", EINV_CDF.s_iLastCompleted, EINV_CDF.s_iLastRefused))) m_iRefusalKept = 1;
  // Without the refusal the pass gives exactly that item back.
  array<string> none = {};
  Advance(6);
  if (!Check(Track(m_Target, none), "the pass runs without refusals"))
  {
   Finish("restore item");
  }
 }
 void CheckRestored()
 {
  if (!PassDone())
  {
   Waited(20, "the restoring pass finished within 20 seconds");
   return;
  }
  string now = Items(m_Target.m_Actor);
  if (Check(EINV_CDF.s_iLastCompleted == 1 && EINV_CDF.s_iLastUnplaced == 0 && now == m_Target.m_sItems, string.Format("exactly the removed item is back (completed %1): missing=%2 extra=%3", EINV_CDF.s_iLastCompleted, Missing(m_Target.m_sItems, now), Missing(now, m_Target.m_sItems)))) m_iRemovedRestored = 1;
  Finish("completed");
 }
}

// TEST ONLY. Notes each character's items once CDF applied his state (deep state pass),
// whichever way the adapters wrap this call.
modded class CDF_GMSaveState
{
 override static void Apply(IEntity entity, string state)
 {
  super.Apply(entity, state);
  EXPG_CdfRoundTrip.NoteApplied(entity);
 }
}
