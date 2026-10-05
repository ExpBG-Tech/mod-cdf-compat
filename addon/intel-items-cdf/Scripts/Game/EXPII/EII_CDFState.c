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
  Resource resource = Resource.Load(prefab);
  return resource && resource.IsValid();
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
 static bool ValidDocument(CDF_GMSaveDocument document)
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
  }
  return true;
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
  if (!entity || !state.Contains("\"eiiIntel\""))
  {
   super.Apply(entity, state);
   return;
  }
  EII_CDFPayload payload;
  string original;
  if (!EII_CDFPayload.Read(state, payload, original))
  {
   Print("[EII CDF] Invalid payload rejected", LogLevel.ERROR);
   return;
  }
  EII_IntelComponent.RestoreDepth++;
  super.Apply(entity, original);
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
  array<IEntity> items = {}, restored = {};
  set<IEntity> assigned = new set<IEntity>();
  manager.GetItems(items, EStoragePurpose.PURPOSE_ANY);
  foreach (IEntity item : items)
   if (item && item.FindComponent(EII_IntelComponent) && item.GetPrefabData()) restored.Insert(item);
  foreach (EII_CDFItem record : records)
  {
   IEntity created;
   string guid = EII_CDFItem.GetIntelGuid(record.prefab);
   foreach (IEntity candidate : restored)
   {
    if (!candidate || assigned.Contains(candidate) || !candidate.GetPrefabData()) continue;
    if (EII_CDFItem.GetIntelGuid(candidate.GetPrefabData().GetPrefabName()) == guid) { created = candidate; break; }
   }
   // CDF can omit deep/overflow items. Only insert when no matching instance exists.
   if (!created)
   {
    array<IEntity> before = {};
    manager.GetItems(before, EStoragePurpose.PURPOSE_ANY);
    if (!manager.TrySpawnPrefabToStorage(record.prefab, null, -1, EStoragePurpose.PURPOSE_ANY))
    {
     Print("[EII CDF] Cannot restore carried intel: inventory rejected item", LogLevel.ERROR);
     continue;
    }
    created = FindNewItemAmong(manager, before);
   }
   if (created) assigned.Insert(created);
   EII_IntelComponent intel;
   if (created) intel = EII_IntelComponent.Cast(created.FindComponent(EII_IntelComponent));
   if (!intel || !intel.RestoreState(record.title, record.content, record.spent))
    Print("[EII CDF] Restored inventory item could not receive intel", LogLevel.ERROR);
   else intel.SetDebug(record.diagnostics);
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
  if (!EII_CDFPayload.ValidDocument(document))
  {
   Print("[EII CDF] Load rejected before world clearing: invalid intel state", LogLevel.ERROR);
   return false;
  }
  return super.Restore(document);
 }
}
