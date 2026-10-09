// TEST ONLY. CDF Game Master Save round trip of AI vehicle crews through the EXPBG CDF
// Compat vehicle-crew bridge (EVC_CDFCrew.c), in one diagnostic server.
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EVC_CDFCrewRoundTrip.c -ExpectResult '\[EXPG CREW CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 seats=[4-9] seated=[4-9] adopted=[2-9] linked=[4-9] chair=(1|skipped) items=[4-9] fallback=1 noDuplicates=1 legacy=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file into its fixture addon; the driver class name
// EXPG_CdfRoundTrip is fixed by the runner's layer.
// Real CDF 1.4.1 code throughout: CDF_GMSaveCapture.Capture, SaveToFile/LoadFromFile,
// CDF_GMSaveRestore.Restore (Clear, spawning, deferred state pass) with character
// inventories on. Scene, all authored like a Game Master's:
//  - a vanilla M1025 (M2HB) whose driver and gunner are spawned by the vehicle's own
//    default-occupant path (SCR_EditableVehicleComponent.OccupyVehicleWithDefaultCharacters,
//    the GM "crewed" placement): their group has no author, CDF alone skips it;
//  - a vanilla M2 tripod (static weapon) and an authored US fire team: one member in the
//    tripod, one in an M1025 cargo seat, one in a Heine office chair seat when that mod is
//    loaded (self-skipping otherwise), one on foot.
// Cases, in order:
//  1. Capture: the default crew is added (group with the vehicle's author and its members),
//     every seated soldier carries the seat key, vehicle seats also CDF's crew link; the
//     key leaves CDF's own state readable; the file is written and read.
//  2. Load (clearBeforeLoad on): every crew member is back alive in the same vehicle, seat
//     type and index, in an AI group (the default crew together, with its saved faction),
//     with the items he had at the save; his group knows the vehicle; nobody is
//     duplicated; the bridge's summary counts every seat seated.
//  3. Warm reload of a document whose cargo seat index no longer exists: that soldier stands
//     next to the M1025, the others are seated, still nobody duplicated.
//  4. Legacy: the same document without any seat key or crew link (an existing save) loads
//     through CDF alone: the bridge starts no pass and seats nobody, nobody duplicated.
// Not covered: GM UI and dialogs, a cold server restart, multiplayer and JIP, players.
class EVC_CdfExpect
{
 string m_sHolderPrefab;
 vector m_vHolderPos;
 int m_iIndex;
 int m_iType;
 string m_sPrefab;
 string m_sItems;
 bool m_bDefaultCrew;
 bool m_bChair;
}

class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 360;
 // Seconds after the bridge's pass before soldiers and inventories are compared.
 static const float SETTLE = 4;
 static const float RADIUS = 40;
 static const string FILE = "$profile:evc-cdf-roundtrip.json";
 static const ResourceName VEHICLE = "{3EA6F47D95867114}Prefabs/Vehicles/Wheeled/M998/M1025_armed_M2HB.et";
 static const ResourceName TRIPOD = "{73530808E4B455BA}Prefabs/Weapons/Tripods/Tripod_M3_M2HB.et";
 static const ResourceName TEAM = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
 // Heine "Placeable Structures For GM" office chair (seat child Sentar.et); optional.
 static const ResourceName CHAIR = "{4AC034517E87747E}Prefabs/GM_Props/Office_chair_GM.et";
 vector m_vPoint = "4773.46 0 7094.57";
 IEntity m_Vehicle;
 IEntity m_Tripod;
 IEntity m_Chair;
 SCR_AIGroup m_Team;
 ref array<ref EVC_CdfExpect> m_aExpect = {};
 ref CDF_GMSaveDocument m_Loaded;
 ref CDF_GMSaveDocument m_Moved;
 ref CDF_GMSaveDocument m_Legacy;
 string m_sDefaultFaction;
 string m_sChair = "skipped";
 int m_iPhase;
 int m_iChecks;
 int m_iFailures;
 int m_iSeats;
 int m_iSeated;
 int m_iAdopted;
 int m_iLinked;
 int m_iItems;
 int m_iFallback;
 int m_iNoDuplicates = 1;
 int m_iLegacy;
 int m_iSoldiers;
 int m_iPasses;
 float m_fStarted;
 float m_fNext;
 float m_fPhaseStarted;
 float m_fPassSeen;
 bool m_bFinished;
 protected array<IEntity> m_aNear;

 void EXPG_CdfRoundTrip(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now()
 {
  return GetGame().GetWorld().GetWorldTime() * 0.001;
 }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  m_fStarted = Now();
  m_fNext = m_fStarted + 15;
  PrintFormat("[EXPG CREW CDF ROUNDTRIP BEGIN] cdf=%1 deadline=%2", CDF_GMSave.VERSION, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  m_iChecks++;
  if (!value) m_iFailures++;
  PrintFormat("[EXPG CREW CDF ROUNDTRIP CHECK] pass=%1 %2", value, label);
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
  string first = string.Format("checks=%1 failures=%2 seats=%3 seated=%4 adopted=%5 linked=%6", m_iChecks, m_iFailures, m_iSeats, m_iSeated, m_iAdopted, m_iLinked);
  string second = string.Format("chair=%1 items=%2 fallback=%3 noDuplicates=%4 legacy=%5 reason=%6", m_sChair, m_iItems, m_iFallback, m_iNoDuplicates, m_iLegacy, reason);
  PrintFormat("[EXPG CREW CDF ROUNDTRIP RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  m_iPhase = phase;
  m_fPhaseStarted = Now();
  PrintFormat("[EXPG CREW CDF ROUNDTRIP PHASE] phase=%1 elapsed=%2", m_iPhase, Now() - m_fStarted);
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
  author.Initialize("evc-cdf-roundtrip", "", 0, -1);
  editable.SetAuthor(author);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 IEntity Spawn(ResourceName name, vector offset)
 {
  Resource resource = Resource.Load(name);
  if (!resource || !resource.IsValid())
  {
   return null;
  }
  IEntity entity = GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), Params(m_vPoint + offset));
  if (entity) Author(entity);
  return entity;
 }
 bool Collect(IEntity entity)
 {
  if (m_aNear) m_aNear.Insert(entity);
  return true;
 }
 // Living AI soldiers around the scene.
 int Soldiers(notnull array<SCR_ChimeraCharacter> found)
 {
  found.Clear();
  array<IEntity> near = {};
  m_aNear = near;
  GetGame().GetWorld().QueryEntitiesBySphere(m_vPoint, RADIUS, Collect);
  m_aNear = null;
  foreach (IEntity entity : near)
  {
   SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
   if (soldier && soldier.GetCharacterController() && !soldier.GetCharacterController().IsDead()) found.Insert(soldier);
  }
  return found.Count();
 }
 // The entity of a prefab nearest a spot (within 3 m), or its direct child with compartments.
 IEntity Holder(string prefab, vector spot)
 {
  array<IEntity> near = {};
  m_aNear = near;
  GetGame().GetWorld().QueryEntitiesBySphere(spot, 6, Collect);
  m_aNear = null;
  IEntity best;
  float bestDistance = 3;
  foreach (IEntity entity : near)
  {
   array<IEntity> candidates = {entity};
   IEntity child = entity.GetChildren();
   while (child)
   {
    candidates.Insert(child);
    child = child.GetSibling();
   }
   foreach (IEntity candidate : candidates)
   {
    if (!candidate.FindComponent(BaseCompartmentManagerComponent) || CDF_GMSaveState.GetPrefabName(candidate) != prefab) continue;
    float distance = vector.Distance(candidate.GetOrigin(), spot);
    if (distance >= bestDistance) continue;
    best = candidate;
    bestDistance = distance;
   }
  }
  return best;
 }
 static array<BaseCompartmentSlot> Slots(IEntity holder)
 {
  array<BaseCompartmentSlot> slots = {};
  if (!holder)
  {
   return slots;
  }
  BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(holder.FindComponent(BaseCompartmentManagerComponent));
  if (manager) manager.GetCompartments(slots);
  return slots;
 }
 static BaseCompartmentSlot FreeSlot(IEntity holder, ECompartmentType type)
 {
  array<BaseCompartmentSlot> slots = Slots(holder);
  foreach (BaseCompartmentSlot slot : slots)
  {
   if (slot && slot.GetType() == type && !slot.GetOccupant() && slot.IsCompartmentAccessible())
   {
    return slot;
   }
  }
  return null;
 }
 static IEntity SeatChild(IEntity entity)
 {
  if (!entity)
  {
   return null;
  }
  IEntity child = entity.GetChildren();
  while (child)
  {
   if (child.FindComponent(BaseCompartmentManagerComponent))
   {
    return child;
   }
   child = child.GetSibling();
  }
  return null;
 }
 bool Board(SCR_ChimeraCharacter soldier, BaseCompartmentSlot slot, bool vehicle)
 {
  if (!soldier || !slot)
  {
   return false;
  }
  CompartmentAccessComponent access = soldier.GetCompartmentAccessComponent();
  if (!access || !access.GetInVehicle(slot.GetOwner(), slot, true, -1, ECloseDoorAfterActions.INVALID, true))
  {
   return false;
  }
  if (vehicle) Usable(m_Team, slot.GetVehicle());
  return true;
 }
 // As the GM move-in does: the group may use the vehicle.
 static void Usable(SCR_AIGroup group, IEntity vehicle)
 {
  if (!group || !vehicle || !group.GetGroupUtilityComponent())
  {
   return;
  }
  IEntity usageOwner;
  SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.FindOnNearestParent(vehicle, usageOwner);
  if (usage) group.GetGroupUtilityComponent().AddUsableVehicle(usage);
 }
 static bool KnowsVehicle(SCR_AIGroup group, IEntity vehicle)
 {
  if (!group || !vehicle || !group.GetGroupUtilityComponent())
  {
   return false;
  }
  IEntity usageOwner;
  SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.FindOnNearestParent(vehicle, usageOwner);
  // A holder without AI vehicle usage (static weapon) has nothing to register.
  if (!usage)
  {
   return true;
  }
  return group.GetGroupUtilityComponent().IsUsableVehicle(usage);
 }
 // Sorted prefab names of everything the soldier carries and wears.
 static string Items(SCR_ChimeraCharacter soldier)
 {
  InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(soldier.FindComponent(InventoryStorageManagerComponent));
  if (!manager)
  {
   return "none";
  }
  array<IEntity> items = {};
  manager.GetItems(items);
  array<string> names = {};
  foreach (IEntity item : items)
  {
   if (item && item.GetPrefabData()) names.Insert(item.GetPrefabData().GetPrefabName());
  }
  names.Sort();
  string joined;
  foreach (string name : names) joined += name + ";";
  return joined;
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
   BoardTeam();
   return;
  }
  if (m_iPhase == 2)
  {
   CaptureAndLoad();
   return;
  }
  if (m_iPhase == 3)
  {
   CheckLoaded();
   return;
  }
  if (m_iPhase == 4)
  {
   CheckMoved();
   return;
  }
  if (m_iPhase == 5)
  {
   CheckLegacy();
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
  m_Vehicle = Spawn(VEHICLE, vector.Zero);
  m_Tripod = Spawn(TRIPOD, Vector(9, 0, 0));
  m_Team = SCR_AIGroup.Cast(Spawn(TEAM, Vector(6, 0, 9)));
  Resource chairResource = Resource.Load(CHAIR);
  if (chairResource && chairResource.IsValid()) m_Chair = Spawn(CHAIR, Vector(-6, 0, 9));
  if (!Check(m_Vehicle && m_Tripod && m_Team, "authored M1025, M2 tripod and US fire team spawned"))
  {
   Finish("setup");
   return;
  }
  m_Team.EBG_Exclude = true;
  // The GM "crewed" placement: driver and gunner from the vehicle's default occupants.
  SCR_EditableVehicleComponent editable = SCR_EditableVehicleComponent.Cast(m_Vehicle.FindComponent(SCR_EditableVehicleComponent));
  if (!Check(editable != null, "the M1025 is an editable vehicle"))
  {
   Finish("setup");
   return;
  }
  array<ECompartmentType> crew = {ECompartmentType.PILOT, ECompartmentType.TURRET};
  editable.OccupyVehicleWithDefaultCharacters(crew);
  Advance(1);
 }
 void BoardTeam()
 {
  SCR_BaseCompartmentManagerComponent manager = SCR_BaseCompartmentManagerComponent.Cast(m_Vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
  if (m_Team.GetAgentsCount() < 4 || !manager || manager.IsSpawningDefaultOccupants() || Now() - m_fPhaseStarted < 6)
  {
   Waited(40, "the fire team has four AI members and the M1025 crew is spawned within 40 seconds");
   return;
  }
  array<AIAgent> agents = {};
  m_Team.GetAgents(agents);
  array<SCR_ChimeraCharacter> members = {};
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member) members.Insert(member);
  }
  if (!Check(members.Count() >= 4, "four fire team members"))
  {
   Finish("board");
   return;
  }
  Check(Board(members[0], FreeSlot(m_Tripod, ECompartmentType.TURRET), true), "a fire team member mans the M2 tripod");
  Check(Board(members[1], FreeSlot(m_Vehicle, ECompartmentType.CARGO), true), "a fire team member sits in an M1025 cargo seat");
  if (m_Chair)
  {
   IEntity seat = SeatChild(m_Chair);
   BaseCompartmentSlot chairSlot;
   array<BaseCompartmentSlot> chairSlots = Slots(seat);
   foreach (BaseCompartmentSlot slot : chairSlots)
   {
    if (slot && !slot.GetOccupant()) chairSlot = slot;
   }
   if (Check(Board(members[2], chairSlot, false), "a fire team member sits in the Heine office chair")) m_sChair = "0";
  }
  Advance(2);
 }
 // Every seated AI soldier of a holder, as expected after a load.
 void Expect(IEntity holder, bool chair)
 {
  if (!holder)
  {
   return;
  }
  array<BaseCompartmentSlot> slots = Slots(holder);
  foreach (int index, BaseCompartmentSlot slot : slots)
  {
   if (!slot) continue;
   SCR_ChimeraCharacter occupant = SCR_ChimeraCharacter.Cast(slot.GetOccupant());
   if (!occupant) continue;
   EVC_CdfExpect expect = new EVC_CdfExpect();
   expect.m_sHolderPrefab = CDF_GMSaveState.GetPrefabName(holder);
   expect.m_vHolderPos = holder.GetOrigin();
   expect.m_iIndex = index;
   expect.m_iType = slot.GetType();
   expect.m_sPrefab = CDF_GMSaveState.GetPrefabName(occupant);
   expect.m_sItems = Items(occupant);
   SCR_AIGroup group = EVC_CDF.GroupOf(occupant);
   expect.m_bDefaultCrew = group && group.m_bEVC_DefaultCrew;
   if (expect.m_bDefaultCrew) m_sDefaultFaction = group.GetFactionName();
   expect.m_bChair = chair;
   m_aExpect.Insert(expect);
  }
 }
 void CaptureAndLoad()
 {
  if (Now() - m_fPhaseStarted < 4)
  {
   return;
  }
  // 1. Capture: every seat and the soldiers' items in the same frame as the save.
  Expect(m_Vehicle, false);
  Expect(m_Tripod, false);
  if (m_Chair) Expect(SeatChild(m_Chair), true);
  m_iSoldiers = Characters();
  int crew;
  int chairs;
  foreach (EVC_CdfExpect expect : m_aExpect)
  {
   if (expect.m_bDefaultCrew) crew++;
   if (expect.m_bChair) chairs++;
  }
  m_iSeats = m_aExpect.Count();
  Check(crew >= 2 && !m_sDefaultFaction.IsEmpty(), string.Format("the M1025 default crew sits in the vehicle (%1, faction %2)", crew, m_sDefaultFaction));
  Check(m_iSeats >= crew + 2, string.Format("the fire team members sit in the tripod and the cargo seat (%1 seats)", m_iSeats));
  CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("evc-cdf-roundtrip", "fixture");
  if (!Check(document != null, "CDF captured a document"))
  {
   Finish("capture");
   return;
  }
  m_iAdopted = EVC_CDF.s_iSavedMembers;
  m_iLinked = EVC_CDF.s_iSavedLinked;
  Check(EVC_CDF.s_iSavedGroups == 1 && m_iAdopted == crew, string.Format("the default crew CDF skipped is added: one group, %1 members (%2)", crew, m_iAdopted));
  Check(EVC_CDF.s_iSavedSeats == m_iSeats && EVC_CDF.s_iSavedProps == chairs && m_iLinked == m_iSeats - chairs, string.Format("every seated soldier carries his seat (%1 seats, %2 linked, %3 props)", EVC_CDF.s_iSavedSeats, m_iLinked, EVC_CDF.s_iSavedProps));
  int tagged;
  int readable;
  int authored;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (record.m_iEntityType == EEditableEntityType.GROUP && record.m_sAuthorUID == "evc-cdf-roundtrip" && record.m_Entity && SCR_AIGroup.Cast(record.m_Entity.GetOwner()) && SCR_AIGroup.Cast(record.m_Entity.GetOwner()).m_bEVC_DefaultCrew) authored++;
   if (!record.m_sState.Contains("\"evcSeat\"")) continue;
   tagged++;
   // An older reader sees CDF's own state under the extra key, unchanged.
   JsonLoadContext context = new JsonLoadContext();
   string payload;
   if (!context.LoadFromString(record.m_sState) || !context.ReadValue(EVC_CDF.KEY, payload)) continue;
   string original;
   if (!EVC_CDF.Untag(record.m_sState, payload, original)) continue;
   array<string> withKey = {};
   array<string> without = {};
   context.ReadValue("ivp", withKey);
   JsonLoadContext plain = new JsonLoadContext();
   if (!original.IsEmpty() && plain.LoadFromString(original)) plain.ReadValue("ivp", without);
   if (withKey.Count() == without.Count()) readable++;
  }
  Check(tagged == m_iSeats && readable == tagged, string.Format("the seat key leaves CDF's own state readable (%1 of %2)", readable, tagged));
  Check(authored == 1, "the added crew group carries the M1025's author");
  if (!Check(document.SaveToFile(FILE), "document written to " + FILE))
  {
   Finish("file");
   return;
  }
  m_Loaded = new CDF_GMSaveDocument();
  m_Moved = new CDF_GMSaveDocument();
  m_Legacy = new CDF_GMSaveDocument();
  if (!Check(m_Loaded.LoadFromFile(FILE) && m_Moved.LoadFromFile(FILE) && m_Legacy.LoadFromFile(FILE), "document read back from the file"))
  {
   Finish("file");
   return;
  }
  // 2. Load.
  m_iPasses = EVC_CDF.s_iPasses;
  m_fPassSeen = 0;
  if (!Check(CDF_GMSaveRestore.Restore(m_Loaded), "CDF restore with clearBeforeLoad"))
  {
   Finish("restore");
   return;
  }
  Advance(3);
 }
 // The bridge's pass of the current load has ended and settled.
 bool PassSettled(string label)
 {
  if (EVC_CDF.s_iPasses <= m_iPasses || EVC_CDF.IsActive() || !EINV_CDF.IsIdle())
  {
   Waited(60, label);
   return false;
  }
  if (m_fPassSeen <= 0) m_fPassSeen = Now();
  return Now() - m_fPassSeen >= SETTLE;
 }
 // Seated soldiers matching the expectations; standing collects the expected not seated.
 int Seated(bool items, notnull array<ref EVC_CdfExpect> standing)
 {
  int seated;
  standing.Clear();
  SCR_AIGroup crewGroup;
  bool together = true;
  foreach (EVC_CdfExpect expect : m_aExpect)
  {
   IEntity holder = Holder(expect.m_sHolderPrefab, expect.m_vHolderPos);
   array<BaseCompartmentSlot> slots = Slots(holder);
   SCR_ChimeraCharacter occupant;
   if (expect.m_iIndex < slots.Count()) occupant = SCR_ChimeraCharacter.Cast(slots[expect.m_iIndex].GetOccupant());
   bool alive = occupant && occupant.GetCharacterController() && !occupant.GetCharacterController().IsDead();
   SCR_AIGroup group;
   if (alive) group = EVC_CDF.GroupOf(occupant);
   if (!alive || CDF_GMSaveState.GetPrefabName(occupant) != expect.m_sPrefab || slots[expect.m_iIndex].GetType() != expect.m_iType || !group)
   {
    standing.Insert(expect);
    continue;
   }
   seated++;
   if (expect.m_bDefaultCrew)
   {
    if (!crewGroup) crewGroup = group;
    if (group != crewGroup || group.GetFactionName() != m_sDefaultFaction) together = false;
   }
   if (!expect.m_bChair) Check(KnowsVehicle(group, holder), "the crew's group knows its vehicle: " + expect.m_sHolderPrefab);
   if (!items) continue;
   string now = Items(occupant);
   if (now == expect.m_sItems)
   {
    m_iItems++;
    continue;
   }
   PrintFormat("[EXPG CREW CDF ROUNDTRIP ITEMS] seat %1/%2 differs: saved=%3 loaded=%4", expect.m_sHolderPrefab, expect.m_iIndex, expect.m_sItems, now);
  }
  Check(together, "the default crew is back in one group with its saved faction");
  return seated;
 }
 // Every character around the scene, living or not: CDF alone may lose a soldier it
 // restores standing inside a vehicle, never add one.
 int Characters()
 {
  array<IEntity> near = {};
  m_aNear = near;
  GetGame().GetWorld().QueryEntitiesBySphere(m_vPoint, RADIUS, Collect);
  m_aNear = null;
  int count;
  foreach (IEntity entity : near)
  {
   if (SCR_ChimeraCharacter.Cast(entity) && !entity.IsDeleted()) count++;
  }
  return count;
 }
 bool NoDuplicates(string label)
 {
  int count = Characters();
  bool same = count == m_iSoldiers;
  if (!same) m_iNoDuplicates = 0;
  return Check(same, string.Format("%1: %2 soldiers as at the save (%3)", label, m_iSoldiers, count));
 }
 void CheckLoaded()
 {
  if (!PassSettled("the vehicle crew pass of the load ends within 60 seconds"))
  {
   return;
  }
  array<ref EVC_CdfExpect> standing = {};
  m_iSeated = Seated(true, standing);
  Check(m_iSeated == m_iSeats, string.Format("every crew member is back alive in his vehicle, seat type and index, in an AI group (%1 of %2)", m_iSeated, m_iSeats));
  Check(m_iItems == m_iSeats, string.Format("every crew member carries the items he had at the save (%1 of %2)", m_iItems, m_iSeats));
  Check(EVC_CDF.s_iLastRestored == m_iSeats && EVC_CDF.s_iLastSeated == m_iSeats && EVC_CDF.s_iLastStanding == 0 && EVC_CDF.s_iLastFailed == 0, string.Format("bridge summary restored=%1 seated=%2 fallbackStanding=%3 failed=%4", EVC_CDF.s_iLastRestored, EVC_CDF.s_iLastSeated, EVC_CDF.s_iLastStanding, EVC_CDF.s_iLastFailed));
  if (m_Chair && m_iSeated == m_iSeats) m_sChair = "1";
  NoDuplicates("nobody duplicated by the load");
  // 3. A cargo seat index that no longer exists: that soldier stands next to the M1025.
  int moved;
  foreach (CDF_GMSaveEntityRecord record : m_Moved.m_aEntities)
  {
   if (moved > 0 || !record.m_sState.Contains("\"evcSeat\"")) continue;
   JsonLoadContext context = new JsonLoadContext();
   string payload;
   string reason;
   string original;
   if (!context.LoadFromString(record.m_sState) || !context.ReadValue(EVC_CDF.KEY, payload) || !EVC_CDF.Untag(record.m_sState, payload, original)) continue;
   EVC_Seat seat = EVC_CDF.Decode(payload, reason);
   if (!seat || seat.m_iType != ECompartmentType.CARGO || !seat.m_bLinked) continue;
   string changed = EVC_CDF.Encode(seat.m_iType, 99, true, seat.m_sHolderPrefab, seat.m_vHolderPos, seat.m_sFaction);
   string retagged = EVC_CDF.Tag(original, changed);
   if (retagged.IsEmpty()) continue;
   record.m_sState = retagged;
   record.m_iTargetValue = 99;
   moved++;
  }
  if (!Check(moved == 1, "the reload document lost one cargo seat"))
  {
   Finish("moved");
   return;
  }
  m_iPasses = EVC_CDF.s_iPasses;
  m_fPassSeen = 0;
  if (!Check(CDF_GMSaveRestore.Restore(m_Moved), "warm reload with clearBeforeLoad"))
  {
   Finish("moved");
   return;
  }
  Advance(4);
 }
 void CheckMoved()
 {
  if (!PassSettled("the vehicle crew pass of the reload ends within 60 seconds"))
  {
   return;
  }
  array<ref EVC_CdfExpect> standing = {};
  int seated = Seated(false, standing);
  bool beside = false;
  if (standing.Count() == 1)
  {
   IEntity vehicle = Holder(VEHICLE, standing[0].m_vHolderPos);
   array<SCR_ChimeraCharacter> soldiers = {};
   Soldiers(soldiers);
   foreach (SCR_ChimeraCharacter soldier : soldiers)
   {
    CompartmentAccessComponent access = soldier.GetCompartmentAccessComponent();
    if (!vehicle || (access && access.IsInCompartment()) || CDF_GMSaveState.GetPrefabName(soldier) != standing[0].m_sPrefab) continue;
    if (vector.DistanceXZ(soldier.GetOrigin(), vehicle.GetOrigin()) <= 8) beside = true;
   }
  }
  bool summary = EVC_CDF.s_iLastSeated == m_iSeats - 1 && EVC_CDF.s_iLastStanding == 1 && EVC_CDF.s_iLastFailed == 0;
  if (Check(seated == m_iSeats - 1 && beside && summary, string.Format("a gone seat leaves its soldier standing next to the M1025, the others seated (%1 seated, standing %2, summary %3)", seated, standing.Count(), summary))) m_iFallback = 1;
  NoDuplicates("nobody duplicated by the warm reload");
  // 4. Legacy: an existing save without seats loads through CDF alone.
  int stripped;
  foreach (CDF_GMSaveEntityRecord record : m_Legacy.m_aEntities)
  {
   if (!record.m_sState.Contains("\"evcSeat\"")) continue;
   JsonLoadContext context = new JsonLoadContext();
   string payload;
   string original;
   if (!context.LoadFromString(record.m_sState) || !context.ReadValue(EVC_CDF.KEY, payload) || !EVC_CDF.Untag(record.m_sState, payload, original)) continue;
   record.m_sState = original;
   record.m_iTarget = CDF_GMSaveEntityRecord.TARGET_NONE;
   record.m_iTargetValue = -1;
   stripped++;
  }
  Check(stripped == m_iSeats, string.Format("the legacy document has no seat left (%1 stripped)", stripped));
  m_iPasses = EVC_CDF.s_iPasses;
  if (!Check(CDF_GMSaveRestore.Restore(m_Legacy), "the legacy document loads"))
  {
   Finish("legacy");
   return;
  }
  Advance(5);
 }
 void CheckLegacy()
 {
  if (Now() - m_fPhaseStarted < 10 || !EINV_CDF.IsIdle())
  {
   Waited(60, "the legacy load settles within 60 seconds");
   return;
  }
  array<ref EVC_CdfExpect> standing = {};
  int seated = Seated(false, standing);
  if (Check(EVC_CDF.s_iPasses == m_iPasses && !EVC_CDF.IsActive() && seated == 0, string.Format("an existing save without seats: no bridge pass, nobody seated by the bridge (%1 seated)", seated))) m_iLegacy = 1;
  NoDuplicates("nobody duplicated by the legacy load");
  Finish("completed");
 }
}
