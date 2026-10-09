// EXPBG CDF Compat - Vehicle crew. Copyright 2026 ExpBG Tech.
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 support for AI characters seated in vehicles, static weapons
// and sittable props (compartments of an entity that is not a vehicle, e.g. chairs).
//
// What CDF 1.4.1 does alone: CDF_GMSaveCapture asks each editable for its crew link
// (SCR_EditableEntityComponent.Serialize: target and seat index) and CDF_GMSaveRestore
// replays it through Deserialize. The vanilla character overrides that reported the
// vehicle and seat are commented out (SCR_EditableCharacterComponent), so a captured crew
// member has no target and comes back standing at his saved spot inside the vehicle; his
// group forgets the vehicle. A crew placed by the Game Master with a vehicle ("crewed" /
// "with passengers" placement, Spawn Occupants) lives in a group the game spawned
// without author (BaseCompartmentSlot.SpawnCharacterInCompartment): CDF skips that group
// ("sans-auteur:GROUP" in "Racines parcourues ... ignores") and the crew is lost.
//
// Save (CDF_GMSaveCapture.Capture, after CDF and the inner adapters):
//  - default crews of saved vehicles (groups the vehicle's compartment manager spawned,
//    marked below) that CDF skipped are added as ordinary CDF records: the group (author
//    of the vehicle) and its living AI members, each with CDF_GMSaveState.Capture, so the
//    whole adapter chain (inventory, Unit Dialog, Unit Scripts, Intel) carries them;
//  - every saved AI character seated in an indexed compartment gets the seat in his
//    stored state, as one extra top-level key: "evcSeat": {"v":1,"t":type,"i":index,
//    "veh":linked,"p":holder prefab,"x","y","z":holder position,"f":group faction}.
//    CDF and every adapter read states by key, so the key is ignored by readers that do
//    not know it. A seat in a saved vehicle or static weapon also fills CDF's own crew
//    link (m_iTarget = vehicle record, m_iTargetValue = seat index): CDF's repair and the
//    garrison filter renumber it with the records. Prisoners and ACE captives are never
//    crew (EPR_CDF.IsCaptiveSeat): the prisoner bridge saves them on foot.
// Old readers (CDF alone, this pack before vehicle-crew): CDF replays the crew link through
// the Game Master's own move-in (first free driver, turret, then cargo seat of that
// vehicle), the extra key is ignored, added crews load as authored squads. Nothing refused.
//
// Load (CDF_GMSaveRestore.Restore): seats are read before CDF clears the scene; a state that
// carries the key but is not readable JSON (CDF would lose that soldier's inventory and
// damage) refuses the load; an unknown seat version only loses that seat. The crew link is
// hidden from CDF while it restores (the stored states stay untouched), then each
// restored crew member is moved straight into his saved seat (instant teleport, no walk):
// the same frame for the first IMMEDIATE_SEATS, then SEATS_PER_PUMP per pump. A vehicle is
// matched by its record, a prop CDF does not save by prefab and position (PROP_RADIUS);
// nothing is ever spawned. Once CDF's deferred finalization is done (group repair), a
// seated crew's group gets the vehicle as usable and its saved faction when it has none.
// A seat that is gone, occupied or of another type, or a holder that cannot be found,
// leaves the soldier standing next to it. CDF restores each crew member once from his
// record, so nobody is duplicated; a load clears the default crews of saved vehicles with
// them (IsManaged). Players, prisoners, garrison guards, groups Unit Caching or ambient
// traffic reserve and ACE animation helpers (captives, carrying) are never touched.
// One summary per load:
// [EXPBG CDF CREW] restored=N seated=M fallbackStanding=K failed=F ...
class EVC_Seat
{
 CDF_GMSaveEntityRecord m_Record;
 CDF_GMSaveEntityRecord m_Holder;
 int m_iTarget;
 int m_iType;
 int m_iIndex;
 bool m_bLinked;
 ResourceName m_sHolderPrefab;
 vector m_vHolderPos;
 string m_sFaction;
 IEntity m_Character;
 IEntity m_HolderEntity;
 int m_iAttempts;
 int m_iResult;
 int m_iDue;
 int m_iOrdinal;
}

class EVC_CDF
{
 static const string KEY = "evcSeat";
 static const int VERSION = 1;
 static const int MAX_SEATS = 1024;
 static const int IMMEDIATE_SEATS = 32;
 static const int SEATS_PER_PUMP = 16;
 static const int PUMP_MS = 100;
 static const int RETRY_MS = 500;
 static const int MAX_ATTEMPTS = 4;
 static const int MAX_POLLS = 600;
 static const float PROP_RADIUS = 0.35;
 static const int RESULT_PENDING = 0;
 static const int RESULT_REQUESTED = 1;
 static const int RESULT_SEATED = 2;
 static const int RESULT_STANDING = 3;
 static const int RESULT_FAILED = 4;
 static const int REFUSAL_WINDOW_MS = 2000;

 static ref CDF_GMSaveDocument s_Document;
 static ref array<ref EVC_Seat> s_aSeats;
 static BaseWorld s_World;
 static bool s_bActive;
 static int s_iPolls, s_iSkipped, s_iWorkMs, s_iRestored;
 // Finished passes and the last one's totals (native fixture).
 static int s_iPasses, s_iLastRestored, s_iLastSeated, s_iLastStanding, s_iLastFailed, s_iLastSkipped;
 // Last save (native fixture).
 static int s_iSavedSeats, s_iSavedLinked, s_iSavedProps, s_iSavedGroups, s_iSavedMembers, s_iSavedSkipped;
 static string s_sLastRefusal;
 static int s_iLastRefusalTick;

 //------------------------------------------------------------------------------------------------
 // ACE animation helpers (captives, carrying, CPR) are compartments too, never seats.
 static bool IsHelper(IEntity holder)
 {
  string name = holder.ClassName();
  name.ToLower();
  if (name.Contains("helpercompartment"))
  {
   return true;
  }
  string path = CDF_GMSaveState.GetPrefabName(holder);
  path.ToLower();
  return path.Contains("helpercompartment") || path.Contains("/helpers/");
 }
 static bool IsVehicle(IEntity holder)
 {
  return holder && holder.FindComponent(SCR_EditableVehicleComponent) != null;
 }
 static bool IsPlayer(IEntity entity)
 {
  PlayerManager players = GetGame().GetPlayerManager();
  return players && players.GetPlayerIdFromControlledEntity(entity) > 0;
 }
 static SCR_AIGroup GroupOf(IEntity character)
 {
  AIAgent agent = SCR_AIUtils.GetAIAgent(character);
  if (!agent)
  {
   return null;
  }
  return SCR_AIGroup.Cast(agent.GetParentGroup());
 }
 // A living AI soldier this bridge may keep: no player, prisoner or garrison guard, not in a
 // group Unit Caching (Full) or ambient traffic reserves, not in an animation helper.
 static bool Keepable(IEntity entity)
 {
  ChimeraCharacter character = ChimeraCharacter.Cast(entity);
  if (!character || character.IsDeleted() || IsPlayer(character))
  {
   return false;
  }
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.GetLifeState() == ECharacterLifeState.DEAD)
  {
   return false;
  }
  // Prisoners and ACE captives belong to the prisoner bridge (saved on foot, state re-applied).
  if (EPR_CDF.IsCaptiveSeat(character) || EXPG_GarrisonPersistence.OwnsForSave(character))
  {
   return false;
  }
  SCR_AIGroup group = GroupOf(character);
  if (group && EBG_CacheManager.Instance && EBG_CacheManager.Instance.IsReserved(group))
  {
   return false;
  }
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (access && access.GetCompartment() && access.GetCompartment().GetVehicle() && IsHelper(access.GetCompartment().GetVehicle()))
  {
   return false;
  }
  return true;
 }
 // The compartment a keepable soldier fully sits in, with its holder (top entity with a
 // compartment manager: vehicle, static weapon, chair seat) and its index in the holder's
 // compartment list (stable for the same prefab).
 static BaseCompartmentSlot SeatOf(IEntity entity, out IEntity holder, out int index)
 {
  holder = null;
  index = -1;
  if (!Keepable(entity))
  {
   return null;
  }
  ChimeraCharacter character = ChimeraCharacter.Cast(entity);
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (!access || !access.IsInCompartment() || access.IsGettingIn() || access.IsGettingOut())
  {
   return null;
  }
  BaseCompartmentSlot slot = access.GetCompartment();
  if (!slot || slot.GetOccupant() != character)
  {
   return null;
  }
  int found = -1;
  IEntity top = slot.GetVehicle(found);
  if (!top || found < 0 || IsHelper(top))
  {
   return null;
  }
  holder = top;
  index = found;
  return slot;
 }
 static bool Serializable(SCR_EditableEntityComponent editable)
 {
  return editable && !editable.HasEntityFlag(EEditableEntityFlag.LOCAL) && !editable.HasEntityFlag(EEditableEntityFlag.NON_SERIALIZABLE);
 }

 //------------------------------------------------------------------------------------------------
 // Seat payload and the extra top-level key in a stored state.
 static string Encode(int type, int index, bool linked, ResourceName prefab, vector position, string faction)
 {
  JsonSaveContext context = new JsonSaveContext();
  string path = prefab;
  context.WriteValue("v", VERSION);
  context.WriteValue("t", type);
  context.WriteValue("i", index);
  context.WriteValue("veh", linked);
  context.WriteValue("p", path);
  context.WriteValue("x", position[0]);
  context.WriteValue("y", position[1]);
  context.WriteValue("z", position[2]);
  context.WriteValue("f", faction);
  return context.SaveToString();
 }
 static EVC_Seat Decode(string payload, out string reason)
 {
  JsonLoadContext context = new JsonLoadContext();
  if (!context.LoadFromString(payload))
  {
   reason = "seat payload is not JSON";
   return null;
  }
  int version;
  if (!context.ReadValue("v", version) || version != VERSION)
  {
   reason = string.Format("unknown seat version %1", version);
   return null;
  }
  int type;
  int index;
  bool linked;
  string path;
  float x;
  float y;
  float z;
  string faction;
  if (!context.ReadValue("t", type) || !context.ReadValue("i", index) || !context.ReadValue("veh", linked) || !context.ReadValue("p", path) || !context.ReadValue("x", x) || !context.ReadValue("y", y) || !context.ReadValue("z", z))
  {
   reason = "seat payload lacks a field";
   return null;
  }
  context.ReadValue("f", faction);
  if (index < 0 || type < 0)
  {
   reason = "seat payload has a negative seat";
   return null;
  }
  EVC_Seat seat = new EVC_Seat();
  seat.m_iType = type;
  seat.m_iIndex = index;
  seat.m_bLinked = linked;
  seat.m_sFaction = faction;
  seat.m_sHolderPrefab = path;
  seat.m_vHolderPos = Vector(x, y, z);
  return seat;
 }
 // {"evcSeat":"<payload>" without its closing brace.
 static string Head(string payload)
 {
  JsonSaveContext context = new JsonSaveContext();
  if (!context.WriteValue(KEY, payload))
  {
   return "";
  }
  string head = context.SaveToString();
  head = head.Trim();
  int close = head.LastIndexOf("}");
  if (close <= 0)
  {
   return "";
  }
  return head.Substring(0, close);
 }
 // The state with the seat key added first; "" when it cannot be added losslessly.
 static string Tag(string state, string payload)
 {
  string head = Head(payload);
  if (head.IsEmpty())
  {
   return "";
  }
  string body = state.Trim();
  string tagged;
  if (body.IsEmpty() || body == "{}")
  {
   tagged = head + "}";
  }
  else
  {
   if (body.Length() < 2 || body.Substring(0, 1) != "{" || body.Substring(body.Length() - 1, 1) != "}")
   {
    return "";
   }
   string inner = body.Substring(1, body.Length() - 1);
   tagged = head + "," + inner;
  }
  JsonLoadContext check = new JsonLoadContext();
  string back;
  if (!check.LoadFromString(tagged) || !check.ReadValue(KEY, back) || back != payload)
  {
   return "";
  }
  // Lossless: the inner chain's state is exactly what follows the key.
  string original;
  bool untagged = Untag(tagged, payload, original);
  bool same = original == body || (original.IsEmpty() && body == "{}");
  if (!untagged || !same)
  {
   return "";
  }
  return tagged;
 }
 // The stored state without the seat key, exactly as the inner chain wrote it (save check).
 static bool Untag(string tagged, string payload, out string original)
 {
  original = "";
  string head = Head(payload);
  if (head.IsEmpty() || tagged.IndexOf(head) != 0)
  {
   return false;
  }
  string rest = tagged.Substring(head.Length(), tagged.Length() - head.Length());
  if (rest == "}")
  {
   return true;
  }
  if (rest.Length() < 2 || rest.Substring(0, 1) != ",")
  {
   return false;
  }
  original = "{" + rest.Substring(1, rest.Length() - 1);
  JsonLoadContext check = new JsonLoadContext();
  return check.LoadFromString(original);
 }

 //------------------------------------------------------------------------------------------------
 static CDF_GMSaveEntityRecord NewRecord(SCR_EditableEntityComponent editable, int parent)
 {
  IEntity owner = editable.GetOwner();
  ResourceName prefab = editable.GetPrefab(true);
  if (!owner || prefab.IsEmpty())
  {
   return null;
  }
  CDF_GMSaveEntityRecord record = new CDF_GMSaveEntityRecord();
  record.m_Entity = editable;
  record.m_iParent = parent;
  record.m_sPrefab = prefab;
  record.m_iEntityFlags = editable.GetEntityFlags();
  record.m_iEntityType = editable.GetEntityType();
  record.m_fScale = owner.GetScale();
  record.m_bDirtyHierarchy = editable.HasEntityFlag(EEditableEntityFlag.INDIVIDUAL_CHILDREN) || editable.HasEntityFlag(EEditableEntityFlag.DIRTY_HIERARCHY);
  vector transform[4];
  owner.GetWorldTransform(transform);
  record.m_vPosition = transform[3];
  float quat[4];
  Math3D.MatrixToQuat(transform, quat);
  record.m_fQuatX = quat[0];
  record.m_fQuatY = quat[1];
  record.m_fQuatZ = quat[2];
  record.m_fQuatW = quat[3];
  if (CDF_GMSaveConfig.GetInstance().m_bSaveAttributes)
  {
   CDF_GMSaveAttributes.Read(editable, record.m_aAttributeIds, record.m_aAttributeX, record.m_aAttributeY, record.m_aAttributeZ);
  }
  record.m_sState = CDF_GMSaveState.Capture(owner);
  return record;
 }
 // The default crew group of a saved vehicle that CDF did not save (no author).
 static SCR_AIGroup Adoptable(IEntity occupant, notnull map<IEntity, int> saved)
 {
  IEntity holder;
  int index;
  if (!SeatOf(occupant, holder, index))
  {
   return null;
  }
  SCR_AIGroup group = GroupOf(occupant);
  if (!group || !group.m_bEVC_DefaultCrew || saved.Contains(group) || EXPG_GarrisonPersistence.OwnsForSave(group))
  {
   return null;
  }
  if (!Serializable(SCR_EditableEntityComponent.GetEditableEntity(group)))
  {
   return null;
  }
  return group;
 }
 // Adds the group (author of the vehicle) and its keepable members as CDF records.
 static int Adopt(notnull CDF_GMSaveDocument document, SCR_AIGroup group, CDF_GMSaveEntityRecord vehicleRecord, notnull map<IEntity, int> saved)
 {
  CDF_GMSaveEntityRecord groupRecord = NewRecord(SCR_EditableEntityComponent.GetEditableEntity(group), -1);
  if (!groupRecord)
  {
   return 0;
  }
  groupRecord.m_sAuthorUID = vehicleRecord.m_sAuthorUID;
  groupRecord.m_sAuthorPlatformID = vehicleRecord.m_sAuthorPlatformID;
  groupRecord.m_iAuthorPlatform = vehicleRecord.m_iAuthorPlatform;
  groupRecord.m_iAuthorUpdated = vehicleRecord.m_iAuthorUpdated;
  int groupIndex = document.m_aEntities.Count();
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  array<ref CDF_GMSaveEntityRecord> members = {};
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   IEntity member = agent.GetControlledEntity();
   if (!member || saved.Contains(member) || !Keepable(member)) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(member);
   if (!Serializable(editable)) continue;
   CDF_GMSaveEntityRecord memberRecord = NewRecord(editable, groupIndex);
   if (memberRecord) members.Insert(memberRecord);
  }
  if (members.IsEmpty())
  {
   return 0;
  }
  saved.Set(group, groupIndex);
  document.m_aEntities.Insert(groupRecord);
  foreach (CDF_GMSaveEntityRecord added : members)
  {
   saved.Set(added.m_Entity.GetOwner(), document.m_aEntities.Count());
   document.m_aEntities.Insert(added);
  }
  return members.Count();
 }
 // Adds the default crews CDF skipped and the seat of every saved seated soldier. false only
 // when an added crew member's state fails another adapter's save check.
 static bool CaptureCrew(notnull CDF_GMSaveDocument document, out string reason)
 {
  reason = "";
  int timing = System.GetTickCount();
  s_iSavedSeats = 0;
  s_iSavedLinked = 0;
  s_iSavedProps = 0;
  s_iSavedGroups = 0;
  s_iSavedMembers = 0;
  s_iSavedSkipped = 0;
  map<IEntity, int> saved = new map<IEntity, int>();
  foreach (int index, CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (record && record.m_Entity && record.m_Entity.GetOwner()) saved.Set(record.m_Entity.GetOwner(), index);
  }
  int firstAdded = document.m_aEntities.Count();
  for (int vehicleIndex = 0; vehicleIndex < firstAdded; vehicleIndex++)
  {
   CDF_GMSaveEntityRecord vehicleRecord = document.m_aEntities[vehicleIndex];
   if (!vehicleRecord || !vehicleRecord.m_Entity || SCR_Enum.HasFlag(vehicleRecord.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED)) continue;
   IEntity vehicle = vehicleRecord.m_Entity.GetOwner();
   if (!IsVehicle(vehicle)) continue;
   BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
   if (!manager) continue;
   array<BaseCompartmentSlot> slots = {};
   manager.GetCompartments(slots);
   foreach (BaseCompartmentSlot slot : slots)
   {
    if (!slot || !slot.GetOccupant() || saved.Contains(slot.GetOccupant())) continue;
    SCR_AIGroup group = Adoptable(slot.GetOccupant(), saved);
    if (!group) continue;
    int added = Adopt(document, group, vehicleRecord, saved);
    if (added <= 0) continue;
    s_iSavedGroups++;
    s_iSavedMembers += added;
   }
  }
  int count = document.m_aEntities.Count();
  for (int characterIndex = 0; characterIndex < count; characterIndex++)
  {
   CDF_GMSaveEntityRecord crew = document.m_aEntities[characterIndex];
   if (!crew || !crew.m_Entity || crew.m_iTarget != CDF_GMSaveEntityRecord.TARGET_NONE || crew.m_iEntityType != EEditableEntityType.CHARACTER) continue;
   if (SCR_Enum.HasFlag(crew.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED)) continue;
   IEntity holder;
   int seatIndex;
   BaseCompartmentSlot seat = SeatOf(crew.m_Entity.GetOwner(), holder, seatIndex);
   if (!seat) continue;
   int holderIndex = -1;
   if (IsVehicle(holder)) saved.Find(holder, holderIndex);
   ResourceName holderPrefab = CDF_GMSaveState.GetPrefabName(holder);
   if (s_iSavedSeats >= MAX_SEATS || (holderIndex < 0 && holderPrefab.IsEmpty()))
   {
    s_iSavedSkipped++;
    continue;
   }
   string faction;
   SCR_AIGroup crewGroup = GroupOf(crew.m_Entity.GetOwner());
   if (crewGroup) faction = crewGroup.GetFactionName();
   string payload = Encode(seat.GetType(), seatIndex, holderIndex >= 0, holderPrefab, holder.GetOrigin(), faction);
   string tagged = Tag(crew.m_sState, payload);
   if (tagged.IsEmpty())
   {
    s_iSavedSkipped++;
    continue;
   }
   crew.m_sState = tagged;
   s_iSavedSeats++;
   if (holderIndex < 0)
   {
    s_iSavedProps++;
    continue;
   }
   crew.m_iTarget = holderIndex;
   crew.m_iTargetValue = seatIndex;
   s_iSavedLinked++;
  }
  if (document.m_aEntities.Count() > firstAdded && !ValidAdded(document, firstAdded, reason))
  {
   return false;
  }
  if (s_iSavedSeats > 0 || s_iSavedGroups > 0 || s_iSavedSkipped > 0)
  {
   PrintFormat("[EXPBG CDF CREW SAVE] seats=%1 vehicleLinked=%2 propSeats=%3 addedGroups=%4 addedMembers=%5 skipped=%6 ownMs=%7", s_iSavedSeats, s_iSavedLinked, s_iSavedProps, s_iSavedGroups, s_iSavedMembers, s_iSavedSkipped, System.GetTickCount() - timing);
  }
  return true;
 }
 // Added records pass the save checks the other adapters run on CDF's own records.
 static bool ValidAdded(notnull CDF_GMSaveDocument document, int first, out string reason)
 {
  CDF_GMSaveDocument added = new CDF_GMSaveDocument();
  for (int i = first; i < document.m_aEntities.Count(); i++)
  {
   added.m_aEntities.Insert(document.m_aEntities[i]);
  }
  int payloads;
  int unread;
  string why;
  if (!EUS_CDF.ValidDocument(added, false, payloads, unread, why))
  {
   reason = "A vehicle crew member's unit script cannot be saved: " + why;
   return false;
  }
  if (!EUD_CDF.ValidDocument(added, false, payloads, why))
  {
   reason = "A vehicle crew member's unit dialog cannot be saved: " + why;
   return false;
  }
  if (!EII_CDFPayload.ValidDocument(added))
  {
   reason = "A vehicle crew member's Intel Items state failed Intel Items validation";
   return false;
  }
  return true;
 }

 //------------------------------------------------------------------------------------------------
 // Clear (CDF_GMSaveCapture.IsManaged): the default crew of a vehicle CDF manages goes with it.
 static bool ClearedWithVehicle(SCR_EditableEntityComponent entity)
 {
  if (!entity || entity.GetEntityType() != EEditableEntityType.GROUP || !Serializable(entity))
  {
   return false;
  }
  SCR_AIGroup group = SCR_AIGroup.Cast(entity.GetOwner());
  if (!group || !group.m_bEVC_DefaultCrew || EXPG_GarrisonPersistence.OwnsForSave(group))
  {
   return false;
  }
  if (EBG_CacheManager.Instance && EBG_CacheManager.Instance.IsReserved(group))
  {
   return false;
  }
  array<AIAgent> agents = {};
  group.GetAgents(agents);
  foreach (AIAgent agent : agents)
  {
   if (!agent) continue;
   IEntity holder;
   int index;
   if (!SeatOf(agent.GetControlledEntity(), holder, index) || !IsVehicle(holder)) continue;
   if (CDF_GMSaveCapture.IsManaged(SCR_EditableEntityComponent.GetEditableEntity(holder)))
   {
    return true;
   }
  }
  return false;
 }

 //------------------------------------------------------------------------------------------------
 // Load: every seat of the document, read before CDF changes anything.
 static bool ReadDocument(notnull CDF_GMSaveDocument document, notnull array<ref EVC_Seat> seats, out int skipped, out string reason)
 {
  skipped = 0;
  reason = "";
  int count = document.m_aEntities.Count();
  string marker = "\"" + KEY + "\"";
  foreach (int index, CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record || !record.m_sState.Contains(marker)) continue;
   JsonLoadContext context = new JsonLoadContext();
   string payload;
   if (!context.LoadFromString(record.m_sState) || !context.ReadValue(KEY, payload))
   {
    reason = string.Format("Unreadable vehicle crew state on record %1 (%2)", index, record.m_sPrefab);
    return false;
   }
   bool character = record.m_iEntityType < 0 || record.m_iEntityType == EEditableEntityType.CHARACTER;
   if (!character || SCR_Enum.HasFlag(record.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED) || seats.Count() >= MAX_SEATS)
   {
    skipped++;
    continue;
   }
   string why;
   EVC_Seat seat = Decode(payload, why);
   if (!seat)
   {
    skipped++;
    Print(string.Format("[EXPBG CDF CREW] Seat of record %1 (%2) skipped: %3", index, record.m_sPrefab, why), LogLevel.WARNING);
    continue;
   }
   seat.m_Record = record;
   seat.m_iTarget = record.m_iTarget;
   if (seat.m_bLinked && record.m_iTarget >= 0 && record.m_iTarget < count) seat.m_Holder = document.m_aEntities[record.m_iTarget];
   if (!seat.m_Holder) seat.m_bLinked = false;
   seat.m_iOrdinal = seats.Count();
   seats.Insert(seat);
  }
  return true;
 }
 // While CDF restores, no crew link: CDF would replay it as the GM move-in, into the first
 // free seat. The stored states stay as they are (the seat key is ignored by every reader,
 // and Intel Items matches the states it validated before the clear by their exact text).
 static void Hide(notnull array<ref EVC_Seat> seats)
 {
  foreach (EVC_Seat seat : seats)
  {
   if (seat.m_bLinked) seat.m_Record.m_iTarget = CDF_GMSaveEntityRecord.TARGET_NONE;
  }
 }
 // The crew links as they were read (CDF's repair may have renumbered the records).
 static void Reveal(notnull CDF_GMSaveDocument document, notnull array<ref EVC_Seat> seats, int countBefore)
 {
  bool renumbered = document.m_aEntities.Count() != countBefore;
  foreach (EVC_Seat seat : seats)
  {
   if (!seat.m_bLinked) continue;
   if (!renumbered)
   {
    seat.m_Record.m_iTarget = seat.m_iTarget;
    continue;
   }
   seat.m_Record.m_iTarget = document.m_aEntities.Find(seat.m_Holder);
  }
 }

 //------------------------------------------------------------------------------------------------
 static void BeginLoad(CDF_GMSaveDocument document, notnull array<ref EVC_Seat> seats, int skipped)
 {
  EndLoad();
  s_Document = document;
  s_aSeats = seats;
  s_World = GetGame().GetWorld();
  s_bActive = true;
  s_iPolls = 0;
  s_iWorkMs = 0;
  s_iSkipped = skipped;
  s_iRestored = 0;
  foreach (EVC_Seat seat : seats)
  {
   if (seat.m_Record.m_Entity) seat.m_Character = seat.m_Record.m_Entity.GetOwner();
   if (seat.m_Character)
   {
    s_iRestored++;
    continue;
   }
   seat.m_iResult = RESULT_FAILED;
  }
  // The same frame as CDF's spawn: the first seats are taken before anybody stands up.
  Work(IMMEDIATE_SEATS, false);
 }
 // A previous pass still running when another load replaces the scene ends here.
 static void EndLoad()
 {
  if (s_bActive) Finish(true);
 }
 static bool IsActive()
 {
  return s_bActive;
 }
 static bool Resolved(EVC_Seat seat)
 {
  return seat.m_iResult == RESULT_SEATED || seat.m_iResult == RESULT_STANDING || seat.m_iResult == RESULT_FAILED;
 }
 // One pump: at most SEATS_PER_PUMP seats advance. true while seats remain.
 static bool Work(int budget, bool cdfDone)
 {
  if (!s_bActive)
  {
   return false;
  }
  if (!GetGame() || GetGame().GetWorld() != s_World)
  {
   Finish(true);
   return false;
  }
  int start = System.GetTickCount();
  ChimeraWorld world = GetGame().GetWorld();
  bool paused = world && world.IsGameTimePaused();
  bool open = false;
  foreach (EVC_Seat seat : s_aSeats)
  {
   if (Resolved(seat)) continue;
   open = true;
   if (budget <= 0 || seat.m_iDue - start > 0) continue;
   budget--;
   Advance(seat, cdfDone, paused);
  }
  s_iWorkMs += System.GetTickCount() - start;
  return open;
 }
 static bool Pump(bool cdfDone)
 {
  if (!s_bActive)
  {
   return false;
  }
  s_iPolls++;
  bool open = Work(SEATS_PER_PUMP, cdfDone);
  if (!s_bActive)
  {
   return false;
  }
  if (!open || s_iPolls >= MAX_POLLS)
  {
   Finish(false);
   return false;
  }
  return true;
 }
 protected static void Advance(EVC_Seat seat, bool cdfDone, bool paused)
 {
  int now = System.GetTickCount();
  ChimeraCharacter character = ChimeraCharacter.Cast(seat.m_Character);
  if (!character || character.IsDeleted() || !character.GetCharacterController() || character.GetCharacterController().GetLifeState() == ECharacterLifeState.DEAD)
  {
   seat.m_iResult = RESULT_FAILED;
   return;
  }
  if (seat.m_iResult == RESULT_PENDING)
  {
   seat.m_iResult = RESULT_REQUESTED;
   Request(seat, character, now);
   return;
  }
  BaseCompartmentSlot slot = Slot(seat);
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (slot && access && slot.GetOccupant() == character && access.GetCompartment() == slot)
  {
   // The group and its vehicle list are CDF's until its deferred repair is done.
   if (!cdfDone)
   {
    seat.m_iDue = now + RETRY_MS;
    return;
   }
   Settle(seat, character);
   seat.m_iResult = RESULT_SEATED;
   return;
  }
  if (paused)
  {
   seat.m_iDue = now + RETRY_MS;
   return;
  }
  if (seat.m_iAttempts < MAX_ATTEMPTS)
  {
   Request(seat, character, now);
   return;
  }
  Stand(seat, character);
  seat.m_iResult = RESULT_STANDING;
 }
 // The saved holder: its record's entity, or the prop of the saved prefab at the saved spot.
 static BaseCompartmentSlot Slot(EVC_Seat seat)
 {
  if (!seat.m_HolderEntity || seat.m_HolderEntity.IsDeleted())
  {
   seat.m_HolderEntity = null;
   if (seat.m_bLinked && seat.m_Holder && seat.m_Holder.m_Entity) seat.m_HolderEntity = seat.m_Holder.m_Entity.GetOwner();
   else if (!seat.m_bLinked) seat.m_HolderEntity = FindProp(seat.m_sHolderPrefab, seat.m_vHolderPos);
  }
  if (!seat.m_HolderEntity)
  {
   return null;
  }
  BaseCompartmentManagerComponent manager = BaseCompartmentManagerComponent.Cast(seat.m_HolderEntity.FindComponent(BaseCompartmentManagerComponent));
  if (!manager)
  {
   return null;
  }
  array<BaseCompartmentSlot> slots = {};
  manager.GetCompartments(slots);
  if (seat.m_iIndex >= slots.Count())
  {
   return null;
  }
  return slots[seat.m_iIndex];
 }
 // Instant move into the saved seat. A seat that is gone, taken or of another type, or a
 // holder that does not exist after CDF's spawn, ends the attempts (the soldier stands).
 protected static void Request(EVC_Seat seat, ChimeraCharacter character, int now)
 {
  seat.m_iAttempts++;
  seat.m_iDue = now + RETRY_MS;
  BaseCompartmentSlot slot = Slot(seat);
  if (!slot)
  {
   if (seat.m_bLinked || seat.m_HolderEntity) seat.m_iAttempts = MAX_ATTEMPTS;
   return;
  }
  IEntity occupant = slot.GetOccupant();
  if (occupant == character)
  {
   return;
  }
  CompartmentAccessComponent access = character.GetCompartmentAccessComponent();
  if (occupant || !access || !slot.IsCompartmentAccessible() || slot.GetType() != seat.m_iType || access.IsInCompartment())
  {
   seat.m_iAttempts = MAX_ATTEMPTS;
   return;
  }
  access.GetInVehicle(slot.GetOwner(), slot, true, -1, ECloseDoorAfterActions.INVALID, true);
 }
 // A seated crew's group knows its vehicle (as the GM move-in does) and keeps its faction.
 protected static void Settle(EVC_Seat seat, ChimeraCharacter character)
 {
  SCR_AIGroup group = GroupOf(character);
  if (!group)
  {
   return;
  }
  if (!seat.m_sFaction.IsEmpty() && group.GetFactionName().IsEmpty()) group.InitFactionKey(seat.m_sFaction);
  if (!IsVehicle(seat.m_HolderEntity))
  {
   return;
  }
  IEntity usageOwner;
  SCR_AIVehicleUsageComponent usage = SCR_AIVehicleUsageComponent.FindOnNearestParent(seat.m_HolderEntity, usageOwner);
  SCR_AIGroupUtilityComponent utility = group.GetGroupUtilityComponent();
  if (usage && utility) utility.AddUsableVehicle(usage);
 }
 // Next to the holder, on its floor level (the GM transform keeps him above ground);
 // where CDF put him when the holder is gone.
 protected static void Stand(EVC_Seat seat, ChimeraCharacter character)
 {
  IEntity holder = seat.m_HolderEntity;
  if (!holder || holder.IsDeleted())
  {
   return;
  }
  vector mins;
  vector maxs;
  holder.GetBounds(mins, maxs);
  float side = Math.Clamp(maxs[0] + 1.0, 1.0, 6.0);
  int row = seat.m_iOrdinal - (seat.m_iOrdinal / 5) * 5;
  float along = Math.Min(mins[2] + 0.5 + row * 0.9, maxs[2]);
  vector position = holder.CoordToParent(Vector(side, 0, along));
  vector origin = holder.GetOrigin();
  position[1] = origin[1];
  vector angles = holder.GetYawPitchRoll();
  vector transform[4];
  Math3D.AnglesToMatrix(Vector(angles[0], 0, 0), transform);
  transform[3] = position;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(character);
  if (editable)
  {
   editable.SetTransform(transform);
   return;
  }
  character.SetWorldTransform(transform);
 }
 // A prop CDF does not save, found by prefab and spot (it or a direct child).
 static IEntity FindProp(ResourceName prefab, vector position)
 {
  if (prefab.IsEmpty() || !GetGame().GetWorld())
  {
   return null;
  }
  EVC_PropQuery query = new EVC_PropQuery();
  query.m_sPrefab = prefab;
  query.m_vPosition = position;
  query.m_fBest = PROP_RADIUS * PROP_RADIUS;
  GetGame().GetWorld().QueryEntitiesBySphere(position, PROP_RADIUS + 2.0, query.Add);
  return query.m_Best;
 }

 //------------------------------------------------------------------------------------------------
 static void Finish(bool interrupted)
 {
  if (!s_bActive)
  {
   return;
  }
  s_bActive = false;
  int seated;
  int standing;
  int failed;
  int expected;
  if (s_aSeats)
  {
   expected = s_aSeats.Count();
   foreach (EVC_Seat seat : s_aSeats)
   {
    if (seat.m_iResult == RESULT_SEATED) seated++;
    else if (seat.m_iResult == RESULT_STANDING) standing++;
    else failed++;
   }
  }
  s_iPasses++;
  s_iLastRestored = s_iRestored;
  s_iLastSeated = seated;
  s_iLastStanding = standing;
  s_iLastFailed = failed;
  s_iLastSkipped = s_iSkipped;
  int cut = 0;
  if (interrupted) cut = 1;
  LogLevel severity = LogLevel.NORMAL;
  if (standing > 0 || failed > 0 || s_iSkipped > 0 || cut > 0) severity = LogLevel.WARNING;
  string first = string.Format("[EXPBG CDF CREW] restored=%1 seated=%2 fallbackStanding=%3 failed=%4", s_iRestored, seated, standing, failed);
  string second = string.Format(" skipped=%1 expected=%2 polls=%3 workMs=%4 interrupted=%5", s_iSkipped, expected, s_iPolls, s_iWorkMs, cut);
  Print(first + second, severity);
  s_aSeats = null;
  s_Document = null;
  s_World = null;
 }

 //------------------------------------------------------------------------------------------------
 // First refusal of the current request, shown to the requesting GM with CDF's feedback.
 static void Reject(string reason)
 {
  Print("[EXPBG CDF CREW HOLD] " + reason, LogLevel.WARNING);
  int now = System.GetTickCount();
  int age = now - s_iLastRefusalTick;
  if (s_sLastRefusal.IsEmpty() || age < 0 || age > REFUSAL_WINDOW_MS)
  {
   s_sLastRefusal = reason;
   s_iLastRefusalTick = now;
  }
 }
 static string TakeRefusal()
 {
  string reason = s_sLastRefusal;
  int age = System.GetTickCount() - s_iLastRefusalTick;
  s_sLastRefusal = "";
  if (age < 0 || age > REFUSAL_WINDOW_MS)
  {
   return "";
  }
  if (reason.Length() > 1024) reason = reason.Substring(0, 1024);
  return reason;
 }
}

// Nearest entity (or direct child) of one prefab within PROP_RADIUS of a spot, with compartments.
class EVC_PropQuery
{
 ResourceName m_sPrefab;
 vector m_vPosition;
 float m_fBest;
 IEntity m_Best;
 int m_iSeen;

 bool Add(IEntity entity)
 {
  m_iSeen++;
  if (m_iSeen > 256)
  {
   return false;
  }
  Consider(entity);
  IEntity child = entity.GetChildren();
  int children = 0;
  while (child && children < 32)
  {
   Consider(child);
   child = child.GetSibling();
   children++;
  }
  return true;
 }
 protected void Consider(IEntity entity)
 {
  if (!entity || entity.IsDeleted() || !entity.FindComponent(BaseCompartmentManagerComponent))
  {
   return;
  }
  float distance = vector.DistanceSq(entity.GetOrigin(), m_vPosition);
  if (distance > m_fBest || CDF_GMSaveState.GetPrefabName(entity) != m_sPrefab)
  {
   return;
  }
  m_fBest = distance;
  m_Best = entity;
 }
}

// Groups a vehicle's compartment manager spawned as its default crew (GM "crewed" or "with
// passengers" placement, Spawn Occupants): CDF does not save them (no author).
modded class SCR_AIGroup
{
 bool m_bEVC_DefaultCrew;
}

modded class SCR_BaseCompartmentManagerComponent
{
 override protected void FinishedSpawningDefaultOccupants(bool wasCanceled)
 {
  SCR_AIGroup crew = SCR_AIGroup.Cast(m_SpawnedOccupantsAIGroup);
  if (crew) crew.m_bEVC_DefaultCrew = true;
  super.FinishedSpawningDefaultOccupants(wasCanceled);
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  // Another adapter refused; keep its refusal.
  if (!document)
  {
   return null;
  }
  string reason;
  if (!EVC_CDF.CaptureCrew(document, reason))
  {
   EVC_CDF.Reject(reason + "; nothing was written");
   return null;
  }
  return document;
 }

 // Clear calls this public predicate: a saved vehicle's default crew goes with it.
 override static bool IsManaged(SCR_EditableEntityComponent entity)
 {
  if (super.IsManaged(entity))
  {
   return true;
  }
  return EVC_CDF.ClearedWithVehicle(entity);
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  if (!Replication.IsServer())
  {
   return super.Restore(document);
  }
  int timing = System.GetTickCount();
  array<ref EVC_Seat> seats = {};
  int skipped;
  string reason;
  if (!EVC_CDF.ReadDocument(document, seats, skipped, reason))
  {
   EVC_CDF.Reject(reason + "; the scene was not cleared");
   return false;
  }
  int count = document.m_aEntities.Count();
  EVC_CDF.Hide(seats);
  int inner = System.GetTickCount();
  bool result = super.Restore(document);
  int innerEnd = System.GetTickCount();
  EVC_CDF.Reveal(document, seats, count);
  if (result)
  {
   GetGame().GetCallqueue().Remove(EVC_PumpCrew);
   if (seats.IsEmpty() && skipped == 0)
   {
    // Existing saves without seats: nothing to do (a previous pass ends here).
    EVC_CDF.EndLoad();
   }
   else
   {
    EVC_CDF.BeginLoad(document, seats, skipped);
    GetGame().GetCallqueue().CallLater(EVC_PumpCrew, EVC_CDF.PUMP_MS, false);
   }
  }
  if (!seats.IsEmpty() || skipped > 0)
  {
   int before = inner - timing;
   int after = System.GetTickCount() - innerEnd;
   PrintFormat("[CDF TIMING] vehicle-crew restore result=%1 seats=%2 skipped=%3 ownMs=%4 beforeMs=%5 afterMs=%6 innerMs=%7", result, seats.Count(), skipped, before + after, before, after, innerEnd - inner);
  }
  return result;
 }
 // Bounded pump; group work waits for CDF's deferred finalization (repair, state pass).
 static void EVC_PumpCrew()
 {
  bool complete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty() && s_aGuardedGroups && s_aGuardedGroups.IsEmpty() && s_aPendingMembers && s_aPendingMembers.IsEmpty();
  if (EVC_CDF.Pump(complete) && GetGame())
  {
   GetGame().GetCallqueue().CallLater(EVC_PumpCrew, EVC_CDF.PUMP_MS, false);
  }
 }
}

// GM-facing refusal: CDF reports save/load results as hints only and never says why an
// adapter refused. Show the vehicle crew reason in a CDF-style dialog.
modded class SCR_PlayerController
{
 override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)
 {
  super.CDF_GMSave_Feedback(result);
  string action;
  if (result.m_sKey == "#CDF_GMSave_Msg_LoadAborted") action = "Load";
  else if (result.m_sKey == "#CDF_GMSave_Msg_CaptureFailed") action = "Save";
  string reason;
  if (!action.IsEmpty()) reason = EVC_CDF.TakeRefusal();
  if (reason.IsEmpty())
  {
   return;
  }
  string message = action + " refused by EXPBG vehicle crews:\n\n" + reason + "\n\nDetails: [EXPBG CDF CREW HOLD] in the server log.";
  // A hosting GM owns this controller locally; an owner RPC would not reach it.
  if (GetGame().GetPlayerController() == this) EVC_CDF_RpcDo_Refused(message);
  else Rpc(EVC_CDF_RpcDo_Refused, message);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EVC_CDF_RpcDo_Refused(string message)
 {
  EVC_CDFRefusalDialog.Open(message);
 }
}
class EVC_CDFRefusalDialog : CDF_GMSaveBaseDialog
{
 static void Open(string message)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager())
  {
   return;
  }
  SCR_ConfigurableDialogUiPreset preset = CDF_GMSaveDialogUtils.CreatePreset("EXPBG_EVC_CDF_REFUSED", "#CDF_GMSave_Hint_Title");
  preset.m_eVisualStyle = EDialogType.WARNING;
  preset.m_sMessage = message;
  preset.m_aButtons.Insert(CDF_GMSaveDialogUtils.CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "#CDF_GMSave_Btn_Close", EConfigurableDialogUiButtonAlign.LEFT, "MenuBack"));
  EVC_CDFRefusalDialog dialog = new EVC_CDFRefusalDialog();
  CreateByPreset(preset, dialog);
 }
}
