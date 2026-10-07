// TEST ONLY. CDF Game Master Save round trip of EXPBG GM Tools garrisons through the
// EXPBG CDF Compat garrison bridge (EXPG_CDFGarrisonBridge.c), in one diagnostic server.
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EXPG_CDFGarrisonRoundTrip.c -ExpectResult '\[EXPG CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 garrisons=3 documentWritten=1 ownedRecords=0 ordinary=1 appendRefused=1 posts=1 patrollers=[1-9]\d* respawnedDead=0 duplicates=0 aiBeforeBind=0 fullWoke=1 corruptRefused=1 corruptSkipped=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// Real CDF 1.4.1 code throughout: CDF_GMSaveCapture.Capture, SaveToFile/LoadFromFile,
// CDF_GMSaveRestore.Restore (Clear, spawning, deferred finalization). Garrisons as in
// the GM Tools ledger fixture: A awake (caching Off) and C Full (interior patrollers) in
// a map town house, B Simulation in a village house; one casualty in A and in C; an
// ordinary authored fire team D far away. All squads carry an author, so CDF would
// capture them. Cases, in order:
//  1. Capture: a document is written; no record belongs to a garrison (the ledger
//     carries them), D is in it; the file is written and read back.
//  2. Append load (clearBeforeLoad off) is refused before anything changes.
//  3. Load (clearBeforeLoad on): CDF clears the scene (garrisons included), the bridge
//     discards the old garrisons and, after CDF's finalization, the ledger restores
//     the same three garrisons: same buildings, posts and stops within 0.3 m local,
//     patrollers, A awake, B Simulation again, C Full with nobody in the world; no
//     casualty respawned, no duplicate soldier or squad, D restored once by CDF. A
//     restored soldier's AI never runs before he is bound.
//  4. C woken: survivors on their posts.
//  5. A save whose ledger cannot be read: the first load is refused untouched; the
//     second (confirmation) loads it without garrisons.
// Not covered: GM UI and dialogs, a cold server restart, multiplayer and JIP.
modded class EXPG_GarrisonManager
{
 EXPG_GarrisonRecord EXPG_TestByToken(string token)
 {
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.Token == token) return record;
  }
  return null;
 }

 int EXPG_TestTokenCount(string token)
 {
  int count = 0;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished && record.Token == token) count++;
  }
  return count;
 }

 int EXPG_TestActive()
 {
  int count = 0;
  foreach (EXPG_GarrisonRecord record : m_Records)
  {
   if (!record.Finished) count++;
  }
  return count;
 }
}

modded class EXPG_GarrisonRecord
{
 static int EXPG_TestReleases;
 static string EXPG_TestReleaseReason;
 override void RequestRelease(string reason)
 {
  if (!ReleaseRequested && reason != "a CDF save was loaded")
  {
   EXPG_TestReleases++;
   EXPG_TestReleaseReason = reason;
   PrintFormat("[EXPG CDF ROUNDTRIP RELEASE] group=%1 reason=%2", Group, reason);
  }
  super.RequestRelease(reason);
 }
}

class EXPG_CdfExpect
{
 string Name;
 string Token;
 IEntity Structure;
 int Alive;
 int Patrollers;
 ref array<int> Ids = {};
 ref array<bool> Dead = {};
 ref array<bool> Fixed = {};
 ref array<vector> LocalPosts = {};
}

class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 575;
 static const int SQUAD = 4;
 static const string FILE = "$profile:expg-cdf-roundtrip.json";
 IEntity HouseA;
 IEntity HouseB;
 IEntity MapHouse;
 bool Baked;
 vector PointA;
 vector PointB = "4773.46 0 7094.57";
 vector PointD;
 EXPG_GarrisonManager Manager;
 SCR_AIGroup GroupA;
 SCR_AIGroup GroupB;
 SCR_AIGroup GroupC;
 SCR_AIGroup GroupD;
 EXPG_GarrisonRecord RecordA;
 EXPG_GarrisonRecord RecordB;
 EXPG_GarrisonRecord RecordC;
 ref array<ref EXPG_CdfExpect> Expect = {};
 ref array<IEntity> Holders = {};
 ref array<IEntity> BoundOnce = {};
 ref array<IEntity> Violations = {};
 ref CDF_GMSaveDocument Loaded;
 ref CDF_GMSaveDocument Corrupt;
 int Phase;
 int Checks;
 int Failures;
 int Garrisons;
 int DocumentWritten;
 int OwnedRecords = -1;
 int Ordinary;
 int AppendRefused;
 int PostsOk;
 int PatrollersRestored;
 int RespawnedDead;
 int Duplicates;
 int FullWoke;
 int CorruptRefused;
 int CorruptSkipped;
 int OrdinaryLiving;
 bool Watching;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;

 void EXPG_CdfRoundTrip(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer()) { ClearEventMask(EntityEvent.FRAME); return; }
  Started = Now();
  Next = Started + 15;
  PrintFormat("[EXPG CDF ROUNDTRIP BEGIN] cdf=%1 squads=4 deadline=%2", CDF_GMSave.VERSION, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG CDF ROUNDTRIP CHECK] pass=%1 %2", value, label);
  return value;
 }
 void Finish(string reason)
 {
  if (Finished) return;
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  string first = string.Format("checks=%1 failures=%2 garrisons=%3 documentWritten=%4 ownedRecords=%5 ordinary=%6 appendRefused=%7 posts=%8 patrollers=%9", Checks, Failures, Garrisons, DocumentWritten, OwnedRecords, Ordinary, AppendRefused, PostsOk, PatrollersRestored);
  string second = string.Format("respawnedDead=%1 duplicates=%2 aiBeforeBind=%3 fullWoke=%4 corruptRefused=%5 corruptSkipped=%6 reason=%7", RespawnedDead, Duplicates, Violations.Count(), FullWoke, CorruptRefused, CorruptSkipped, reason);
  PrintFormat("[EXPG CDF ROUNDTRIP RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG CDF ROUNDTRIP PHASE] phase=%1 elapsed=%2", Phase, Now() - Started);
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds) return false;
  Check(false, label);
  Finish("phase " + Phase.ToString());
  return true;
 }
 EntitySpawnParams Params(vector point, float elevation = 0)
 {
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  Math3D.AnglesToMatrix(vector.Zero, spawn.Transform);
  point[1] = GetGame().GetWorld().GetSurfaceY(point[0], point[2]) + elevation;
  spawn.Transform[3] = point;
  return spawn;
 }
 bool AddMapHouse(IEntity entity)
 {
  if (MapHouse || !SCR_DestructibleBuildingEntity.Cast(entity)) return true;
  ResourceName prefab = SCR_ResourceNameUtils.GetPrefabName(entity);
  if (!prefab.Contains("House_Town_E_2I01")) return true;
  MapHouse = entity;
  return false;
 }
 // A Game Master's author: CDF captures (and clears) authored entities only.
 void Author(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable) return;
  SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
  author.Initialize("expg-cdf-roundtrip", "", 0, -1);
  editable.SetAuthor(author);
  // As a Game Master's placement does: deleting an entity whose author is not
  // registered logs a vanilla script error.
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 SCR_AIGroup SpawnTeam(vector point)
 {
  ResourceName team = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(team);
  return SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(point, 0.3)));
 }
 SCR_AIGroup AddSquad(IEntity house, vector point, int mode, out EXPG_GarrisonRecord record)
 {
  string reason;
  if (!Check(Manager.CanFit(house, SQUAD, reason) && reason.IsEmpty(), "Add Garrison accepted by CanFit: " + reason)) return null;
  SCR_AIGroup group = SpawnTeam(point);
  bool fresh = group && group.EXPG_BeginFreshRoster(SQUAD);
  bool adopted = fresh && Manager.AdoptFresh(group, house, 0, SQUAD);
  if (!Check(adopted, "freshly spawned fire team adopted")) return null;
  Author(group);
  group.EXPG_CacheMode = mode;
  record = Manager.Find(group);
  if (!Check(record != null, "garrison record created")) return null;
  return group;
 }
 bool Bound(EXPG_GarrisonRecord record)
 {
  if (!record || record.Finished || !record.Ready || record.Full || record.Simulation || !record.Group) return false;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Dead) continue;
   SCR_ChimeraCharacter actor = member.CacheMember.Entity;
   if (!actor || actor.GetCharacterGroup() != record.Group) return false;
   if (member.Fixed && !member.Post) return false;
   if (!member.Fixed && !member.Patrol) return false;
  }
  return true;
 }
 bool SimulationAsleep(EXPG_GarrisonRecord record)
 {
  return record && !record.Full && record.Simulation && record.Simulation.Suspended;
 }
 bool FullAsleep(EXPG_GarrisonRecord record)
 {
  return record && record.Full && record.Full.GetState() == EBG_FullGroupPhase.CACHED && !record.Group;
 }
 void Kill(EXPG_GarrisonRecord record)
 {
  SCR_ChimeraCharacter actor = record.Members[record.Members.Count() - 1].CacheMember.Entity;
  if (actor) actor.GetDamageManager().Kill(Instigator.CreateInstigator(actor));
 }
 vector LocalPost(EXPG_GarrisonRecord record, EXPG_GarrisonMember member)
 {
  return record.Plan.Structure.CoordToLocal(member.PostPoint());
 }
 void Remember(string name, EXPG_GarrisonRecord record)
 {
  EXPG_CdfExpect saved = new EXPG_CdfExpect();
  saved.Name = name;
  saved.Token = record.Token;
  saved.Structure = record.Plan.Structure;
  foreach (EXPG_GarrisonMember member : record.Members)
  {
   saved.Ids.Insert(member.CacheMember.Id);
   saved.Dead.Insert(member.CacheMember.Dead);
   saved.Fixed.Insert(member.Fixed);
   saved.LocalPosts.Insert(LocalPost(record, member));
   if (!member.CacheMember.Dead) saved.Alive++;
   if (!member.CacheMember.Dead && !member.Fixed) saved.Patrollers++;
  }
  PrintFormat("[EXPG CDF ROUNDTRIP SAVED] garrison=%1 token=%2 alive=%3 members=%4 patrollers=%5", name, saved.Token, saved.Alive, saved.Ids.Count(), saved.Patrollers);
  Expect.Insert(saved);
 }
 int Agents(EXPG_GarrisonRecord record)
 {
  if (!record || !record.Group) return 0;
  return record.Group.GetAgentsCount();
 }
 // Living characters within 25 m of a point (duplicate soldiers show up here).
 int m_Counted;
 bool CountLiving(IEntity entity)
 {
  SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(entity);
  if (actor && !EXPG_GarrisonManager.IsDeadActor(actor)) m_Counted++;
  return true;
 }
 int LivingNear(vector point)
 {
  m_Counted = 0;
  GetGame().GetWorld().QueryEntitiesBySphere(point, 25, CountLiving);
  return m_Counted;
 }
 void SampleHolds()
 {
  foreach (EXPG_CdfExpect saved : Expect)
  {
   EXPG_GarrisonRecord record = Manager.EXPG_TestByToken(saved.Token);
   if (!record || record.ReleaseRequested) continue;
   foreach (EXPG_GarrisonMember member : record.Members)
   {
    SCR_ChimeraCharacter actor = member.CacheMember.Entity;
    if (!actor || member.CacheMember.Dead || member.CacheMember.WasPlayer) continue;
    if (member.Post || member.Patrol)
    {
     if (!BoundOnce.Contains(actor)) BoundOnce.Insert(actor);
     continue;
    }
    if (BoundOnce.Contains(actor) || Violations.Contains(actor)) continue;
    AIControlComponent control = actor.GetAIControlComponent();
    if (!control || !control.GetAIAgent() || control.GetAIAgent().GetPermanentLOD() == AIAgent.GetMaxLOD()) continue;
    Violations.Insert(actor);
    PrintFormat("[EXPG CDF ROUNDTRIP AI BEFORE BIND] garrison=%1 member=%2 permanentLOD=%3", saved.Name, member.CacheMember.Id, control.GetAIAgent().GetPermanentLOD());
   }
  }
 }
 bool VerifyRestored(EXPG_CdfExpect saved, EXPG_GarrisonRecord record, bool awake, bool counted = true)
 {
  bool ok = Check(record.Plan.Structure == saved.Structure, saved.Name + ": the same building holds the restored garrison");
  ok = Check(record.Members.Count() == saved.Ids.Count(), string.Format("%1: %2 member rows (saved %3)", saved.Name, record.Members.Count(), saved.Ids.Count())) && ok;
  if (!ok) return false;
  int alive = 0;
  int patrollers = 0;
  bool posts = true;
  foreach (int i, EXPG_GarrisonMember member : record.Members)
  {
   if (member.CacheMember.Id != saved.Ids[i] || member.CacheMember.Dead != saved.Dead[i]) { posts = false; continue; }
   if (member.CacheMember.Dead)
   {
    if (member.CacheMember.Entity) RespawnedDead++;
    continue;
   }
   alive++;
   if (!member.Fixed) patrollers++;
   float drift = vector.Distance(LocalPost(record, member), saved.LocalPosts[i]);
   if (drift > 0.3 || member.Fixed != saved.Fixed[i]) posts = false;
   PrintFormat("[EXPG CDF ROUNDTRIP POST] garrison=%1 member=%2 fixed=%3 localDrift=%4", saved.Name, member.CacheMember.Id, member.Fixed, drift);
  }
  ok = Check(alive == saved.Alive, string.Format("%1: %2 living guards (saved %3)", saved.Name, alive, saved.Alive)) && ok;
  ok = Check(posts, saved.Name + ": posts and stops within 0.3 m in building-local coordinates, same role") && ok;
  ok = Check(patrollers == saved.Patrollers, string.Format("%1: %2 patrollers (saved %3)", saved.Name, patrollers, saved.Patrollers)) && ok;
  if (awake) ok = Check(Agents(record) == saved.Alive, string.Format("%1: the squad holds exactly the survivors (%2)", saved.Name, Agents(record))) && ok;
  if (counted) PatrollersRestored += patrollers;
  return ok;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished) return;
  if (Watching) SampleHolds();
  if (Now() < Next) return;
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS) { Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase)); Finish("timeout"); return; }
  if (EXPG_GarrisonRecord.EXPG_TestReleases > 0 && Phase < 11) { Check(false, "no garrison was released: " + EXPG_GarrisonRecord.EXPG_TestReleaseReason); Finish("release"); return; }
  CDF_GMSaveConfig cfg = CDF_GMSaveConfig.GetInstance();
  if (Phase == 0)
  {
   Manager = EXPG_GarrisonManager.Get();
   if (!Check(Manager != null && cfg != null, "garrison manager and CDF configuration")) { Finish("setup"); return; }
   Check(Manager.PersistenceMode() == EXPG_GarrisonPersistence.MODE_CDF_BRIDGED, "CDF bridged mode with this pack's bridge");
   cfg.m_bClearBeforeLoad = true;
   cfg.m_bRepairDuplicatesOnLoad = true;
   cfg.m_bUsePersistenceBlob = false;
   cfg.m_bCaptureOnlyAuthored = true;
   cfg.m_bAutoSaveEnabled = false;
   GetGame().GetWorld().QueryEntitiesBySphere("4570 0 10700", 700, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("7270 0 4700", 500, AddMapHouse);
   if (!MapHouse) GetGame().GetWorld().QueryEntitiesBySphere("5040 0 3950", 500, AddMapHouse);
   ResourceName townHouse = "{38A5F3E4578087AB}Prefabs/Structures/Houses/Town/House_Town_E_2I01/House_Town_E_2I01.et";
   Resource townResource = Resource.Load(townHouse);
   if (MapHouse)
   {
    HouseA = MapHouse;
    Baked = true;
    PointA = MapHouse.GetOrigin();
   }
   else
   {
    PointA = PointB + "0 0 80";
    HouseA = GetGame().SpawnEntityPrefab(townResource, GetGame().GetWorld(), Params(PointA));
   }
   ResourceName village = "{EDBC0E94793BA9F1}Prefabs/Structures/Houses/Village/House_Village_E_1I01/House_Village_E_1I01.et";
   Resource villageResource = Resource.Load(village);
   HouseB = GetGame().SpawnEntityPrefab(villageResource, GetGame().GetWorld(), Params(PointB));
   if (!Check(SCR_DestructibleBuildingEntity.Cast(HouseA) != null && SCR_DestructibleBuildingEntity.Cast(HouseB) != null, "two enterable houses")) { Finish("buildings"); return; }
   PointB[1] = GetGame().GetWorld().GetSurfaceY(PointB[0], PointB[2]);
   PointD = PointB + "150 0 0";
   PointD[1] = GetGame().GetWorld().GetSurfaceY(PointD[0], PointD[2]);
   GroupD = SpawnTeam(PointD);
   if (!Check(GroupD != null, "ordinary fire team D spawned")) { Finish("ordinary"); return; }
   Author(GroupD);
   GroupD.EBG_Exclude = true;
   Manager.Prepare(HouseA);
   Manager.Prepare(HouseB);
   Advance(1);
   return;
  }
  if (Phase == 1)
  {
   EXPG_BuildingPlan planA = Manager.FindPlan(HouseA);
   EXPG_BuildingPlan planB = Manager.FindPlan(HouseB);
   if (!planA || !planA.Done || !planB || !planB.Done) { Waited(150, "both building analyses finished within 150 seconds"); return; }
   GroupA = AddSquad(HouseA, PointA + "12 0 0", 0, RecordA);
   if (!GroupA) { Finish("add A"); return; }
   GroupB = AddSquad(HouseB, PointB + "12 0 0", 1, RecordB);
   if (!GroupB) { Finish("add B"); return; }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (!Bound(RecordA) || !Bound(RecordB)) { Waited(60, "A and B placed and bound within 60 seconds"); return; }
   // Test seam: stand-ins hold the town house's free fixed posts while C is placed, so
   // C takes the reinforcement order's interior patrols.
   EXPG_BuildingPlan townPlan = Manager.FindPlan(HouseA);
   foreach (int order, int slot : townPlan.Slots)
   {
    if (!townPlan.FixedSlots[order]) continue;
    IEntity holder = GetGame().SpawnEntity(GenericEntity, GetGame().GetWorld(), Params(PointA + "0 60 0"));
    if (holder && townPlan.ReserveNode(holder, slot)) Holders.Insert(holder);
    else if (holder) SCR_EntityHelper.DeleteEntityAndChildren(holder);
   }
   GroupC = AddSquad(HouseA, PointA + "-12 0 0", 2, RecordC);
   if (!GroupC) { Finish("add C"); return; }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (!Bound(RecordC)) { Waited(60, "C placed and bound within 60 seconds"); return; }
   EXPG_BuildingPlan heldPlan = Manager.FindPlan(HouseA);
   foreach (IEntity held : Holders)
   {
    if (heldPlan) heldPlan.ReleaseReservation(held);
    SCR_EntityHelper.DeleteEntityAndChildren(held);
   }
   Holders.Clear();
   int patrolling = 0;
   foreach (EXPG_GarrisonMember placed : RecordC.Members) if (!placed.Fixed) patrolling++;
   if (!Check(patrolling > 0, string.Format("C patrols inside (%1 patrollers)", patrolling))) { Finish("no patrollers"); return; }
   Kill(RecordA);
   Kill(RecordC);
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   bool dead = RecordA.Members[SQUAD - 1].CacheMember.Dead && RecordC.Members[SQUAD - 1].CacheMember.Dead;
   if (!dead || !Bound(RecordA) || !SimulationAsleep(RecordB) || !FullAsleep(RecordC)) { Waited(150, string.Format("casualties registered, A awake, B Simulation, C Full within 150 seconds: A='%1' B='%2' C='%3'", RecordA.Status, RecordB.Status, RecordC.Status)); return; }
   Advance(5);
   return;
  }
  if (Phase == 5)
  {
   // 1. Capture.
   Remember("A", RecordA);
   Remember("B", RecordB);
   Remember("C", RecordC);
   CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("expg-cdf-roundtrip", "fixture");
   if (!Check(document != null, "CDF captured a document with three active garrisons (Full included)")) { Finish("capture"); return; }
   Check(EXPG_CDFGarrison.Declared(document.m_sWorldState), "the document carries the garrison ledger envelope");
   OwnedRecords = 0;
   bool ordinary = false;
   foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
   {
    if (!record.m_Entity) continue;
    IEntity entity = record.m_Entity.GetOwner();
    if (EXPG_GarrisonPersistence.OwnsForSave(entity) || entity == GroupA || entity == GroupB) OwnedRecords++;
    if (entity == GroupD) ordinary = true;
   }
   Check(OwnedRecords == 0, string.Format("no document record belongs to a garrison (%1)", OwnedRecords));
   Check(ordinary, "the ordinary authored squad D is in the document");
   OrdinaryLiving = LivingNear(PointD);
   Check(OrdinaryLiving == SQUAD, string.Format("D's %1 soldiers stand at its spot before the save", OrdinaryLiving));
   if (Check(document.SaveToFile(FILE), "document written to " + FILE)) DocumentWritten = 1;
   Loaded = new CDF_GMSaveDocument();
   Corrupt = new CDF_GMSaveDocument();
   if (!Check(Loaded.LoadFromFile(FILE) && Corrupt.LoadFromFile(FILE) && Loaded.m_aEntities.Count() == document.m_aEntities.Count(), "document read back from the file")) { Finish("file"); return; }
   Corrupt.m_sWorldState.Replace("expgGarrisonVersion", "expgGarrisonVersionX");
   // 2. Append load.
   cfg.m_bClearBeforeLoad = false;
   bool appended = CDF_GMSaveRestore.Restore(Loaded);
   cfg.m_bClearBeforeLoad = true;
   if (Check(!appended && Manager.EXPG_TestActive() == 3 && GroupA && GroupB && GroupD, "an append load is refused and the scene is untouched")) AppendRefused = 1;
   // 3. Load.
   bool restored = CDF_GMSaveRestore.Restore(Loaded);
   if (!Check(restored, "CDF restore with clearBeforeLoad")) { Finish("restore"); return; }
   Watching = true;
   Advance(6);
   return;
  }
  if (Phase == 6)
  {
   if (Manager.EXPG_TestActive() < 3 || EXPG_CDFGarrison.Pending) { Waited(20, string.Format("CDF finalized and the ledger imported 3 garrisons within 20 seconds (%1)", Manager.EXPG_TestActive())); return; }
   EXPG_GarrisonRecord a = Manager.EXPG_TestByToken(Expect[0].Token);
   EXPG_GarrisonRecord b = Manager.EXPG_TestByToken(Expect[1].Token);
   EXPG_GarrisonRecord c = Manager.EXPG_TestByToken(Expect[2].Token);
   if (!a || !b || !c) { Check(false, "every saved token is loaded"); Finish("tokens"); return; }
   if (!Bound(a) || !SimulationAsleep(b) || !FullAsleep(c)) { Waited(240, string.Format("A awake and bound, B Simulation again, C Full within 240 seconds: A='%1' B='%2' C='%3'", a.Status, b.Status, c.Status)); return; }
   Check(!GroupA && !GroupB && !GroupD, "CDF cleared the old garrison squads and D");
   foreach (EXPG_CdfExpect saved : Expect) if (Manager.EXPG_TestTokenCount(saved.Token) != 1) Duplicates++;
   Garrisons = Manager.EXPG_TestActive();
   bool okA = VerifyRestored(Expect[0], a, true);
   bool okB = VerifyRestored(Expect[1], b, true);
   bool okC = VerifyRestored(Expect[2], c, false);
   if (okA && okB && okC) PostsOk = 1;
   Check(Agents(c) == 0 && FullAsleep(c), "C is Full cached with nobody in the world");
   int nearB = LivingNear(PointB);
   if (nearB != Expect[1].Alive) Duplicates++;
   Check(nearB == Expect[1].Alive, string.Format("exactly B's survivors stand at the village house (%1)", nearB));
   int livingD = LivingNear(PointD);
   if (Check(livingD == OrdinaryLiving, string.Format("CDF restored the ordinary squad D once: %1 soldiers (saved %2)", livingD, OrdinaryLiving))) Ordinary = 1;
   Check(Garrisons == 3 && Duplicates == 0 && RespawnedDead == 0, string.Format("three garrisons, no duplicate, no casualty respawned (%1, %2, %3)", Garrisons, Duplicates, RespawnedDead));
   EXPG_GarrisonPersistence.SyncSaveExclusion();
   Check(a.Group && EXPG_SaveExclusion.IsFlagged(a.Group) && b.Group && EXPG_SaveExclusion.IsFlagged(b.Group), "restored garrison squads are NON_SERIALIZABLE for CDF again");
   c.CacheMode = 0;
   RecordC = c;
   Advance(7);
   return;
  }
  if (Phase == 7)
  {
   if (!Bound(RecordC)) { Waited(60, "C woke onto its posts within 60 seconds: " + RecordC.Status); return; }
   if (Check(VerifyRestored(Expect[2], RecordC, true, false) && RespawnedDead == 0, "C woke with its survivors on their posts; its casualty stays dead")) FullWoke = 1;
   Check(Violations.IsEmpty(), string.Format("no restored soldier ran AI before he was bound (%1)", Violations.Count()));
   Watching = false;
   // 5. Unreadable ledger.
   bool first = CDF_GMSaveRestore.Restore(Corrupt);
   if (Check(!first && Manager.EXPG_TestActive() == 3, "a save with an unreadable ledger is refused and the scene is untouched")) CorruptRefused = 1;
   bool second = CDF_GMSaveRestore.Restore(Corrupt);
   Check(second, "loading it again confirms: it loads without its garrisons");
   Advance(11);
   return;
  }
  if (Phase == 11)
  {
   if (EXPG_CDFGarrison.Pending || Manager.EXPG_TestActive() > 0) { Waited(20, string.Format("the confirmed load finished without garrisons within 20 seconds (%1)", Manager.EXPG_TestActive())); return; }
   int livingAfter = LivingNear(PointD);
   if (Check(livingAfter == OrdinaryLiving && LivingNear(PointB) == 0, string.Format("old garrisons cleared, none restored, D restored once (%1 soldiers)", livingAfter))) CorruptSkipped = 1;
   Finish("completed");
  }
 }
}
