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
 static string GetIntelGuid(string resource)
 {
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
 bool CaptureInventory(array<IEntity> items)
 {
  // Track only Intel; ordinary cargo must not grow duplicate-check work.
  set<IEntity> visited = new set<IEntity>();
  foreach (IEntity entity : items)
  {
   if (!entity) continue;
   EII_IntelComponent intel = EII_IntelComponent.Cast(entity.FindComponent(EII_IntelComponent));
   if (!intel || visited.Contains(entity)) continue;
   // Reject, never truncate: do not allocate or serialize an oversized payload.
   if (inventory.Count() >= INVENTORY_LIMIT) return false;
   visited.Insert(entity);
   EII_CDFItem record = new EII_CDFItem();
   record.Capture(intel);
   inventory.Insert(record);
  }
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
   string guid = EII_CDFItem.GetIntelGuid(record.m_sPrefab);
   if (!record.m_sState.Contains("\"eiiIntel\""))
   {
    if (!guid.IsEmpty()) return false;
    continue;
   }
   EII_CDFPayload payload;
   string original;
   if (!Read(record.m_sState, payload, original)) return false;
   if (!guid.IsEmpty() && !payload.item) return false;
   if (payload.item && EII_CDFItem.GetIntelGuid(payload.item.prefab) != guid) return false;
   if (keep) EII_CDFLoad.Keep(record.m_sState, payload, original);
  }
  return true;
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
  EII_CDFPayload payload = new EII_CDFPayload();
  EII_IntelComponent intel = EII_IntelComponent.Cast(entity.FindComponent(EII_IntelComponent));
  if (intel)
  {
   payload.item = new EII_CDFItem();
   payload.item.Capture(intel);
  }
  CDF_GMSaveConfig config = CDF_GMSaveConfig.GetInstance();
  InventoryStorageManagerComponent manager = InventoryStorageManagerComponent.Cast(entity.FindComponent(InventoryStorageManagerComponent));
  if (manager && config.m_bSaveInventories && CanCaptureInventory(entity, config))
  {
   array<IEntity> items = {};
   manager.GetItems(items, EStoragePurpose.PURPOSE_ANY);
   if (!payload.CaptureInventory(items)) return "{\"eiiIntel\":{\"version\":0}}";
  }
  if (!payload.item && payload.inventory.IsEmpty()) return original;
  JsonSaveContext context = new JsonSaveContext();
  // Keep the marker on encoding failure so document validation also rejects carriers.
  if (!context.WriteValue("eiiIntel", payload) || !context.WriteValue("cdfState", original))
   return "{\"eiiIntel\":{\"version\":0}}";
  string encoded = context.SaveToString();
  if (encoded.IsEmpty()) return "{\"eiiIntel\":{\"version\":0}}";
  return encoded;
 }
 override static void Apply(IEntity entity, string state)
 {
  int timing = System.GetTickCount();
  if (!entity || !state.Contains("\"eiiIntel\""))
  {
   int passInner = System.GetTickCount();
   super.Apply(entity, state);
   EII_CDFLoad.ApplyTiming(false, timing, passInner, System.GetTickCount());
   return;
  }
  EII_CDFPayload payload;
  string original;
  if (!EII_CDFLoad.ReadValidated(state, payload, original))
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
  EII_CDFLoad.ApplyTiming(true, timing, inner, innerEnd);
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
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  // An upstream adapter can refuse capture while recovery is pending.
  // Preserve its refusal; no Intel document exists to validate or blame.
  if (!document) return null;
  if (!EII_RemoveCarriedRecords(document)) return null;
  if (!EII_CDFPayload.ValidDocument(document))
  {
   Print("[EII CDF] Save rejected: invalid intel payload or more than 400 intel items in one inventory", LogLevel.ERROR);
   return null;
  }
  return document;
 }
 // Clear calls this public method across classes; no internal ShouldCapture hook.
 override static bool IsManaged(SCR_EditableEntityComponent entity)
 {
  if (EII_CDFItem.IsCarriedIntel(entity)) return false;
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
   if (EII_CDFItem.IsCarriedIntel(record.m_Entity)) { remap.Insert(-1); continue; }
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
   EII_CDFLoad.Validated.Clear();
   Print("[EII CDF] Load rejected before world clearing: invalid intel state", LogLevel.ERROR);
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
  else EII_CDFLoad.Validated.Clear();
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
  EII_CDFLoad.Validated.Clear();
  PrintFormat("[CDF TIMING] intel-items apply calls=%1 payload=%2 ownMs=%3 innerMs=%4 slowestOwnMs=%5 complete=%6", EII_CDFLoad.ApplyCalls, EII_CDFLoad.ApplyPayload, EII_CDFLoad.ApplyOwnMs, EII_CDFLoad.ApplyInnerMs, EII_CDFLoad.ApplySlowestMs, complete);
 }
}
