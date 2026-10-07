// Optional CDF GameMaster Save 1.4.1 hooks; upstream state is kept opaque.
class EII_CDFItem
{
 string prefab;
 string title;
 string content;
 bool spent;
 bool diagnostics;
 void Capture(EII_IntelComponent intel)
 {
  prefab = intel.GetOwner().GetPrefabData().GetPrefabName();
  title = intel.GetTitle();
  content = intel.GetContent();
  spent = intel.HasStarted();
  diagnostics = intel.GetDebug();
  intel.Trace("CDF captured");
 }
 // Prefilter for the Intel, rack and drive whitelists (0.1.8): every whitelisted path contains
 // "/EXPII/" and every GUID form is 18 characters, so false only where no whitelist can match.
 static bool EII_MaybeExpii(string resource)
 {
  return resource.Length() == 18 || resource.Contains("/EXPII/");
 }
 static string GetIntelGuid(string resource)
 {
  if (!EII_MaybeExpii(resource))
   return string.Empty;
  array<string> prefabs = {
   "{D3DCA7AB761413C6}PrefabsEditable/EXPII/EII_ManualUS.et",
   "{7E8D916D8F449094}PrefabsEditable/EXPII/EII_ManualSoviet.et",
   "{23BD5CC4A9B2CC83}PrefabsEditable/EXPII/EII_NotebookBlue.et",
   "{4940337955AE3340}PrefabsEditable/EXPII/EII_NotebookBlack.et",
   "{9AA03E7756B80823}PrefabsEditable/EXPII/EII_NotebookOrange.et",
   "{46DA23F715D50DAB}PrefabsEditable/EXPII/EII_ArmoredLaptop.et",
   "{1B3F61C4291552DE}PrefabsEditable/EXPII/EII_Tablet2.et"
  };
  foreach (string known : prefabs)
  {
   string guid = known.Substring(0, 18);
   if (resource == known || resource == guid) return guid;
  }
  return string.Empty;
 }
 static bool IsIntelPrefab(string resource)
 {
  return !GetIntelGuid(resource).IsEmpty();
 }
 static bool IsCarriedIntel(SCR_EditableEntityComponent editable)
 {
  if (!editable || !editable.GetOwner() || !editable.GetOwner().FindComponent(EII_IntelComponent)) return false;
  InventoryItemComponent item = InventoryItemComponent.Cast(editable.GetOwner().FindComponent(InventoryItemComponent));
  return item && item.GetParentSlot();
 }
 bool Valid()
 {
  if (!IsIntelPrefab(prefab) || !EII_IntelComponent.ValidText(title, content)) return false;
  return EII_CDFLoad.PrefabValid(prefab);
 }
}

class EII_CDFPayload
{
 static const int INVENTORY_LIMIT = 400;
 int version = 1;
 ref EII_CDFItem item;
 ref array<ref EII_CDFItem> inventory = {};
 // One carried Intel item, in GetItems order; Capture dedupes entities (0.1.8: one pass with
 // carried drives). Reject, never truncate: do not allocate or serialize an oversized payload.
 bool AddCarried(EII_IntelComponent intel)
 {
  if (inventory.Count() >= INVENTORY_LIMIT)
   return false;
  EII_CDFItem record = new EII_CDFItem();
  record.Capture(intel);
  inventory.Insert(record);
  return true;
 }
 bool Valid()
 {
  if (version != 1 || !inventory || inventory.Count() > INVENTORY_LIMIT || (item && !item.Valid())) return false;
  foreach (EII_CDFItem entry : inventory)
   if (!entry || !entry.Valid()) return false;
  return item || !inventory.IsEmpty();
 }
 static bool Read(string state, out EII_CDFPayload payload, out string original)
 {
  JsonLoadContext context = new JsonLoadContext();
  return context.LoadFromString(state) && context.ReadValue("eiiIntel", payload) && payload && payload.Valid() && context.ReadValue("cdfState", original);
 }
 static bool ValidDocument(CDF_GMSaveDocument document, bool keep = false)
 {
  if (!document || !document.m_aEntities) return false;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record) return false;
   // Server rack / USB drive envelope first (outermost); without one, state is unchanged.
   string state;
   if (!EII_CDFRackBridge.ValidRecord(record, keep, state)) return false;
   string guid = EII_CDFItem.GetIntelGuid(record.m_sPrefab);
   if (!state.Contains("\"eiiIntel\""))
   {
    if (!guid.IsEmpty()) return false;
    continue;
   }
   EII_CDFPayload payload;
   string original;
   if (!Read(state, payload, original)) return false;
   if (!guid.IsEmpty() && !payload.item) return false;
   if (payload.item && EII_CDFItem.GetIntelGuid(payload.item.prefab) != guid) return false;
   if (keep) EII_CDFLoad.Keep(state, payload, original);
  }
  return true;
 }
}

// Server racks and USB drives (EXPBG GM Tools 0.1.5). Their own prefab whitelist; Intel's
// seven-prefab whitelist is unchanged. Rack and drive state travels in an outer "eirIntel"
// envelope around the Intel/CDF state, so records without one load exactly as before.
class EII_CDFRack
{
 string prefab;
 string title;
 string content;
 int seconds;
 bool diagnostics;
 void Capture(string resource, EIR_RackComponent rack)
 {
  prefab = resource;
  title = rack.GetTitle();
  content = rack.GetContent();
  seconds = rack.GetSeconds();
  diagnostics = rack.GetDebug();
  rack.Trace("CDF captured");
 }
 // Seconds are clamped by the rack on restore, so a changed range never refuses a load.
 bool Valid()
 {
  if (EII_CDFRackBridge.GetRackGuid(prefab).IsEmpty() || !EIR_RackComponent.ValidText(title, content)) return false;
  return EII_CDFLoad.PrefabValid(prefab);
 }
}

class EII_CDFDrive
{
 string prefab;
 string title;
 string content;
 void Capture(string resource, EIR_DriveComponent drive)
 {
  prefab = resource;
  title = drive.GetTitle();
  content = drive.GetContent();
 }
 bool Valid()
 {
  if (EII_CDFRackBridge.GetDriveGuid(prefab).IsEmpty() || !EIR_RackComponent.ValidText(title, content)) return false;
  return EII_CDFLoad.PrefabValid(prefab);
 }
}

class EII_CDFRackPayload
{
 static const int INVENTORY_LIMIT = 400;
 int version = 1;
 ref EII_CDFRack rack;
 ref EII_CDFDrive drive;
 ref array<ref EII_CDFDrive> drives = {};
 // The captured entity itself: a whitelisted rack, or a whitelisted drive outside any inventory.
 // Capture resolves both components and allocates this payload only for a rack or a drive (0.1.8).
 void CaptureEntity(IEntity entity, EIR_RackComponent rackComponent, EIR_DriveComponent driveComponent)
 {
  // Only racks and drives pay for the prefab lookup; every other record skips it.
  if (!rackComponent && !driveComponent) return;
  string resource = EII_CDFRackBridge.PrefabOf(entity);
  if (rackComponent && !EII_CDFRackBridge.GetRackGuid(resource).IsEmpty())
  {
   rack = new EII_CDFRack();
   rack.Capture(resource, rackComponent);
  }
  if (driveComponent && !EII_CDFRackBridge.GetDriveGuid(resource).IsEmpty())
  {
   drive = new EII_CDFDrive();
   drive.Capture(resource, driveComponent);
  }
 }
 // One carried, whitelisted drive holding intel, in GetItems order; Capture checks the rest
 // (CDF itself restores empty drives as ordinary cargo). Reject, never truncate, like Intel.
 bool AddCarried(string resource, EIR_DriveComponent component)
 {
  if (drives.Count() >= INVENTORY_LIMIT)
   return false;
  EII_CDFDrive record = new EII_CDFDrive();
  record.Capture(resource, component);
  drives.Insert(record);
  return true;
 }
 bool Valid()
 {
  if (version != 1 || !drives || drives.Count() > INVENTORY_LIMIT || (rack && drive)) return false;
  if ((rack && !rack.Valid()) || (drive && !drive.Valid())) return false;
  foreach (EII_CDFDrive entry : drives)
   if (!entry || !entry.Valid()) return false;
  return rack || drive || !drives.IsEmpty();
 }
 static bool Read(string state, out EII_CDFRackPayload payload, out string original)
 {
  JsonLoadContext context = new JsonLoadContext();
  return context.LoadFromString(state) && context.ReadValue("eirIntel", payload) && payload && payload.Valid() && context.ReadValue("cdfState", original);
 }
 // Outermost envelope; an entity with no rack or drive state keeps its state byte-for-byte.
 static string Wrap(EII_CDFRackPayload payload, string inner)
 {
  if (!payload || (!payload.rack && !payload.drive && payload.drives.IsEmpty())) return inner;
  JsonSaveContext context = new JsonSaveContext();
  // Keep the marker on encoding failure so document validation rejects the save.
  if (!context.WriteValue("eirIntel", payload) || !context.WriteValue("cdfState", inner))
  {
   EII_CDFLoad.Refuse("server rack or USB drive state could not be encoded");
   return "{\"eirIntel\":{\"version\":0}}";
  }
  string encoded = context.SaveToString();
  if (!encoded.IsEmpty()) return encoded;
  EII_CDFLoad.Refuse("server rack or USB drive state could not be encoded");
  return "{\"eirIntel\":{\"version\":0}}";
 }
}

// A rack/drive payload validated by Restore, reused by Apply for the same state text.
class EII_CDFRackParsed
{
 ref EII_CDFRackPayload Payload;
 string Original;
}

// Rack/drive prefab whitelist, carried-drive detection and document validation.
class EII_CDFRackBridge
{
 static string MatchGuid(string resource, array<string> prefabs)
 {
  foreach (string known : prefabs)
  {
   string guid = known.Substring(0, 18);
   if (resource == known || resource == guid) return guid;
  }
  return string.Empty;
 }
 static string GetRackGuid(string resource)
 {
  if (!EII_CDFItem.EII_MaybeExpii(resource))
   return string.Empty;
  array<string> prefabs = {
   "{67558101DE7E37AA}Prefabs/EXPII/EIR_ServerRack_Base.et",
   "{AF2266B64D5D4750}PrefabsEditable/EXPII/EIR_ServerRackA.et",
   "{43C0A8B7CC36A2D1}PrefabsEditable/EXPII/EIR_ServerRackB.et"
  };
  return MatchGuid(resource, prefabs);
 }
 static string GetDriveGuid(string resource)
 {
  if (!EII_CDFItem.EII_MaybeExpii(resource))
   return string.Empty;
  array<string> prefabs = {
   "{74CA7EB748CF82EC}PrefabsEditable/EXPII/EIR_USBDrive.et"
  };
  return MatchGuid(resource, prefabs);
 }
 // The prefab CDF records (a modified world instance resolves to its prefab ancestor), so a
 // Workbench-placed rack gets the same envelope as its record's prefab.
 static string PrefabOf(IEntity entity)
 {
  if (!entity) return string.Empty;
  return CDF_GMSaveState.GetPrefabName(entity);
 }
 // A whitelisted drive in any inventory: its carrier's payload owns it, as for carried Intel.
 static bool IsCarriedDrive(SCR_EditableEntityComponent editable)
 {
  if (!editable || !editable.GetOwner() || !editable.GetOwner().FindComponent(EIR_DriveComponent)) return false;
  if (GetDriveGuid(PrefabOf(editable.GetOwner())).IsEmpty()) return false;
  InventoryItemComponent item = InventoryItemComponent.Cast(editable.GetOwner().FindComponent(InventoryItemComponent));
  return item && item.GetParentSlot();
 }
 // Validates and unwraps one record's envelope into state. Rack and drive records without
 // an envelope come from saves made before this support: accepted, counted, left as they were.
 static bool ValidRecord(CDF_GMSaveEntityRecord record, bool keep, out string state)
 {
  state = record.m_sState;
  bool rackRecord = !GetRackGuid(record.m_sPrefab).IsEmpty();
  bool driveRecord = !GetDriveGuid(record.m_sPrefab).IsEmpty();
  if (!state.Contains("\"eirIntel\""))
  {
   if (keep && (rackRecord || driveRecord)) EII_CDFLoad.StorageOlder++;
   return true;
  }
  EII_CDFRackPayload payload;
  string original;
  if (!EII_CDFRackPayload.Read(state, payload, original))
   return EII_CDFLoad.Refuse("invalid server rack or USB drive state");
  bool hasRack = payload.rack != null;
  bool hasDrive = payload.drive != null;
  if (hasRack != rackRecord || hasDrive != driveRecord)
   return EII_CDFLoad.Refuse("server rack or USB drive state does not match its prefab");
  if (hasRack && GetRackGuid(payload.rack.prefab) != GetRackGuid(record.m_sPrefab))
   return EII_CDFLoad.Refuse("server rack or USB drive state does not match its prefab");
  if (hasDrive && GetDriveGuid(payload.drive.prefab) != GetDriveGuid(record.m_sPrefab))
   return EII_CDFLoad.Refuse("server rack or USB drive state does not match its prefab");
  if (keep) EII_CDFLoad.KeepStorage(record.m_sState, payload, original);
  state = original;
  return true;
 }
 // Whitelisted drives by GUID in inventory order, each entity once; skips entities in exclude.
 static void GroupDrives(array<IEntity> items, set<IEntity> exclude, map<string, ref array<IEntity>> groups)
 {
  set<IEntity> seen = new set<IEntity>();
  foreach (IEntity item : items)
  {
   if (!item || seen.Contains(item) || (exclude && exclude.Contains(item))) continue;
   seen.Insert(item);
   if (!item.FindComponent(EIR_DriveComponent)) continue;
   string guid = GetDriveGuid(PrefabOf(item));
   if (guid.IsEmpty()) continue;
   array<IEntity> bucket = groups.Get(guid);
   if (!bucket)
   {
    bucket = {};
    groups.Insert(guid, bucket);
   }
   bucket.Insert(item);
  }
 }
}

// A payload validated by Restore, reused by Apply for the same state text.
class EII_CDFParsed
{
 ref EII_CDFPayload Payload;
 string Original;
}

// Per-load restore helpers: validation reuse, bounded carried-Intel matching and
// [CDF TIMING] counters. Kept apart from the serialized payload classes.
class EII_CDFLoad
{
 static ref set<string> ValidPrefabs = new set<string>();
 static ref map<string, ref EII_CDFParsed> Validated = new map<string, ref EII_CDFParsed>();
 static int Payloads, FinishAttempts;
 static int ApplyCalls, ApplyPayload, ApplyOwnMs, ApplyInnerMs, ApplySlowestMs;
 // Server racks and USB drives: validated envelopes, per-load counts and the first refusal.
 static ref map<string, ref EII_CDFRackParsed> ValidatedStorage = new map<string, ref EII_CDFRackParsed>();
 static int StoragePayloads, StorageOlder, StorageRacks, StorageDrives, StorageCarried, StorageFailed;
 static string StorageRefusal;
 static void Reset()
 {
  Validated.Clear();
  Payloads = 0;
  FinishAttempts = 0;
  ApplyCalls = 0;
  ApplyPayload = 0;
  ApplyOwnMs = 0;
  ApplyInnerMs = 0;
  ApplySlowestMs = 0;
  ValidatedStorage.Clear();
  StoragePayloads = 0;
  StorageOlder = 0;
  StorageRacks = 0;
  StorageDrives = 0;
  StorageCarried = 0;
  StorageFailed = 0;
  StorageRefusal = string.Empty;
 }
 static void Forget()
 {
  Validated.Clear();
  ValidatedStorage.Clear();
 }
 static bool Refuse(string reason)
 {
  if (StorageRefusal.IsEmpty()) StorageRefusal = reason;
  return false;
 }
 static void KeepStorage(string state, EII_CDFRackPayload payload, string original)
 {
  EII_CDFRackParsed parsed = new EII_CDFRackParsed();
  parsed.Payload = payload;
  parsed.Original = original;
  ValidatedStorage.Set(state, parsed);
 }
 // Fail closed: an envelope Restore did not validate is parsed and validated again.
 static bool ReadStorage(string state, out EII_CDFRackPayload payload, out string original)
 {
  EII_CDFRackParsed parsed;
  if (ValidatedStorage.Find(state, parsed) && parsed && parsed.Payload)
  {
   payload = parsed.Payload;
   original = parsed.Original;
   return true;
  }
  return EII_CDFRackPayload.Read(state, payload, original);
 }
 static bool PrefabValid(string prefab)
 {
  if (ValidPrefabs.Contains(prefab)) return true;
  Resource resource = Resource.Load(prefab);
  if (!resource || !resource.IsValid()) return false;
  ValidPrefabs.Insert(prefab);
  return true;
 }
 static void Keep(string state, EII_CDFPayload payload, string original)
 {
  EII_CDFParsed parsed = new EII_CDFParsed();
  parsed.Payload = payload;
  parsed.Original = original;
  Validated.Set(state, parsed);
  Payloads++;
 }
 // Fail closed: a state Restore did not validate is parsed and validated again.
 static bool ReadValidated(string state, out EII_CDFPayload payload, out string original)
 {
  EII_CDFParsed parsed;
  if (Validated.Find(state, parsed) && parsed && parsed.Payload)
  {
   payload = parsed.Payload;
   original = parsed.Original;
   return true;
  }
  return EII_CDFPayload.Read(state, payload, original);
 }
 // Intel instances by GUID in inventory order, each entity once; skips entities in exclude.
 static void GroupIntel(array<IEntity> items, set<IEntity> exclude, map<string, ref array<IEntity>> groups)
 {
  set<IEntity> seen = new set<IEntity>();
  foreach (IEntity item : items)
  {
   if (!item || seen.Contains(item) || (exclude && exclude.Contains(item))) continue;
   seen.Insert(item);
   if (!item.FindComponent(EII_IntelComponent) || !item.GetPrefabData()) continue;
   string guid = EII_CDFItem.GetIntelGuid(item.GetPrefabData().GetPrefabName());
   if (guid.IsEmpty()) continue;
   array<IEntity> bucket = groups.Get(guid);
   if (!bucket)
   {
    bucket = {};
    groups.Insert(guid, bucket);
   }
   bucket.Insert(item);
  }
 }
 // Next unassigned instance of this GUID, or null.
 static IEntity Take(map<string, ref array<IEntity>> groups, map<string, int> taken, string guid)
 {
  array<IEntity> bucket = groups.Get(guid);
  int next = taken.Get(guid);
  if (!bucket || next >= bucket.Count()) return null;
  taken.Set(guid, next + 1);
  return bucket[next];
 }
 // [CDF TIMING]: own time excludes super (the rest of the adapter chain and CDF itself).
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
  if (!entity) return original;
  // 0.1.8: nothing is allocated until an Intel item, rack or drive is found (most records
  // hold none); Wrap(null, inner) returns inner, so such records keep their state as before.
  EII_CDFPayload payload;
  EII_CDFRackPayload storage;
  EIR_RackComponent rackComponent = EIR_RackComponent.Cast(entity.FindComponent(EIR_RackComponent));
  EIR_DriveComponent driveComponent = EIR_DriveComponent.Cast(entity.FindComponent(EIR_DriveComponent));
  if (rackComponent || driveComponent)
  {
   storage = new EII_CDFRackPayload();
   storage.CaptureEntity(entity, rackComponent, driveComponent);
  }
  EII_IntelComponent intel = EII_IntelComponent.Cast(entity.FindComponent(EII_IntelComponent));
  if (intel)
  {
   payload = new EII_CDFPayload();
   payload.item = new EII_CDFItem();
   payload.item.Capture(intel);
  }
  CDF_GMSaveConfig config = CDF_GMSaveConfig.GetInstance();
  InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(entity.FindComponent(InventoryStorageManagerComponent));
  if (manager && config.m_bSaveInventories && CanCaptureInventory(entity, config))
  {
   array<IEntity> items = {};
   manager.GetItems(items, EStoragePurpose.PURPOSE_ANY);
   // One pass for carried Intel and carried drives, in GetItems order (restore matching
   // assigns same-GUID instances in that order). Too much Intel refuses at once, as when
   // the drive pass never ran after it; too many drives is refused after the pass, so too
   // much Intel anywhere in the inventory still takes precedence.
   set<IEntity> intelSeen;
   set<IEntity> drivesSeen;
   bool tooManyDrives = false;
   foreach (IEntity carried : items)
   {
    if (!carried) continue;
    EII_IntelComponent carriedIntel = EII_IntelComponent.Cast(carried.FindComponent(EII_IntelComponent));
    if (carriedIntel && (!intelSeen || !intelSeen.Contains(carried)))
    {
     if (!payload) payload = new EII_CDFPayload();
     if (!payload.AddCarried(carriedIntel))
      return "{\"eiiIntel\":{\"version\":0}}";
     if (!intelSeen) intelSeen = new set<IEntity>();
     intelSeen.Insert(carried);
    }
    if (tooManyDrives) continue;
    EIR_DriveComponent carriedDrive = EIR_DriveComponent.Cast(carried.FindComponent(EIR_DriveComponent));
    if (!carriedDrive || carriedDrive.IsEmpty() || (drivesSeen && drivesSeen.Contains(carried))) continue;
    string resource = EII_CDFRackBridge.PrefabOf(carried);
    if (EII_CDFRackBridge.GetDriveGuid(resource).IsEmpty()) continue;
    if (!storage) storage = new EII_CDFRackPayload();
    if (!storage.AddCarried(resource, carriedDrive))
    {
     tooManyDrives = true;
     continue;
    }
    if (!drivesSeen) drivesSeen = new set<IEntity>();
    drivesSeen.Insert(carried);
   }
   if (tooManyDrives)
   {
    EII_CDFLoad.Refuse("more than 400 USB drives holding intel in one inventory");
    return "{\"eirIntel\":{\"version\":0}}";
   }
  }
  if (!payload || (!payload.item && payload.inventory.IsEmpty()))
   return EII_CDFRackPayload.Wrap(storage, original);
  JsonSaveContext context = new JsonSaveContext();
  // Keep the marker on encoding failure so document validation also rejects carriers.
  if (!context.WriteValue("eiiIntel", payload) || !context.WriteValue("cdfState", original))
   return "{\"eiiIntel\":{\"version\":0}}";
  string encoded = context.SaveToString();
  if (encoded.IsEmpty()) return "{\"eiiIntel\":{\"version\":0}}";
  return EII_CDFRackPayload.Wrap(storage, encoded);
 }
 override static void Apply(IEntity entity, string state)
 {
  int timing = System.GetTickCount();
  // A rack/drive envelope is outermost: unwrap it, restore it after the inner state.
  EII_CDFRackPayload storage;
  string current = state;
  if (entity && state.Contains("\"eirIntel\"") && !EII_CDFLoad.ReadStorage(state, storage, current))
  {
   EII_CDFLoad.StorageFailed++;
   Print("[EII CDF] Invalid server rack or USB drive payload rejected", LogLevel.ERROR);
   EII_CDFLoad.ApplyTiming(true, timing, 0, 0);
   return;
  }
  if (!entity || !current.Contains("\"eiiIntel\""))
  {
   int passInner = System.GetTickCount();
   super.Apply(entity, current);
   int passEnd = System.GetTickCount();
   EII_RestoreStorage(entity, storage);
   EII_CDFLoad.ApplyTiming(storage != null, timing, passInner, passEnd);
   return;
  }
  EII_CDFPayload payload;
  string original;
  if (!EII_CDFLoad.ReadValidated(current, payload, original))
  {
   Print("[EII CDF] Invalid payload rejected", LogLevel.ERROR);
   EII_CDFLoad.ApplyTiming(true, timing, 0, 0);
   return;
  }
  EII_IntelComponent.RestoreDepth++;
  int inner = System.GetTickCount();
  super.Apply(entity, original);
  int innerEnd = System.GetTickCount();
  if (payload.item)
  {
   EII_IntelComponent intel = EII_IntelComponent.Cast(entity.FindComponent(EII_IntelComponent));
   if (!intel || !intel.RestoreState(payload.item.title, payload.item.content, payload.item.spent))
    Print("[EII CDF] World item restore failed", LogLevel.ERROR);
   else intel.SetDebug(payload.item.diagnostics);
  }
  CDF_GMSaveConfig config = CDF_GMSaveConfig.GetInstance();
  if (!payload.inventory.IsEmpty())
  {
   if (config.m_bSaveInventories && CanCaptureInventory(entity, config)) EII_RestoreInventory(entity, payload.inventory);
   else Print("[EII CDF] Inventory restoration disabled in CDF configuration", LogLevel.ERROR);
  }
  EII_IntelComponent.RestoreDepth--;
  EII_RestoreStorage(entity, storage);
  EII_CDFLoad.ApplyTiming(true, timing, inner, innerEnd);
 }
 // Server racks and USB drives, after CDF (and Intel) restored the entity and its inventory.
 protected static void EII_RestoreStorage(IEntity entity, EII_CDFRackPayload storage)
 {
  if (!entity || !storage) return;
  EII_CDFLoad.StoragePayloads++;
  if (storage.rack)
  {
   EIR_RackComponent rack = EIR_RackComponent.Cast(entity.FindComponent(EIR_RackComponent));
   // Diagnostics first, so the rack's own trace records the restore when they are on.
   if (rack) rack.SetDebug(storage.rack.diagnostics);
   if (!rack || !rack.RestoreState(storage.rack.title, storage.rack.content, storage.rack.seconds))
   {
    EII_CDFLoad.StorageFailed++;
    Print("[EII CDF] Server rack restore failed", LogLevel.ERROR);
   }
   else EII_CDFLoad.StorageRacks++;
  }
  if (storage.drive)
  {
   EIR_DriveComponent drive = EIR_DriveComponent.Cast(entity.FindComponent(EIR_DriveComponent));
   if (!drive || !drive.Store(storage.drive.title, storage.drive.content))
   {
    EII_CDFLoad.StorageFailed++;
    Print("[EII CDF] USB drive restore failed", LogLevel.ERROR);
   }
   else EII_CDFLoad.StorageDrives++;
  }
  if (storage.drives.IsEmpty()) return;
  CDF_GMSaveConfig config = CDF_GMSaveConfig.GetInstance();
  if (config.m_bSaveInventories && CanCaptureInventory(entity, config)) EII_RestoreDrives(entity, storage.drives);
  else
  {
   EII_CDFLoad.StorageFailed += storage.drives.Count();
   Print("[EII CDF] USB drive inventory restoration disabled in CDF configuration", LogLevel.ERROR);
  }
 }
 // Same bounded matching as carried Intel: CDF restored the drives as empty cargo; each record
 // takes a distinct instance, only drives CDF omitted are spawned, then one after-scan.
 protected static void EII_RestoreDrives(IEntity entity, array<ref EII_CDFDrive> records)
 {
  InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(entity.FindComponent(InventoryStorageManagerComponent));
  if (!manager)
  {
   EII_CDFLoad.StorageFailed += records.Count();
   Print("[EII CDF] Saved USB drives require an inventory manager", LogLevel.ERROR);
   return;
  }
  array<IEntity> items = {};
  manager.GetItems(items, EStoragePurpose.PURPOSE_ANY);
  map<string, ref array<IEntity>> restored = new map<string, ref array<IEntity>>();
  EII_CDFRackBridge.GroupDrives(items, null, restored);
  map<string, int> taken = new map<string, int>();
  array<IEntity> created = {};
  array<bool> rejected = {};
  bool spawned = false;
  foreach (EII_CDFDrive record : records)
  {
   IEntity match = EII_CDFLoad.Take(restored, taken, EII_CDFRackBridge.GetDriveGuid(record.prefab));
   created.Insert(match);
   rejected.Insert(false);
   if (match) continue;
   if (!manager.TrySpawnPrefabToStorage(record.prefab, null, -1, EStoragePurpose.PURPOSE_ANY))
   {
    Print("[EII CDF] Cannot restore a carried USB drive: inventory rejected item", LogLevel.ERROR);
    rejected[rejected.Count() - 1] = true;
    continue;
   }
   spawned = true;
  }
  if (spawned)
  {
   array<IEntity> after = {};
   manager.GetItems(after, EStoragePurpose.PURPOSE_ANY);
   set<IEntity> known = new set<IEntity>();
   foreach (IEntity item : items) known.Insert(item);
   map<string, ref array<IEntity>> fresh = new map<string, ref array<IEntity>>();
   EII_CDFRackBridge.GroupDrives(after, known, fresh);
   map<string, int> freshTaken = new map<string, int>();
   for (int i = 0; i < records.Count(); i++)
   {
    if (!created[i] && !rejected[i]) created[i] = EII_CDFLoad.Take(fresh, freshTaken, EII_CDFRackBridge.GetDriveGuid(records[i].prefab));
   }
  }
  for (int r = 0; r < records.Count(); r++)
  {
   if (rejected[r])
   {
    EII_CDFLoad.StorageFailed++;
    continue;
   }
   EII_CDFDrive entry = records[r];
   EIR_DriveComponent drive = null;
   if (created[r]) drive = EIR_DriveComponent.Cast(created[r].FindComponent(EIR_DriveComponent));
   if (!drive || !drive.Store(entry.title, entry.content))
   {
    EII_CDFLoad.StorageFailed++;
    Print("[EII CDF] Restored USB drive could not receive intel", LogLevel.ERROR);
   }
   else EII_CDFLoad.StorageCarried++;
  }
 }
 protected static void EII_RestoreInventory(IEntity entity, array<ref EII_CDFItem> records)
 {
  InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(entity.FindComponent(InventoryStorageManagerComponent));
  if (!manager)
  {
   Print("[EII CDF] Saved intel requires an inventory manager", LogLevel.ERROR);
   return;
  }
  // Internal static calls can bypass modded overrides. Let CDF restore its
  // original inventory, then assign each payload to a distinct actual instance.
  // Bounded per carrier: one inventory snapshot, restored Intel grouped by GUID once,
  // and every missing item spawned before one after-scan (no rescan per record).
  array<IEntity> items = {};
  manager.GetItems(items, EStoragePurpose.PURPOSE_ANY);
  map<string, ref array<IEntity>> restored = new map<string, ref array<IEntity>>();
  EII_CDFLoad.GroupIntel(items, null, restored);
  map<string, int> taken = new map<string, int>();
  array<IEntity> created = {};
  array<bool> rejected = {};
  bool spawned = false;
  foreach (EII_CDFItem record : records)
  {
   IEntity match = EII_CDFLoad.Take(restored, taken, EII_CDFItem.GetIntelGuid(record.prefab));
   created.Insert(match);
   rejected.Insert(false);
   if (match) continue;
   // CDF can omit deep/overflow items. Only insert when no matching instance exists.
   if (!manager.TrySpawnPrefabToStorage(record.prefab, null, -1, EStoragePurpose.PURPOSE_ANY))
   {
    Print("[EII CDF] Cannot restore carried intel: inventory rejected item", LogLevel.ERROR);
    rejected[rejected.Count() - 1] = true;
    continue;
   }
   spawned = true;
  }
  if (spawned)
  {
   array<IEntity> after = {};
   manager.GetItems(after, EStoragePurpose.PURPOSE_ANY);
   set<IEntity> known = new set<IEntity>();
   foreach (IEntity item : items) known.Insert(item);
   map<string, ref array<IEntity>> fresh = new map<string, ref array<IEntity>>();
   EII_CDFLoad.GroupIntel(after, known, fresh);
   map<string, int> freshTaken = new map<string, int>();
   for (int i = 0; i < records.Count(); i++)
   {
    if (!created[i] && !rejected[i]) created[i] = EII_CDFLoad.Take(fresh, freshTaken, EII_CDFItem.GetIntelGuid(records[i].prefab));
   }
  }
  for (int r = 0; r < records.Count(); r++)
  {
   if (rejected[r]) continue;
   EII_CDFItem entry = records[r];
   EII_IntelComponent intel = null;
   if (created[r]) intel = EII_IntelComponent.Cast(created[r].FindComponent(EII_IntelComponent));
   if (!intel || !intel.RestoreState(entry.title, entry.content, entry.spent))
    Print("[EII CDF] Restored inventory item could not receive intel", LogLevel.ERROR);
   else intel.SetDebug(entry.diagnostics);
  }
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  EII_CDFLoad.StorageRefusal = string.Empty;
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  // An upstream adapter can refuse capture while recovery is pending.
  // Preserve its refusal; no Intel document exists to validate or blame.
  if (!document) return null;
  if (!EII_RemoveCarriedRecords(document)) return null;
  if (!EII_CDFPayload.ValidDocument(document))
  {
   if (!EII_CDFLoad.StorageRefusal.IsEmpty()) Print("[EII CDF] Save rejected: " + EII_CDFLoad.StorageRefusal, LogLevel.ERROR);
   else Print("[EII CDF] Save rejected: invalid intel payload or more than 400 intel items in one inventory", LogLevel.ERROR);
   return null;
  }
  // A later adapter may wrap a carrier's state around a rack/drive failure marker.
  if (!EII_CDFLoad.StorageRefusal.IsEmpty())
  {
   Print("[EII CDF] Save rejected: " + EII_CDFLoad.StorageRefusal, LogLevel.ERROR);
   return null;
  }
  return document;
 }
 // Clear calls this public method across classes; no internal ShouldCapture hook.
 override static bool IsManaged(SCR_EditableEntityComponent entity)
 {
  if (EII_CDFItem.IsCarriedIntel(entity) || EII_CDFRackBridge.IsCarriedDrive(entity)) return false;
  return super.IsManaged(entity);
 }
 // CaptureEntity's internal static ShouldCapture cannot be used as an extension point.
 protected static bool EII_RemoveCarriedRecords(CDF_GMSaveDocument document)
 {
  if (!document || !document.m_aEntities) return false;
  array<int> remap = {};
  array<ref CDF_GMSaveEntityRecord> kept = {};
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record) return false;
   if (EII_CDFItem.IsCarriedIntel(record.m_Entity) || EII_CDFRackBridge.IsCarriedDrive(record.m_Entity)) { remap.Insert(-1); continue; }
   remap.Insert(kept.Count());
   kept.Insert(record);
  }
  foreach (CDF_GMSaveEntityRecord record : kept)
  {
   if (record.m_iParent >= 0)
   {
    if (record.m_iParent >= remap.Count() || remap[record.m_iParent] < 0)
    {
     Print("[EII CDF] Save rejected: unexpected child of a carried intel item", LogLevel.ERROR);
     return false;
    }
    record.m_iParent = remap[record.m_iParent];
   }
   if (record.m_iTarget >= 0)
   {
    if (record.m_iTarget >= remap.Count()) return false;
    record.m_iTarget = remap[record.m_iTarget];
   }
  }
  document.m_aEntities = kept;
  return true;
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  int timing = System.GetTickCount();
  int records = document.m_aEntities.Count();
  EII_CDFLoad.Reset();
  if (!EII_CDFPayload.ValidDocument(document, true))
  {
   EII_CDFLoad.Forget();
   if (!EII_CDFLoad.StorageRefusal.IsEmpty()) Print("[EII CDF] Load rejected before world clearing: " + EII_CDFLoad.StorageRefusal, LogLevel.ERROR);
   else Print("[EII CDF] Load rejected before world clearing: invalid intel state", LogLevel.ERROR);
   return false;
  }
  int inner = System.GetTickCount();
  bool result = super.Restore(document);
  int innerEnd = System.GetTickCount();
  if (result)
  {
   GetGame().GetCallqueue().Remove(EII_FinishTiming);
   GetGame().GetCallqueue().CallLater(EII_FinishTiming, 600, false);
  }
  else EII_CDFLoad.Forget();
  int before = inner - timing;
  int after = System.GetTickCount() - innerEnd;
  PrintFormat("[CDF TIMING] intel-items restore path=scan result=%1 records=%2 payload=%3 ownMs=%4 beforeMs=%5 afterMs=%6 innerMs=%7", result, records, EII_CDFLoad.Payloads, before + after, before, after, innerEnd - inner);
  return result;
 }
 // One Apply timing line per load, once the CDF deferred state pass has run (or timed out).
 protected static void EII_FinishTiming()
 {
  bool complete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty();
  if (!complete && GetGame())
  {
   EII_CDFLoad.FinishAttempts++;
   if (EII_CDFLoad.FinishAttempts < 20) { GetGame().GetCallqueue().CallLater(EII_FinishTiming, 100, false); return; }
  }
  EII_CDFLoad.Forget();
  PrintFormat("[CDF TIMING] intel-items apply calls=%1 payload=%2 ownMs=%3 innerMs=%4 slowestOwnMs=%5 complete=%6", EII_CDFLoad.ApplyCalls, EII_CDFLoad.ApplyPayload, EII_CDFLoad.ApplyOwnMs, EII_CDFLoad.ApplyInnerMs, EII_CDFLoad.ApplySlowestMs, complete);
  // olderRecords: rack/drive records saved before this support; they keep prefab defaults.
  if (EII_CDFLoad.StoragePayloads > 0 || EII_CDFLoad.StorageOlder > 0 || EII_CDFLoad.StorageFailed > 0)
   PrintFormat("[EII CDF] Server racks and USB drives: racks=%1 drives=%2 carriedDrives=%3 failed=%4 payloads=%5 olderRecords=%6", EII_CDFLoad.StorageRacks, EII_CDFLoad.StorageDrives, EII_CDFLoad.StorageCarried, EII_CDFLoad.StorageFailed, EII_CDFLoad.StoragePayloads, EII_CDFLoad.StorageOlder);
 }
}
