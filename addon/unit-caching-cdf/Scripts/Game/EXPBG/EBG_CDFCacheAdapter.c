// Optional CDF 1.4.1 hooks. The original CDF deep state remains opaque and round trips intact.
class EBG_CDFCacheState
{
 static bool Failed, CaptureFailed;
 static World ImportWorld;
 static SCR_BaseGameMode ImportMode;
 static int FinishAttempts;
 static string Reason;
 static ref CDF_GMSaveDocument PendingDocument;
 static ref CDF_GMSaveDocument RecoveryDocument;
 static bool IsModule(ResourceName prefab)
 {
  return prefab.Contains("7E1080ED8F0633FD");
 }
 static bool Declared(string state)
 {
  JsonLoadContext context = new JsonLoadContext();
  string payload;
  if (!context.LoadFromString(state)) return false;
  // A wrongly typed declared key is still an envelope and must fail Unwrap, not fall back to legacy state.
  return context.ReadValue("ebgCache", payload) || state.Contains("\"ebgCache\"");
 }
 static bool Unwrap(string state, out string payload, out string original)
 {
  JsonLoadContext context = new JsonLoadContext();
  if (!context.LoadFromString(state)) return false;
  if (!context.ReadValue("ebgCache", payload)) return false;
  if (!context.ReadValue("cdfState", original)) return false;
  return true;
 }
 static bool UniqueTokens(string payload, array<int> tokens)
 {
  JsonLoadContext context = new JsonLoadContext();
  int count;
  if (!context.LoadFromString(payload) || !context.ReadValue("cachedGroups", count) || count < 0 || count > 2048) return false;
  for (int i = 0; i < count; i++)
  {
   int token;
   if (!context.StartObject("cached" + i.ToString()) || !context.ReadValue("token", token) || !context.EndObject() || tokens.Contains(token)) return false;
   tokens.Insert(token);
  }
  return true;
 }
 static void Reset()
 {
  EBG_CDFAuthors.Reset();
  Failed = false;
  CaptureFailed = false;
  Reason = "";
  PendingDocument = null;
  RecoveryDocument = null;
  ImportWorld = null;
  ImportMode = null;
  FinishAttempts = 0;
 }
 static void Reject(string reason)
 {
  Print("[EBG CDF HOLD] " + reason, LogLevel.WARNING);
 }
 static void Fail(string reason)
 {
  Failed = true;
  Reason = reason;
  Print("[EBG CDF HOLD] " + reason, LogLevel.WARNING);
 }
}
modded class CDF_GMSaveState
{
 override static string Capture(IEntity entity)
 {
  string original = super.Capture(entity);
  EBG_CacheZone zone = EBG_CacheZone.Cast(entity);
  if (!zone) return original;
  string payload;
  if (!EBG_CacheSnapshot.WriteZone(zone, payload))
  {
   EBG_CDFCacheState.CaptureFailed = true;
   EBG_CDFCacheState.Reject("Module snapshot export failed");
   return original;
  }
  JsonSaveContext context = new JsonSaveContext();
  context.WriteValue("ebgCache", payload);
  context.WriteValue("cdfState", original);
  return context.SaveToString();
 }
 override static void Apply(IEntity entity, string state)
 {
  EBG_CacheZone zone = EBG_CacheZone.Cast(entity);
  if (!zone || !EBG_CDFCacheState.Declared(state))
  {
   super.Apply(entity, state);
   return;
  }
  string payload, original;
  if (!EBG_CDFCacheState.Unwrap(state, payload, original))
  {
   EBG_CDFCacheState.Fail("Module snapshot envelope is invalid");
   return;
  }
  super.Apply(entity, original);
  if (!EBG_CacheSnapshot.ReadZone(zone, payload)) EBG_CDFCacheState.Fail("Module snapshot import failed");
 }
}
modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  string reason;
  bool ownsState = !EBG_CacheZone.Zones.IsEmpty() || EBG_CDFCacheState.RecoveryDocument;
  if (EBG_CacheManager.Instance && !EBG_CacheManager.Instance.Records.IsEmpty()) ownsState = true;
  if (ownsState && CDF_GMSaveConfig.GetInstance().m_bUsePersistenceBlob)
  {
   EBG_CDFCacheState.Reject("CDF 1.4.1 native persistence blobs are unsupported with Optimizer state; set usePersistenceBlob=false before saving");
   return null;
  }
  if (EBG_CDFCacheState.RecoveryDocument)
  {
   EBG_CDFCacheState.Reject("Previous CDF module import requires recovery; original payload retained");
   return null;
  }
  if (!EBG_CacheSnapshot.CanSave(reason))
  {
   EBG_CDFCacheState.Reject(reason);
   return null;
  }
  EBG_CDFCacheState.CaptureFailed = false;
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  if (!document || EBG_CDFCacheState.CaptureFailed) return null;
  // CDF's authored filter can omit a module. Reject the entire capture rather than silently lose its roster.
  foreach (EBG_CacheZone zone : EBG_CacheZone.Zones)
  {
   bool found = false;
   foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
   {
    if (!record.m_Entity || record.m_Entity.GetOwner() != zone) continue;
    string payload, original;
    found = EBG_CDFCacheState.Unwrap(record.m_sState, payload, original);
    break;
   }
   if (!found)
   {
    EBG_CDFCacheState.Reject("CDF capture omitted a cache module or its snapshot");
    return null;
   }
  }
  return document;
 }
}
modded class CDF_GMSaveRestore
{
 static CDF_GMSaveEntityRecord EBG_FindAuthorRecord(SCR_EditableEntityComponent entity)
 {
  if (!EBG_CDFAuthors.Restoring || !CurrentImportWorld() || !s_RestoredEntities || !s_RestoredEntities.Contains(entity)) return null;
  // CDF 1.4.1 spawns document records in order. Advance once, not one full scan per entity.
  array<ref CDF_GMSaveEntityRecord> records = EBG_CDFCacheState.PendingDocument.m_aEntities;
  for (int i = EBG_CDFAuthors.NextRecord; i < records.Count(); i++)
  {
   CDF_GMSaveEntityRecord record = records[i];
   if (record.m_Entity != entity) continue;
   EBG_CDFAuthors.NextRecord = i + 1;
   if (!record.m_sAuthorUID.IsEmpty()) return record;
   return null;
  }
  return null;
 }
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  if (EBG_CacheSnapshot.Loading) { EBG_CDFCacheState.Reject("Cache load already in progress");
   return false;
  }
  if (!EBG_CacheManager.IsPortableWorldReady())
  {
   EBG_CDFCacheState.Reject("CDF import requires an active world outside teardown");
   return false;
  }
  bool ownsState = !EBG_CacheZone.Zones.IsEmpty() || EBG_CDFCacheState.RecoveryDocument;
  if (EBG_CacheManager.Instance && !EBG_CacheManager.Instance.Records.IsEmpty()) ownsState = true;
  foreach (CDF_GMSaveEntityRecord candidate : document.m_aEntities)
  {
   if (EBG_CDFCacheState.IsModule(candidate.m_sPrefab)) ownsState = true;
  }
  if (ownsState && CDF_GMSaveConfig.GetInstance().m_bUsePersistenceBlob)
  {
   // CDF 1.4.1's plain JSON blobs lack the native type discriminators. Reject
   // before clearing; never rewrite or silently discard the opaque saved state.
   EBG_CDFCacheState.Reject("CDF 1.4.1 native persistence blobs are unsupported with Optimizer state; set usePersistenceBlob=false before loading");
   return false;
  }
  // All module payloads and referenced assets must pass before CDF can clear any old entities.
  array<int> snapshotTokens = {};
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   // Ordinary CDF saves deliberately skip removed-mod prefabs. Strict validation
   // is needed only when this import could replace Optimizer-owned state.
   if (ownsState)
   {
    Resource entityPrefab = Resource.Load(record.m_sPrefab);
    if (!entityPrefab || !entityPrefab.IsValid())
    {
     EBG_CDFCacheState.Reject("Missing CDF entity prefab before world clearing: " + record.m_sPrefab);
     return false;
    }
   }
   if (!EBG_CDFCacheState.IsModule(record.m_sPrefab) || record.m_sState.IsEmpty()) continue;
   JsonLoadContext moduleState = new JsonLoadContext();
   if (!moduleState.LoadFromString(record.m_sState))
   {
    EBG_CDFCacheState.Reject("Invalid module state before world clearing");
    return false;
   }
   if (!EBG_CDFCacheState.Declared(record.m_sState)) continue;
   string payload, original, reason;
   if (!EBG_CDFCacheState.Unwrap(record.m_sState, payload, original) || !EBG_CacheSnapshot.ValidateZone(payload, reason) || !EBG_CDFCacheState.UniqueTokens(payload, snapshotTokens))
   {
    EBG_CDFCacheState.Reject("Invalid module snapshot before world clearing: " + reason);
    return false;
   }
  }
  if (!CDF_GMSaveConfig.GetInstance().m_bClearBeforeLoad && !snapshotTokens.IsEmpty())
  {
   EBG_CDFCacheState.Reject("Full snapshot import requires CDF clearBeforeLoad to prevent duplicate rosters");
   return false;
  }
  string importReason;
  if (!snapshotTokens.IsEmpty() && !EBG_CacheSnapshot.PrepareImport(importReason))
  {
   EBG_CDFCacheState.Reject("Full import blocked before world clearing: " + importReason);
   return false;
  }
  bool previousFailed = EBG_CDFCacheState.Failed;
  string previousReason = EBG_CDFCacheState.Reason;
  EBG_CDFCacheState.Failed = false;
  EBG_CDFCacheState.Reason = "";
  EBG_CDFCacheState.PendingDocument = document;
  EBG_CDFCacheState.ImportWorld = GetGame().GetWorld();
  EBG_CDFCacheState.ImportMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
  EBG_CacheSnapshot.Loading = true;
  EBG_CDFCacheState.FinishAttempts = 0;
  // CDF 1.4.1 binds its internal Clear/SpawnRecord calls statically, bypassing
  // modded hooks. Its new transaction set proves base Restore reached mutation;
  // retain the old set so reference comparison cannot confuse two allocations.
  ref set<SCR_EditableEntityComponent> previousNativeRestore = s_RestoredEntities;
  EBG_CDFAuthors.Reset();
  EBG_CDFAuthors.Restoring = true;
  bool result = super.Restore(document);
  EBG_CDFAuthors.FinishAssignments();
  if (!result)
  {
   EBG_CacheSnapshot.Loading = false;
   EBG_CacheSnapshot.EndImport();
   if (s_RestoredEntities != previousNativeRestore) EBG_CDFCacheState.RecoveryDocument = document;
   else
   {
    // Another adapter may reject before Clear/SpawnRecord. Keep the untouched
    // scene usable without discarding any earlier genuine recovery document.
    EBG_CDFCacheState.Failed = previousFailed;
    EBG_CDFCacheState.Reason = previousReason;
   }
   EBG_CDFCacheState.PendingDocument = null;
   EBG_CDFCacheState.ImportWorld = null;
   EBG_CDFCacheState.ImportMode = null;
  }
  if (result) GetGame().GetCallqueue().CallLater(EBG_FinishPortableImport, 600, false);
  return result;
 }
 static void EBG_ShutdownForWorldCleanup()
 {
  if (GetGame())
  {
   GetGame().GetCallqueue().Remove(FinishRestore);
   GetGame().GetCallqueue().Remove(EBG_FinishPortableImport);
  }
  EBG_CDFCacheState.Reset();
 }
 override protected static void FinishRestore()
 {
  if (!CurrentImportWorld()) return;
  super.FinishRestore();
  EBG_FinishPortableImport();
 }
 protected static bool CurrentImportWorld()
 {
  return EBG_CDFCacheState.PendingDocument && EBG_CacheManager.IsPortableWorldReady() && GetGame().GetWorld() == EBG_CDFCacheState.ImportWorld && GetGame().GetGameMode() == EBG_CDFCacheState.ImportMode;
 }
 protected static void EBG_FinishPortableImport()
 {
  if (!CurrentImportWorld()) return;
  // CDF 1.4.1 captures its base static callback in Restore. Observe its actual
  // completion instead of assuming a modded static callback will be dispatched.
  bool complete = !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty() && s_aGuardedGroups && s_aGuardedGroups.IsEmpty() && s_aPendingMembers && s_aPendingMembers.IsEmpty();
  if (!complete)
  {
   EBG_CDFCacheState.FinishAttempts++;
   if (EBG_CDFCacheState.FinishAttempts < 10)
   {
    GetGame().GetCallqueue().CallLater(EBG_FinishPortableImport, 100, false);
    return;
   }
   EBG_CDFCacheState.Fail("CDF deferred finalization did not complete; original payload retained");
  }
  GetGame().GetCallqueue().Remove(EBG_FinishPortableImport);
  // CDF applies module deep state in its deferred finalization, not its initial SpawnRecord pass.
  CDF_GMSaveDocument document = EBG_CDFCacheState.PendingDocument;
  if (document) foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!EBG_CDFCacheState.IsModule(record.m_sPrefab) || !EBG_CDFCacheState.Declared(record.m_sState)) continue;
   EBG_CacheZone zone;
   if (record.m_Entity) zone = EBG_CacheZone.Cast(record.m_Entity.GetOwner());
   if (!zone || !zone.EBG_HasImportedCacheSnapshot()) EBG_CDFCacheState.Fail("CDF module was not restored; original payload retained for recovery");
  }
  if (EBG_CDFCacheState.Failed) EBG_CDFCacheState.RecoveryDocument = document;
  else
  {
   EBG_CDFCacheState.RecoveryDocument = null;
   if (CDF_GMSaveConfig.GetInstance().m_bClearBeforeLoad)
   {
    EBG_OptimizerControl.Reset();
    foreach (CDF_GMSaveEntityRecord restored : document.m_aEntities)
    {
     EBG_CacheZone restoredZone;
     if (restored.m_Entity) restoredZone = EBG_CacheZone.Cast(restored.m_Entity.GetOwner());
     if (restoredZone && restoredZone.EBG_HasImportedCacheSnapshot()) restoredZone.EBG_CompletePortableSettingsLoad();
    }
    EBG_OptimizerControl.Publish();
   }
  }
  EBG_CDFCacheState.PendingDocument = null;
  EBG_CacheSnapshot.Loading = false;
  EBG_CacheSnapshot.EndImport();
  PrintFormat("[EBG CDF LOAD FINALIZED] success=%1 nativeComplete=%2", !EBG_CDFCacheState.Failed, complete);
  if (EBG_CDFCacheState.Failed) Print("[EBG CDF LOAD] Import failed: " + EBG_CDFCacheState.Reason, LogLevel.ERROR);
 }
}
modded class EBG_CacheSnapshot
{
 override static void ShutdownForWorldCleanup()
 {
  CDF_GMSaveRestore.EBG_ShutdownForWorldCleanup();
  super.ShutdownForWorldCleanup();
 }
}
