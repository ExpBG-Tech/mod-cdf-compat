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
 // [CDF TIMING]: own time excludes super (the rest of the adapter chain and CDF itself).
 static int ApplyCalls, ApplyPayload, ApplyOwnMs, ApplyInnerMs, ApplySlowestMs;
 static void ResetApplyTiming()
 {
  ApplyCalls = 0;
  ApplyPayload = 0;
  ApplyOwnMs = 0;
  ApplyInnerMs = 0;
  ApplySlowestMs = 0;
 }
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
// One CDF group record that ended a restore without AI (see EBG_ReportEmptyGroups).
class EBG_CDFEmptyGroup
{
 int Index, Children, Characters, Missing, Destroyed, Waypoints;
 CDF_GMSaveEntityRecord Record;
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
  int timing = System.GetTickCount();
  EBG_CacheZone zone = EBG_CacheZone.Cast(entity);
  if (!zone || !EBG_CDFCacheState.Declared(state))
  {
   int passInner = System.GetTickCount();
   super.Apply(entity, state);
   EBG_CDFCacheState.ApplyTiming(false, timing, passInner, System.GetTickCount());
   return;
  }
  string payload, original;
  if (!EBG_CDFCacheState.Unwrap(state, payload, original))
  {
   EBG_CDFCacheState.Fail("Module snapshot envelope is invalid");
   EBG_CDFCacheState.ApplyTiming(true, timing, 0, 0);
   return;
  }
  int inner = System.GetTickCount();
  super.Apply(entity, original);
  int innerEnd = System.GetTickCount();
  if (!EBG_CacheSnapshot.ReadZone(zone, payload)) EBG_CDFCacheState.Fail("Module snapshot import failed");
  EBG_CDFCacheState.ApplyTiming(true, timing, inner, innerEnd);
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
  int timing = System.GetTickCount();
  int records = document.m_aEntities.Count();
  int payloadRecords;
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
   payloadRecords++;
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
  EBG_CDFCacheState.ResetApplyTiming();
  int inner = System.GetTickCount();
  bool result = super.Restore(document);
  int innerEnd = System.GetTickCount();
  EBG_CDFAuthors.FinishAssignments(document);
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
  string path = "scan";
  if (ownsState) path = "strict";
  int before = inner - timing;
  int after = System.GetTickCount() - innerEnd;
  PrintFormat("[CDF TIMING] unit-caching restore path=%1 result=%2 records=%3 payload=%4 ownMs=%5 beforeMs=%6 afterMs=%7 innerMs=%8", path, result, records, payloadRecords, before + after, before, after, innerEnd - inner);
  return result;
 }
 static void EBG_ShutdownForWorldCleanup()
 {
  if (GetGame())
  {
   GetGame().GetCallqueue().Remove(FinishRestore);
   GetGame().GetCallqueue().Remove(EBG_FinishPortableImport);
   GetGame().GetCallqueue().Remove(EBG_CDFAuthors.PrintUnregistered);
  }
  // Flush the pending unregistered-author summary now: it clears both maps, so the next
  // such delete schedules a fresh window instead of the diagnostic going silent (0.1.8).
  EBG_CDFAuthors.PrintUnregistered();
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
  if (complete && document) EBG_ReportEmptyGroups(document);
  PrintFormat("[EBG CDF LOAD FINALIZED] success=%1 nativeComplete=%2", !EBG_CDFCacheState.Failed, complete);
  if (EBG_CDFCacheState.Failed) Print("[EBG CDF LOAD] Import failed: " + EBG_CDFCacheState.Reason, LogLevel.ERROR);
  PrintFormat("[CDF TIMING] unit-caching apply calls=%1 payload=%2 ownMs=%3 innerMs=%4 slowestOwnMs=%5 complete=%6", EBG_CDFCacheState.ApplyCalls, EBG_CDFCacheState.ApplyPayload, EBG_CDFCacheState.ApplyOwnMs, EBG_CDFCacheState.ApplyInnerMs, EBG_CDFCacheState.ApplySlowestMs, complete);
 }
 // CDF guards every group whose record has children and later reports how many stayed
 // without AI. Name each one and say whether its members were in the save at all, so a
 // save captured after members were removed (old Full caching without this companion)
 // is not mistaken for a restore failure. Runs once per load, after CDF finalization.
 protected static void EBG_ReportEmptyGroups(CDF_GMSaveDocument document)
 {
  array<ref CDF_GMSaveEntityRecord> records = document.m_aEntities;
  map<int, ref EBG_CDFEmptyGroup> candidates = new map<int, ref EBG_CDFEmptyGroup>();
  array<ref EBG_CDFEmptyGroup> groups = {};
  for (int i = 0; i < records.Count(); i++)
  {
   CDF_GMSaveEntityRecord record = records[i];
   if (!record || !record.m_Entity) continue;
   SCR_AIGroup group = SCR_AIGroup.Cast(record.m_Entity.GetOwner());
   if (!group || group.GetAgentsCount() != 0) continue;
   EBG_CDFEmptyGroup candidate = new EBG_CDFEmptyGroup();
   candidate.Index = i;
   candidate.Record = record;
   candidates.Insert(i, candidate);
   groups.Insert(candidate);
  }
  if (groups.IsEmpty()) return;
  foreach (CDF_GMSaveEntityRecord child : records)
  {
   EBG_CDFEmptyGroup parent;
   if (!child || !candidates.Find(child.m_iParent, parent)) continue;
   parent.Children++;
   int type = child.m_iEntityType;
   if (type < 0 && child.m_Entity) type = child.m_Entity.GetEntityType();
   if (type == EEditableEntityType.WAYPOINT) parent.Waypoints++;
   else if (type == EEditableEntityType.CHARACTER || (type < 0 && !child.m_Entity))
   {
    // Files written before CDF 1.4.1 carry no type: a child that is gone counts as a missing member.
    parent.Characters++;
    if (!child.m_Entity) parent.Missing++;
    else if (child.m_Entity.IsDestroyed() || SCR_Enum.HasFlag(child.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED)) parent.Destroyed++;
   }
  }
  // Full snapshots held by this document's cache modules, matched by group prefab and position.
  array<string> cachedPrefabs = {};
  array<vector> cachedPositions = {};
  int cachedGroups;
  int cacheModules = EBG_CachedGroupPoses(document, cachedPrefabs, cachedPositions, cachedGroups);
  int empty, listed, neverSaved, spawnFailed, dead, notJoined;
  foreach (EBG_CDFEmptyGroup entry : groups)
  {
   // Same rule as CDF's guard: a group without child records was left to its prefab.
   if (entry.Children == 0) continue;
   empty++;
   string verdict = "not-in-group";
   if (entry.Missing > 0) { verdict = "spawn-failed"; spawnFailed++; }
   else if (entry.Characters == 0) { verdict = "never-saved"; neverSaved++; }
   else if (entry.Destroyed == entry.Characters) { verdict = "saved-dead"; dead++; }
   else notJoined++;
   if (listed >= 64) continue;
   listed++;
   bool cached = false;
   for (int c = 0; c < cachedPrefabs.Count() && !cached; c++)
   {
    cached = cachedPrefabs[c] == entry.Record.m_sPrefab && vector.DistanceSq(cachedPositions[c], entry.Record.m_vPosition) < 4;
   }
   Print(string.Format("[EBG CDF EMPTY GROUP] members=%1 record=%2 prefab=%3 pos=%4 ", verdict, entry.Index, entry.Record.m_sPrefab, entry.Record.m_vPosition) + string.Format("savedCharacters=%1 missing=%2 destroyed=%3 waypoints=%4 otherChildren=%5 cacheSnapshot=%6", entry.Characters, entry.Missing, entry.Destroyed, entry.Waypoints, entry.Children - entry.Characters - entry.Waypoints, cached), LogLevel.WARNING);
  }
  if (empty == 0) return;
  PrintFormat("[EBG CDF EMPTY GROUPS] empty=%1 neverSaved=%2 spawnFailed=%3 savedDead=%4 notInGroup=%5 listed=%6 cacheModules=%7 cachedGroups=%8", empty, neverSaved, spawnFailed, dead, notJoined, listed, cacheModules, cachedGroups, level: LogLevel.WARNING);
 }
 protected static int EBG_CachedGroupPoses(CDF_GMSaveDocument document, array<string> prefabs, array<vector> positions, out int total)
 {
  int modules;
  total = 0;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record || !EBG_CDFCacheState.IsModule(record.m_sPrefab) || !EBG_CDFCacheState.Declared(record.m_sState)) continue;
   string payload, original;
   JsonLoadContext context = new JsonLoadContext();
   int count;
   if (!EBG_CDFCacheState.Unwrap(record.m_sState, payload, original) || !context.LoadFromString(payload) || !context.ReadValue("cachedGroups", count)) continue;
   modules++;
   total += count;
   for (int i = 0; i < count && i < 2048; i++)
   {
    if (!context.StartObject("cached" + i.ToString())) break;
    string prefab;
    array<vector> matrix = {};
    if (context.ReadValue("prefab", prefab) && context.ReadValue("matrix", matrix) && matrix.Count() == 4)
    {
     prefabs.Insert(prefab);
     positions.Insert(matrix[3]);
    }
    context.EndObject();
   }
  }
  return modules;
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
