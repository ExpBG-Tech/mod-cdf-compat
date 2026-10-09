// EXPBG CDF Compat - Inventory completion. Copyright 2026 ExpBG Tech.
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 empties every cargo storage of a restored entity (vest pouches,
// bags, crates, vehicle cargo) and respawns its saved items one by one. An item its saved
// storage does not take back goes to the engine's own placement ("replaces par le moteur"
// in CDF's inventory log line) and some of those vanish (native run 2026-10-09: magazines
// and M67 grenades of a vanilla US fire team, with or without a unit script). This pass
// completes the inventory once CDF is done:
//  - CDF_GMSaveState.Apply (wrapped) notes each entity whose saved inventory CDF really
//    applied: CDF's per-item counters moved by exactly the saved item count. The saved lists
//    ("ivp" and companions) are read from the CDF state, under any EXPBG "cdfState"
//    envelopes. The items CDF refused itself are named by its own refusal trace and matched
//    to its refusal counter; an entity whose refusals cannot all be named is left alone.
//  - SETTLE_MS after CDF's deferred state pass, the saved prefab multiset minus CDF's
//    refusals is compared with what the entity holds, walked by CDF's own capture traversal
//    (same storages, depth and item cap as a save), and only the difference is spawned. A
//    missing item goes back to the storage it was saved in (same container prefab and
//    storage index); equipment only there, cargo otherwise into any deposit storage with
//    room. An item whose prefab spawns with contents of its own (a loaded weapon, a filled
//    pouch) is not spawned: its saved contents are completed one by one instead.
//  - VERIFY_MS later every spawned item is checked: one outside the inventory is deleted.
//    A second round retries only as many items as failed that way, so the entity never
//    holds more of a prefab than it was saved with. What cannot be placed stays out and is
//    reported.
// Players are never touched. Bounded: at most SCANS_PER_PUMP inventory walks and
// SPAWNS_PER_PUMP spawns per pump, PUMP_MS apart; no per-frame script. Nothing new is
// saved: existing CDF saves load as they are. One summary per load:
// [EXPBG CDF INV] completed=N items on M entities; unplaced=K; checked=... cdfRefused=...
class EINV_Entry
{
 IEntity m_Entity;
 ref array<string> m_aPrefabs = {};
 ref array<int> m_aOwners = {};
 ref array<int> m_aStorages = {};
 ref array<int> m_aSlots = {};
 ref array<int> m_aAmmo = {};
 ref array<int> m_aStructural = {};
 ref array<string> m_aRefused = {};
 // Saved indexes to put back this round, ascending (a container before its contents).
 ref array<int> m_aQueue;
 ref array<IEntity> m_aSpawned = {};
 int m_iNext;
 int m_iRound;
 int m_iDue;
 int m_iInitial;
 bool m_bVerify;
}

class EINV_CDF
{
 static const int PEEL_LIMIT = 8;
 static const int SETTLE_MS = 2000;
 static const int VERIFY_MS = 1000;
 static const int PUMP_MS = 100;
 static const int SCANS_PER_PUMP = 4;
 static const int SPAWNS_PER_PUMP = 8;
 static const int MAX_ROUNDS = 2;
 static const int MAX_CANDIDATES = 6;
 static const int NAMES_LIMIT = 4;
 static const string REFUSAL_TRACE = "Objet refuse par l'inventaire : ";

 static ref array<ref EINV_Entry> s_aEntries;
 static ref array<string> s_aRefusals;
 static bool s_bCollecting;
 static bool s_bScheduled;
 static bool s_bActive;
 // Current pass (one CDF load, or one Track batch).
 static int s_iChecked, s_iCompleted, s_iEntities, s_iUnplaced, s_iRefused, s_iSkipped, s_iRetries, s_iWorkMs;
 static int s_iNamed;
 static string s_sNames;
 // Finished passes and the last one's totals (native fixture).
 static int s_iPasses, s_iLastCompleted, s_iLastUnplaced, s_iLastChecked, s_iLastRefused;

 //------------------------------------------------------------------------------------------------
 // CDF's refusal trace ("Objet refuse par l'inventaire : <prefab>") while one Apply runs.
 static void BeginRefusals()
 {
  if (!s_aRefusals) s_aRefusals = {};
  s_aRefusals.Clear();
  s_bCollecting = true;
 }
 static void EndRefusals(notnull array<string> names)
 {
  s_bCollecting = false;
  names.Clear();
  if (!s_aRefusals)
  {
   return;
  }
  foreach (string name : s_aRefusals) names.Insert(name);
  s_aRefusals.Clear();
 }
 static void NoteTrace(string message)
 {
  if (!s_bCollecting || !s_aRefusals || message.IndexOf(REFUSAL_TRACE) != 0)
  {
   return;
  }
  int length = REFUSAL_TRACE.Length();
  s_aRefusals.Insert(message.Substring(length, message.Length() - length));
 }

 //------------------------------------------------------------------------------------------------
 // The saved inventory lists of a CDF state, read under any EXPBG "cdfState" envelopes.
 // Older saves without slots, ammo or structural flags read like CDF reads them.
 static bool ReadSaved(string state, notnull array<string> prefabs, notnull array<int> owners, notnull array<int> storages, notnull array<int> slots, notnull array<int> ammo, notnull array<int> structural)
 {
  string text = state;
  for (int depth = 0; depth < PEEL_LIMIT; depth++)
  {
   if (!text.Contains("ivp"))
   {
    return false;
   }
   JsonLoadContext context = new JsonLoadContext();
   if (!context.LoadFromString(text))
   {
    return false;
   }
   prefabs.Clear();
   if (context.ReadValue("ivp", prefabs) && !prefabs.IsEmpty())
   {
    owners.Clear();
    storages.Clear();
    slots.Clear();
    ammo.Clear();
    structural.Clear();
    context.ReadValue("ivo", owners);
    context.ReadValue("ivs", storages);
    context.ReadValue("ivl", slots);
    context.ReadValue("iva", ammo);
    context.ReadValue("ivx", structural);
    int count = prefabs.Count();
    if (owners.Count() < count || storages.Count() < count)
    {
     return false;
    }
    while (slots.Count() < count) slots.Insert(-1);
    while (ammo.Count() < count) ammo.Insert(-1);
    while (structural.Count() < count) structural.Insert(0);
    return true;
   }
   string inner;
   if (!context.ReadValue("cdfState", inner))
   {
    return false;
   }
   text = inner;
  }
  return false;
 }

 //------------------------------------------------------------------------------------------------
 static InventoryStorageManagerComponent Manager(IEntity entity)
 {
  if (!entity)
  {
   return null;
  }
  return InventoryStorageManagerComponent.Cast(entity.FindComponent(InventoryStorageManagerComponent));
 }
 static bool IsPlayer(IEntity entity)
 {
  PlayerManager players = GetGame().GetPlayerManager();
  if (!players || !entity)
  {
   return false;
  }
  return players.GetPlayerIdFromControlledEntity(entity) != 0;
 }

 //------------------------------------------------------------------------------------------------
 // From the CDF_GMSaveState.Apply wrapper: CDF applied applied items and refused refusedCount
 // of them, naming the prefabs in refused.
 static void Register(IEntity entity, string state, int applied, int refusedCount, notnull array<string> refused)
 {
  if (!entity || IsPlayer(entity) || !Manager(entity))
  {
   return;
  }
  EINV_Entry entry = new EINV_Entry();
  bool read = ReadSaved(state, entry.m_aPrefabs, entry.m_aOwners, entry.m_aStorages, entry.m_aSlots, entry.m_aAmmo, entry.m_aStructural);
  if (!read || entry.m_aPrefabs.Count() != applied || refused.Count() != refusedCount)
  {
   BeginPass();
   s_iSkipped++;
   Schedule(SETTLE_MS);
   return;
  }
  foreach (string name : refused) entry.m_aRefused.Insert(name);
  Queue(entity, entry);
 }
 // Completes an entity against saved lists (CDF capture layout); refused items stay out.
 // Public for the native round-trip fixture; the load path goes through Register.
 static bool Track(IEntity entity, notnull array<string> prefabs, notnull array<int> owners, notnull array<int> storages, notnull array<int> slots, notnull array<int> ammo, notnull array<int> structural, notnull array<string> refused)
 {
  int count = prefabs.Count();
  if (!entity || IsPlayer(entity) || !Manager(entity) || count == 0 || owners.Count() != count || storages.Count() != count || slots.Count() != count || ammo.Count() != count || structural.Count() != count)
  {
   return false;
  }
  EINV_Entry entry = new EINV_Entry();
  for (int i = 0; i < count; i++)
  {
   entry.m_aPrefabs.Insert(prefabs[i]);
   entry.m_aOwners.Insert(owners[i]);
   entry.m_aStorages.Insert(storages[i]);
   entry.m_aSlots.Insert(slots[i]);
   entry.m_aAmmo.Insert(ammo[i]);
   entry.m_aStructural.Insert(structural[i]);
  }
  foreach (string name : refused) entry.m_aRefused.Insert(name);
  Queue(entity, entry);
  return true;
 }
 protected static void Queue(IEntity entity, EINV_Entry entry)
 {
  BeginPass();
  entry.m_Entity = entity;
  entry.m_iDue = System.GetTickCount() + SETTLE_MS;
  s_iRefused += entry.m_aRefused.Count();
  s_aEntries.Insert(entry);
  Schedule(SETTLE_MS);
 }
 static bool IsIdle()
 {
  return !s_bActive;
 }

 //------------------------------------------------------------------------------------------------
 protected static void BeginPass()
 {
  if (!s_aEntries) s_aEntries = {};
  if (s_bActive)
  {
   return;
  }
  s_bActive = true;
  s_iChecked = 0;
  s_iCompleted = 0;
  s_iEntities = 0;
  s_iUnplaced = 0;
  s_iRefused = 0;
  s_iSkipped = 0;
  s_iRetries = 0;
  s_iWorkMs = 0;
  s_iNamed = 0;
  s_sNames = "";
 }
 protected static void Schedule(int delay)
 {
  if (s_bScheduled || !GetGame())
  {
   return;
  }
  s_bScheduled = true;
  GetGame().GetCallqueue().CallLater(Pump, delay, false);
 }
 // A new CDF load replaced the scene: the previous pass (if any) ends here.
 static void BeginLoad()
 {
  if (s_bActive) Finish(true);
  if (GetGame()) GetGame().GetCallqueue().Remove(Pump);
  s_bScheduled = false;
 }

 //------------------------------------------------------------------------------------------------
 static void Pump()
 {
  s_bScheduled = false;
  if (!s_bActive || !s_aEntries)
  {
   return;
  }
  int start = System.GetTickCount();
  int scans = 0;
  int spawns = 0;
  int index = 0;
  while (index < s_aEntries.Count() && scans < SCANS_PER_PUMP && spawns < SPAWNS_PER_PUMP)
  {
   EINV_Entry entry = s_aEntries[index];
   if (entry.m_iDue - start > 0)
   {
    index++;
    continue;
   }
   if (Step(entry, scans, spawns))
   {
    s_aEntries.RemoveOrdered(index);
    continue;
   }
   index++;
  }
  s_iWorkMs += System.GetTickCount() - start;
  if (s_aEntries.IsEmpty())
  {
   Finish(false);
   return;
  }
  Schedule(PUMP_MS);
 }
 // One entity's next step; true when it is finished.
 protected static bool Step(EINV_Entry entry, inout int scans, inout int spawns)
 {
  IEntity entity = entry.m_Entity;
  InventoryStorageManagerComponent manager = Manager(entity);
  if (!manager || IsPlayer(entity))
  {
   DropSpawned(entry);
   s_iSkipped++;
   return true;
  }
  if (entry.m_bVerify)
  {
   entry.m_bVerify = false;
   entry.m_iRound++;
   int failed = DropSpawned(entry);
   scans++;
   array<int> missing = {};
   Plan(entry, entity, missing);
   // Retry only items that left the inventory or vanished after being placed: a missing
   // item that was never taken would be refused again, and one of ours still in a slot is
   // counted, so a retry can never exceed what went missing.
   if (missing.IsEmpty() || failed == 0 || entry.m_iRound >= MAX_ROUNDS)
   {
    Settle(entry, missing);
    return true;
   }
   array<int> retry = {};
   foreach (int savedIndex : missing)
   {
    if (retry.Count() < failed) retry.Insert(savedIndex);
   }
   s_iRetries += retry.Count();
   entry.m_aQueue = retry;
   entry.m_iNext = 0;
  }
  if (!entry.m_aQueue)
  {
   scans++;
   entry.m_aQueue = {};
   Plan(entry, entity, entry.m_aQueue);
   entry.m_iInitial = entry.m_aQueue.Count();
   s_iChecked++;
   if (entry.m_aQueue.IsEmpty())
   {
    return true;
   }
  }
  while (entry.m_iNext < entry.m_aQueue.Count() && spawns < SPAWNS_PER_PUMP)
  {
   Place(entry, entity, manager, entry.m_aQueue[entry.m_iNext]);
   entry.m_iNext++;
   spawns++;
  }
  if (entry.m_iNext < entry.m_aQueue.Count())
  {
   return false;
  }
  entry.m_bVerify = true;
  entry.m_iDue = System.GetTickCount() + VERIFY_MS;
  return false;
 }
 // Saved indexes still missing: saved multiset minus CDF's refusals minus what the entity
 // holds (CDF's capture traversal). For a prefab missing k times, its last k saved entries.
 protected static void Plan(EINV_Entry entry, IEntity entity, notnull array<int> missing)
 {
  missing.Clear();
  map<string, int> need = new map<string, int>();
  foreach (string savedPrefab : entry.m_aPrefabs)
  {
   int known = 0;
   need.Find(savedPrefab, known);
   need.Set(savedPrefab, known + 1);
  }
  foreach (string refusedPrefab : entry.m_aRefused) Take(need, refusedPrefab);
  array<string> held = {};
  CDF_GMSaveState.EINV_Live(entity, held);
  foreach (string heldPrefab : held) Take(need, heldPrefab);
  for (int i = entry.m_aPrefabs.Count() - 1; i >= 0; i--)
  {
   if (Take(need, entry.m_aPrefabs[i])) missing.Insert(i);
  }
  missing.Sort();
 }
 protected static bool Take(notnull map<string, int> counts, string prefab)
 {
  int count = 0;
  if (!counts.Find(prefab, count) || count <= 0)
  {
   return false;
  }
  counts.Set(prefab, count - 1);
  return true;
 }
 // Deletes spawned items left outside any inventory slot. Returns how many of this round's
 // items are not in a slot any more (deleted here or vanished).
 protected static int DropSpawned(EINV_Entry entry)
 {
  int failed = 0;
  foreach (IEntity spawned : entry.m_aSpawned)
  {
   if (!spawned)
   {
    failed++;
    continue;
   }
   InventoryItemComponent inventoryItem = InventoryItemComponent.Cast(spawned.FindComponent(InventoryItemComponent));
   if (inventoryItem && inventoryItem.GetParentSlot()) continue;
   failed++;
   SCR_EntityHelper.DeleteEntityAndChildren(spawned);
  }
  entry.m_aSpawned.Clear();
  return failed;
 }
 protected static void Settle(EINV_Entry entry, notnull array<int> missing)
 {
  int completed = entry.m_iInitial - missing.Count();
  if (completed > 0)
  {
   s_iCompleted += completed;
   s_iEntities++;
  }
  s_iUnplaced += missing.Count();
  foreach (int savedIndex : missing)
  {
   s_iNamed++;
   if (s_iNamed > NAMES_LIMIT) continue;
   string prefab = entry.m_aPrefabs[savedIndex];
   int slash = prefab.LastIndexOf("/");
   if (slash >= 0) prefab = prefab.Substring(slash + 1, prefab.Length() - slash - 1);
   if (!s_sNames.IsEmpty()) s_sNames += ",";
   s_sNames += prefab;
  }
 }

 //------------------------------------------------------------------------------------------------
 protected static void Place(EINV_Entry entry, IEntity entity, InventoryStorageManagerComponent manager, int index)
 {
  ResourceName prefab = entry.m_aPrefabs[index];
  Resource loaded = Resource.Load(prefab);
  if (!loaded || !loaded.IsValid())
  {
   return;
  }
  EntitySpawnParams spawn = new EntitySpawnParams();
  spawn.TransformMode = ETransformMode.WORLD;
  entity.GetWorldTransform(spawn.Transform);
  vector origin = spawn.Transform[3];
  origin[1] = origin[1] + 1;
  spawn.Transform[3] = origin;
  IEntity item = GetGame().SpawnEntityPrefab(loaded, GetGame().GetWorld(), spawn);
  if (!item)
  {
   return;
  }
  if (HoldsItems(item) || !Insert(entry, manager, index, item))
  {
   SCR_EntityHelper.DeleteEntityAndChildren(item);
   return;
  }
  int rounds = entry.m_aAmmo[index];
  BaseMagazineComponent magazine = BaseMagazineComponent.Cast(item.FindComponent(BaseMagazineComponent));
  if (magazine && rounds >= 0) magazine.SetAmmoCount(rounds);
  entry.m_aSpawned.Insert(item);
 }
 // A prefab that spawns with items of its own would add items the save does not list.
 protected static bool HoldsItems(IEntity item)
 {
  array<Managed> components = {};
  item.FindComponents(BaseInventoryStorageComponent, components);
  array<IEntity> contents = {};
  foreach (Managed component : components)
  {
   BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(component);
   if (storage && storage.GetAll(contents, false) > 0)
   {
    return true;
   }
  }
  return false;
 }
 // Its saved storage first (equipment only there, in its saved slot or that storage's next
 // free one); cargo then goes to any deposit storage with room.
 protected static bool Insert(EINV_Entry entry, InventoryStorageManagerComponent manager, int index, IEntity item)
 {
  array<BaseInventoryStorageComponent> candidates = {};
  Candidates(entry, manager, index, candidates);
  bool equipment = entry.m_aStructural[index] != 0;
  int slot = entry.m_aSlots[index];
  foreach (BaseInventoryStorageComponent candidate : candidates)
  {
   if (equipment && slot >= 0 && manager.TryInsertItemInStorage(item, candidate, slot))
   {
    return true;
   }
   if (manager.TryInsertItemInStorage(item, candidate, -1))
   {
    return true;
   }
  }
  if (equipment)
  {
   return false;
  }
  return manager.TryInsertItem(item, EStoragePurpose.PURPOSE_DEPOSIT);
 }
 // The live storages matching the saved place: storage index of the root storage list, or
 // of a carried container of the saved owner's prefab (CDF's ApplyInventory addressing).
 protected static void Candidates(EINV_Entry entry, InventoryStorageManagerComponent manager, int index, notnull array<BaseInventoryStorageComponent> candidates)
 {
  int storageIndex = entry.m_aStorages[index];
  int ownerIndex = entry.m_aOwners[index];
  if (storageIndex < 0 || ownerIndex >= entry.m_aPrefabs.Count())
  {
   return;
  }
  if (ownerIndex < 0)
  {
   array<BaseInventoryStorageComponent> roots = {};
   manager.GetStorages(roots, EStoragePurpose.PURPOSE_ANY);
   if (storageIndex < roots.Count() && roots[storageIndex]) candidates.Insert(roots[storageIndex]);
   return;
  }
  string container = entry.m_aPrefabs[ownerIndex];
  array<IEntity> carried = {};
  manager.GetItems(carried, EStoragePurpose.PURPOSE_ANY);
  set<IEntity> seen = new set<IEntity>();
  foreach (IEntity holder : carried)
  {
   if (!holder || seen.Contains(holder)) continue;
   seen.Insert(holder);
   if (CDF_GMSaveState.GetPrefabName(holder) != container) continue;
   array<Managed> components = {};
   holder.FindComponents(BaseInventoryStorageComponent, components);
   if (storageIndex >= components.Count()) continue;
   BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(components[storageIndex]);
   if (storage) candidates.Insert(storage);
   if (candidates.Count() >= MAX_CANDIDATES)
   {
    return;
   }
  }
 }

 //------------------------------------------------------------------------------------------------
 static void Finish(bool interrupted)
 {
  if (!s_bActive)
  {
   return;
  }
  s_bActive = false;
  int cut = 0;
  if (interrupted && s_aEntries)
  {
   cut = 1;
   foreach (EINV_Entry pending : s_aEntries)
   {
    DropSpawned(pending);
    s_iSkipped++;
   }
  }
  if (s_aEntries) s_aEntries.Clear();
  s_iPasses++;
  s_iLastCompleted = s_iCompleted;
  s_iLastUnplaced = s_iUnplaced;
  s_iLastChecked = s_iChecked;
  s_iLastRefused = s_iRefused;
  if (s_iChecked == 0 && s_iSkipped == 0)
  {
   return;
  }
  string names;
  if (!s_sNames.IsEmpty()) names = " unplacedItems=" + s_sNames;
  if (s_iNamed > NAMES_LIMIT) names += string.Format("(+%1)", s_iNamed - NAMES_LIMIT);
  string details = string.Format("checked=%1 cdfRefused=%2 skipped=%3 retried=%4 workMs=%5 interrupted=%6", s_iChecked, s_iRefused, s_iSkipped, s_iRetries, s_iWorkMs, cut);
  LogLevel severity = LogLevel.NORMAL;
  if (s_iUnplaced > 0 || cut > 0) severity = LogLevel.WARNING;
  Print(string.Format("[EXPBG CDF INV] completed=%1 items on %2 entities; unplaced=%3; %4%5", s_iCompleted, s_iEntities, s_iUnplaced, details, names), severity);
 }
}

// CDF names each item its inventory refused in this trace (printed only with "verbose").
modded class CDF_GMSave
{
 override static void LogDebug(string message)
 {
  EINV_CDF.NoteTrace(message);
  super.LogDebug(message);
 }
}

modded class CDF_GMSaveState
{
 // CDF counts every saved item it handles once (kept, placed, re-placed by the engine or
 // refused). An entity counts as applied when those counters moved by its saved item count.
 override static void Apply(IEntity entity, string state)
 {
  if (!entity || !state.Contains("ivp"))
  {
   super.Apply(entity, state);
   return;
  }
  int handled = s_iItemsKept + s_iItemsPlaced + s_iItemsFallback + s_iItemsRefused;
  int refusedBefore = s_iItemsRefused;
  EINV_CDF.BeginRefusals();
  super.Apply(entity, state);
  array<string> refused = {};
  EINV_CDF.EndRefusals(refused);
  int applied = s_iItemsKept + s_iItemsPlaced + s_iItemsFallback + s_iItemsRefused - handled;
  if (applied <= 0)
  {
   return;
  }
  EINV_CDF.Register(entity, state, applied, s_iItemsRefused - refusedBefore, refused);
 }
 // What the entity holds, walked like a CDF save (same storages, order, depth, item cap).
 static void EINV_Live(IEntity entity, notnull array<string> prefabs)
 {
  prefabs.Clear();
  if (!entity)
  {
   return;
  }
  array<int> owners = {};
  array<int> storages = {};
  array<int> slots = {};
  array<int> ammo = {};
  array<int> structural = {};
  CaptureInventory(entity, prefabs, owners, storages, slots, ammo, structural);
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  bool result = super.Restore(document);
  if (result) EINV_CDF.BeginLoad();
  return result;
 }
}
