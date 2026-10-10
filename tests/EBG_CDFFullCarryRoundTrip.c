// TEST ONLY. CDF Game Master Save round trip of a Full-cached squad whose soldiers run
// EXPBG Unit Scripts Hold/Freeze and Unit Dialog (GM Tools survivor carry written into
// the portable Full snapshot, EUS_FullCacheCarry.c / EUD_FullCacheCarry.c).
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EBG_CDFFullCarryRoundTrip.c -ExpectResult '\[EXPG FULL CARRY CDF RESULT\] checks=[1-9]\d* failures=0 carried=3 dialogs=1 restored=3 dialogRestored=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file into its fixture addon; the driver class name
// EXPG_CdfRoundTrip is fixed by the runner's layer.
//  1. Authored US fire team in an authored Full cache zone (no players): Hold, Freeze,
//     Hold with a dialog, one soldier without anything.
//  2. The zone Full caches the team; CDF captures the document: the zone's snapshot
//     carries three "eusScript" rows and one "eudDialog" record.
//  3. Load (clearBeforeLoad): zones start disabled after the load, so the imported Full
//     snapshot wakes; the respawned soldiers run Hold (2) and Freeze (1) on the saved
//     spots (1.5 m) and one has the saved dialog.
class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 480;
 static const string FILE = "$profile:full-carry-cdf-roundtrip.json";
 static const string ZONE_PREFAB = "{7E1080ED8F0633FD}PrefabsEditable/EXPBG/EBG_CacheZone.et";
 static const string DIALOG_NAME = "Carry Test";
 vector Point = "4773.46 0 7094.57";
 SCR_AIGroup Team;
 EBG_CacheZone Zone;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 ref array<vector> Spots = {};
 ref array<int> Codes = {};
 ref CDF_GMSaveDocument Loaded;
 int Phase;
 int Checks;
 int Failures;
 int Carried;
 int Dialogs;
 int Restored;
 int DialogRestored;
 float Started;
 float Next;
 float PhaseStarted;
 bool Finished;

 void EXPG_CdfRoundTrip(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  Started = Now();
  Next = Started + 15;
  Print("[EXPG FULL CARRY CDF BEGIN]");
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG FULL CARRY CDF CHECK] pass=%1 %2", value, label);
  return value;
 }
 void Finish(string reason)
 {
  if (Finished)
  {
   return;
  }
  Finished = true;
  ClearEventMask(EntityEvent.FRAME);
  PrintFormat("[EXPG FULL CARRY CDF RESULT] checks=%1 failures=%2 carried=%3 dialogs=%4 restored=%5 dialogRestored=%6 reason=%7", Checks, Failures, Carried, Dialogs, Restored, DialogRestored, reason);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG FULL CARRY CDF PHASE] phase=%1 elapsed=%2", Phase, Now() - Started);
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - PhaseStarted <= seconds)
  {
   return false;
  }
  Check(false, label);
  Finish("phase " + Phase.ToString());
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
 void Author(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable)
  {
   return;
  }
  SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
  author.Initialize("full-carry-cdf", "", 0, -1);
  editable.SetAuthor(author);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 protected array<IEntity> m_Near;
 bool CollectSoldier(IEntity entity)
 {
  if (m_Near && SCR_ChimeraCharacter.Cast(entity)) m_Near.Insert(entity);
  return true;
 }
 int Soldiers(notnull array<SCR_ChimeraCharacter> found)
 {
  found.Clear();
  array<IEntity> near = {};
  m_Near = near;
  GetGame().GetWorld().QueryEntitiesBySphere(Point, 40, CollectSoldier);
  m_Near = null;
  foreach (IEntity entity : near)
  {
   SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
   if (soldier && soldier.GetCharacterController() && !soldier.GetCharacterController().IsDead()) found.Insert(soldier);
  }
  return found.Count();
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (Finished || Now() < Next)
  {
   return;
  }
  Next = Now() + 0.5;
  if (Now() - Started > FIXTURE_SECONDS)
  {
   Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, Phase));
   Finish("timeout");
   return;
  }
  Step();
 }
 void Step()
 {
  if (Phase == 0) Setup();
  else if (Phase == 1) BindScripts();
  else if (Phase == 2) WaitCached();
  else if (Phase == 3) CaptureAndLoad();
  else if (Phase == 4) CheckLoaded();
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
  Point[1] = GetGame().GetWorld().GetSurfaceY(Point[0], Point[2]);
  Resource teamResource = Resource.Load("{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et");
  IEntity teamEntity = GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point));
  Team = SCR_AIGroup.Cast(teamEntity);
  if (!Check(Team != null, "authored US fire team spawned"))
  {
   Finish("setup");
   return;
  }
  Author(Team);
  Advance(1);
 }
 void BindScripts()
 {
  if (!Team || !Team.EBG_HasCompletedInitialSpawn() || Team.GetAgentsCount() != 4)
  {
   Waited(60, "fire team spawned its four members");
   return;
  }
  if (Now() - PhaseStarted < 4)
  {
   return;
  }
  Manager = EUS_Manager.Get();
  if (!Check(Manager != null, "Unit Scripts manager"))
  {
   Finish("setup");
   return;
  }
  array<int> scripts = {EUS_Codes.HOLD, EUS_Codes.FREEZE, EUS_Codes.HOLD};
  array<AIAgent> agents = {};
  Team.GetAgents(agents);
  IEntity leader = Team.GetLeaderEntity();
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member || member == leader || Codes.Count() >= scripts.Count()) continue;
   int code = scripts[Codes.Count()];
   Check(Manager.ApplyUnit(member, code, Report) && member.EUS_Script == code, "script bound: " + EUS_Codes.Describe(code));
   Codes.Insert(code);
   if (Codes.Count() == 3)
   {
    SCR_EditableCharacterComponent unit = EUD_Dialog.Find(member);
    array<string> lines = {"Line one of the carry test."};
    Check(unit && unit.EUD_RestoreState(DIALOG_NAME, lines, 0), "dialog set on the third scripted soldier");
   }
  }
  if (!Check(Codes.Count() == 3, "three followers scripted"))
  {
   Finish("setup");
   return;
  }
  // Full mode (1), 60 m zone, wake 100 m, sleep 300 m, 5 s sleep delay, debug on, enabled.
  Resource zoneResource = Resource.Load(ZONE_PREFAB);
  Zone = EBG_CacheZone.Cast(GetGame().SpawnEntityPrefab(zoneResource, GetGame().GetWorld(), Params(Point)));
  if (!Check(Zone != null, "Full cache zone spawned"))
  {
   Finish("setup");
   return;
  }
  Author(Zone);
  Zone.SetValue(1, 1); Zone.SetValue(2, 0); Zone.SetValue(3, 60);
  Zone.SetValue(4, 100); Zone.SetValue(5, 300); Zone.SetValue(12, 5);
  Zone.SetValue(15, 0); Zone.SetValue(18, 0); Zone.SetValue(21, 1);
  Zone.SetValue(0, 1);
  Advance(2);
 }
 void WaitCached()
 {
  // Spots once the scripts settled (before the cache deletes the soldiers).
  if (Spots.IsEmpty() && Now() - PhaseStarted > 3)
  {
   array<SCR_ChimeraCharacter> soldiers = {};
   Soldiers(soldiers);
   foreach (SCR_ChimeraCharacter soldier : soldiers)
   {
    EUS_UnitControl control = Manager.FindControl(soldier);
    if (control) Spots.Insert(control.GetAnchor());
   }
  }
  EBG_CacheGroup record;
  if (EBG_CacheManager.Get()) record = EBG_CacheManager.Get().FindGroup(Team);
  if (!record || !record.Full || record.Full.GetState() != EBG_FullGroupPhase.CACHED)
  {
   string reason;
   if (record) reason = record.Reason;
   Waited(120, "Hold/Freeze team Full cached; reason='" + reason + "'");
   return;
  }
  Check(Spots.Count() == 3, string.Format("three script spots noted before the cache (%1)", Spots.Count()));
  Advance(3);
 }
 void CaptureAndLoad()
 {
  if (Now() - PhaseStarted < 3)
  {
   return;
  }
  CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("full-carry-cdf", "fixture");
  if (!Check(document != null, "CDF captured the document with the Full-cached team"))
  {
   Finish("capture");
   return;
  }
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record || !record.m_sState.Contains("cachedGroups")) continue;
   string state = record.m_sState;
   int at = state.IndexOf("eusScript");
   while (at >= 0)
   {
    Carried++;
    at = state.IndexOfFrom(at + 9, "eusScript");
   }
   at = state.IndexOf("eudDialog");
   while (at >= 0)
   {
    Dialogs++;
    at = state.IndexOfFrom(at + 9, "eudDialog");
   }
  }
  Check(Carried == 3, string.Format("the zone snapshot carries three unit scripts (%1)", Carried));
  Check(Dialogs == 1, string.Format("the zone snapshot carries one dialog (%1)", Dialogs));
  Loaded = new CDF_GMSaveDocument();
  if (!Check(document.SaveToFile(FILE) && Loaded.LoadFromFile(FILE), "document written and read back"))
  {
   Finish("file");
   return;
  }
  if (!Check(CDF_GMSaveRestore.Restore(Loaded), "CDF restore with clearBeforeLoad"))
  {
   Finish("restore");
   return;
  }
  Advance(4);
 }
 void CheckLoaded()
 {
  array<SCR_ChimeraCharacter> soldiers = {};
  Soldiers(soldiers);
  int holds;
  int freezes;
  int dialogs;
  foreach (SCR_ChimeraCharacter soldier : soldiers)
  {
   SCR_EditableCharacterComponent unit = EUD_Dialog.Find(soldier);
   if (unit && unit.EUD_IsConfigured() && unit.EUD_GetName() == DIALOG_NAME) dialogs++;
   EUS_UnitControl control = Manager.FindControl(soldier);
   if (!control || !control.IsBound()) continue;
   bool near;
   foreach (vector spot : Spots)
   {
    if (vector.DistanceXZ(control.GetAnchor(), spot) <= 1.5) near = true;
   }
   if (!near) continue;
   if (control.GetCode() == EUS_Codes.HOLD) holds++;
   else if (control.GetCode() == EUS_Codes.FREEZE) freezes++;
  }
  if (holds < 2 || freezes < 1 || dialogs < 1)
  {
   Waited(120, string.Format("after the load the Full snapshot woke with its scripts and dialog (hold=%1 freeze=%2 dialog=%3 soldiers=%4)", holds, freezes, dialogs, soldiers.Count()));
   return;
  }
  Restored = holds + freezes;
  DialogRestored = dialogs;
  Check(holds == 2 && freezes == 1, string.Format("Hold (2) and Freeze (1) restored on the saved spots (%1, %2)", holds, freezes));
  Check(dialogs == 1, "the dialog restored on its soldier");
  Check(EBG_CacheZone.Zones.Count() > 0 && !EBG_CacheZone.Zones[0].Enabled, "the loaded zone starts disabled");
  Finish("completed");
 }
}
