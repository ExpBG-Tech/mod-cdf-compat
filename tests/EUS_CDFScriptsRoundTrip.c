// TEST ONLY. CDF Game Master Save round trip of EXPBG Unit Scripts (Hold, Freeze, an
// ambient animation) through the EXPBG CDF Compat unit-scripts bridge (EUS_CDFState.c),
// in one diagnostic server.
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EUS_CDFScriptsRoundTrip.c -ExpectResult '\[EXPG EUS CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 saved=3 restored=3 posed=1 plain=1 inventories=4 cdfLoss=\d+ brokenRefused=1 invalidSkipped=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner copies this file into its fixture addon; the driver class name
// EXPG_CdfRoundTrip is fixed by the runner's layer.
// Real CDF 1.4.1 code throughout: CDF_GMSaveCapture.Capture, SaveToFile/LoadFromFile,
// CDF_GMSaveRestore.Restore (Clear, spawning, deferred state pass) with character
// inventories on. One authored US fire team: Freeze, Hold, Smoke and one soldier
// without a script. Cases, in order:
//  1. Capture: exactly the three scripted soldiers' records carry the "eusScript"
//     envelope and each decodes to the running script; the file is written and read.
//  2. Load (clearBeforeLoad on): CDF clears and respawns the team; within the restore
//     wait three soldiers run the saved scripts on the saved spots (0.4 m) and headings,
//     Smoke plays, the fourth soldier has no script.
//  3. A document whose envelope lost its wrapped CDF state is refused before anything
//     changes (the three scripts stay bound).
//  4. A document whose script payload is invalid (other version) loads: that soldier has
//     no script, the two others do; the bridge counts it as skipped.
//  5. Control: the same document with every unit script envelope removed loads through
//     CDF alone (no script anywhere). Every soldier of load 2 carries exactly the items
//     CDF alone gives the same soldier the same time after its load (inventories=4):
//     the envelope hands CDF its state unchanged and binding a script moves no item.
// The inventory itself is CDF's: CDF 1.4.1 empties the pouches and respawns their items
// one by one, and items refused at their saved place go to the engine's own placement.
// On this vanilla fire team some of those vanish with or without a script (native run
// 2026-10-09: team leader and rifleman lose one 5-tracer magazine, the grenadier three
// M67; identical in the scripted load and in a load where the leader had no script).
// cdfLoss counts the soldiers CDF alone does not give back every item they had at the
// save (reported, not asserted); [EXPG EUS CDF ROUNDTRIP CDF ITEMS] lists the items and
// whether they were already gone right after CDF applied the state.
// Not covered: GM UI and dialogs, a cold server restart, multiplayer and JIP.
class EUS_CdfExpect
{
 bool m_bScripted;
 int m_iCode;
 vector m_vAnchor;
 vector m_vForward;
 // Sorted prefab names: at the save; in load 2 right after CDF applied the state and
 // once settled; in the control load the same two moments.
 string m_sItems;
 string m_sApplied;
 string m_sLoaded;
 bool m_bLoaded;
 string m_sControlApplied;
 string m_sControl;
 bool m_bControl;
}

class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 420;
 // Seconds after a load before soldiers and inventories are compared.
 static const float SETTLE = 8;
 static const string FILE = "$profile:eus-cdf-roundtrip.json";
 // Characters CDF applied a state to in the current load, where and with what items.
 static ref array<IEntity> s_aApplied;
 static ref array<vector> s_aAppliedSpots;
 static ref array<string> s_aAppliedItems;
 vector Point = "4773.46 0 7094.57";
 SCR_AIGroup Team;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 ref array<ref EUS_CdfExpect> Expect = {};
 ref CDF_GMSaveDocument Loaded;
 ref CDF_GMSaveDocument Broken;
 ref CDF_GMSaveDocument Invalid;
 ref CDF_GMSaveDocument m_Stripped;
 int Phase;
 int Checks;
 int Failures;
 int Saved;
 int Restored;
 int Posed;
 int Plain;
 int Inventories;
 int m_iCdfLoss;
 int BrokenRefused;
 int InvalidSkipped;
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
  PrintFormat("[EXPG EUS CDF ROUNDTRIP BEGIN] cdf=%1 deadline=%2", CDF_GMSave.VERSION, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  Checks++;
  if (!value) Failures++;
  PrintFormat("[EXPG EUS CDF ROUNDTRIP CHECK] pass=%1 %2", value, label);
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
  string first = string.Format("checks=%1 failures=%2 saved=%3 restored=%4 posed=%5 plain=%6", Checks, Failures, Saved, Restored, Posed, Plain);
  string second = string.Format("inventories=%1 cdfLoss=%2 brokenRefused=%3 invalidSkipped=%4 reason=%5", Inventories, m_iCdfLoss, BrokenRefused, InvalidSkipped, reason);
  PrintFormat("[EXPG EUS CDF ROUNDTRIP RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  Phase = phase;
  PhaseStarted = Now();
  PrintFormat("[EXPG EUS CDF ROUNDTRIP PHASE] phase=%1 elapsed=%2", Phase, Now() - Started);
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
 // A Game Master's author: CDF captures (and clears) authored entities only.
 void Author(IEntity entity)
 {
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(entity);
  if (!editable)
  {
   return;
  }
  SCR_EditableEntityAuthor author = new SCR_EditableEntityAuthor();
  author.Initialize("eus-cdf-roundtrip", "", 0, -1);
  editable.SetAuthor(author);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 // Living AI soldiers within 30 m of the team's spot.
 int Soldiers(notnull array<SCR_ChimeraCharacter> found)
 {
  found.Clear();
  array<IEntity> near = {};
  m_Near = near;
  GetGame().GetWorld().QueryEntitiesBySphere(Point, 30, CollectSoldier);
  m_Near = null;
  foreach (IEntity entity : near)
  {
   SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
   if (soldier && soldier.GetCharacterController() && !soldier.GetCharacterController().IsDead()) found.Insert(soldier);
  }
  return found.Count();
 }
 protected array<IEntity> m_Near;
 bool CollectSoldier(IEntity entity)
 {
  if (m_Near && SCR_ChimeraCharacter.Cast(entity)) m_Near.Insert(entity);
  return true;
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
 // Prefab names listed in from and not in to (multisets of Items lists).
 static string Missing(string from, string to)
 {
  array<string> wanted = {};
  array<string> left = {};
  from.Split(";", wanted, true);
  to.Split(";", left, true);
  string missing;
  foreach (string name : wanted)
  {
   int at = left.Find(name);
   if (at < 0)
   {
    missing += name + ";";
    continue;
   }
   left.Remove(at);
  }
  if (missing.IsEmpty())
  {
   return "none";
  }
  return missing;
 }
 static void ResetApplied()
 {
  s_aApplied = {};
  s_aAppliedSpots = {};
  s_aAppliedItems = {};
 }
 // Fixture CDF_GMSaveState.Apply wrapper (end of file): the character's items the moment
 // CDF applied his state, before the Unit Scripts pump can bind a restored script.
 static void NoteApplied(IEntity entity)
 {
  SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
  if (!soldier || !s_aApplied)
  {
   return;
  }
  s_aApplied.Insert(soldier);
  s_aAppliedSpots.Insert(soldier.GetOrigin());
  s_aAppliedItems.Insert(Items(soldier));
 }
 // The character CDF applied nearest the saved spot (within 1.5 m), or -1.
 static int AppliedAt(vector spot)
 {
  int best = -1;
  if (!s_aAppliedSpots)
  {
   return best;
  }
  float bestDistance = 1.5;
  foreach (int index, vector appliedSpot : s_aAppliedSpots)
  {
   float distance = vector.DistanceXZ(appliedSpot, spot);
   if (distance >= bestDistance) continue;
   best = index;
   bestDistance = distance;
  }
  return best;
 }
 // Each saved soldier's items in the current load: right after CDF applied his state and
 // now. Soldiers are found by the spot CDF put them on, so a soldier without a script
 // who walked since the load is still recognised. Returns how many were found.
 int Settle(bool control)
 {
  int found;
  foreach (EUS_CdfExpect expect : Expect)
  {
   int index = AppliedAt(expect.m_vAnchor);
   if (index < 0) continue;
   SCR_ChimeraCharacter actor = SCR_ChimeraCharacter.Cast(s_aApplied[index]);
   if (!actor || actor.IsDeleted()) continue;
   found++;
   if (control)
   {
    expect.m_sControlApplied = s_aAppliedItems[index];
    expect.m_sControl = Items(actor);
    expect.m_bControl = true;
    continue;
   }
   expect.m_sApplied = s_aAppliedItems[index];
   expect.m_sLoaded = Items(actor);
   expect.m_bLoaded = true;
  }
  return found;
 }
 static string Label(EUS_CdfExpect expect)
 {
  if (expect.m_bScripted)
  {
   return EUS_Codes.Describe(expect.m_iCode);
  }
  return "no script";
 }
 // The saved scripted soldier whose spot is nearest this soldier (within 1.5 m), or null.
 EUS_CdfExpect Match(SCR_ChimeraCharacter soldier)
 {
  EUS_CdfExpect best;
  float bestDistance = 1.5;
  foreach (EUS_CdfExpect expect : Expect)
  {
   if (!expect.m_bScripted) continue;
   float distance = vector.DistanceXZ(soldier.GetOrigin(), expect.m_vAnchor);
   if (distance >= bestDistance) continue;
   best = expect;
   bestDistance = distance;
  }
  return best;
 }
 bool Loitering(SCR_ChimeraCharacter soldier)
 {
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(soldier.GetCharacterController());
  return controller && controller.IsLoitering();
 }
 int Bound()
 {
  array<SCR_ChimeraCharacter> soldiers = {};
  Soldiers(soldiers);
  int bound;
  foreach (SCR_ChimeraCharacter soldier : soldiers)
  {
   if (Manager.FindControl(soldier)) bound++;
  }
  return bound;
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
  if (Phase == 0)
  {
   Setup();
   return;
  }
  if (Phase == 1)
  {
   BindScripts();
   return;
  }
  if (Phase == 2)
  {
   CaptureAndLoad();
   return;
  }
  if (Phase == 3)
  {
   CheckLoaded();
   return;
  }
  if (Phase == 4)
  {
   CheckInvalid();
   return;
  }
  if (Phase == 5)
  {
   CheckControl();
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
  Point[1] = GetGame().GetWorld().GetSurfaceY(Point[0], Point[2]);
  ResourceName teamName = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(teamName);
  IEntity teamEntity = GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(Point));
  Team = SCR_AIGroup.Cast(teamEntity);
  if (!Check(Team != null, "authored US fire team spawned"))
  {
   Finish("setup");
   return;
  }
  Author(Team);
  Team.EBG_Exclude = true;
  Advance(1);
 }
 void BindScripts()
 {
  if (Team.GetAgentsCount() < 4 || Now() - PhaseStarted < 6)
  {
   Waited(40, "the team has four AI members within 40 seconds");
   return;
  }
  Manager = EUS_Manager.Get();
  array<AIAgent> agents = {};
  Team.GetAgents(agents);
  array<int> codes = {EUS_Codes.FREEZE, EUS_Codes.HOLD, EUS_Codes.ANIMATION + 2};
  int index;
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (!member) continue;
   if (index < codes.Count()) Check(Manager.ApplyUnit(member, codes[index], Report), "script bound: " + EUS_Codes.Describe(codes[index]));
   index++;
  }
  Advance(2);
 }
 void CaptureAndLoad()
 {
  if (Now() - PhaseStarted < 6)
  {
   return;
  }
  // 1. Capture: every soldier's spot and items in the same frame as the save.
  array<SCR_ChimeraCharacter> soldiers = {};
  Soldiers(soldiers);
  int scripted;
  foreach (SCR_ChimeraCharacter soldier : soldiers)
  {
   EUS_CdfExpect expect = new EUS_CdfExpect();
   expect.m_vAnchor = soldier.GetOrigin();
   EUS_UnitControl control = Manager.FindControl(soldier);
   if (control)
   {
    expect.m_bScripted = true;
    expect.m_iCode = control.GetCode();
    expect.m_vAnchor = control.GetAnchor();
    expect.m_vForward = control.GetForward();
    scripted++;
   }
   expect.m_sItems = Items(soldier);
   Expect.Insert(expect);
  }
  CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("eus-cdf-roundtrip", "fixture");
  if (!Check(document != null && scripted == 3 && Expect.Count() == 4, "CDF captured a document with three scripted soldiers"))
  {
   Finish("capture");
   return;
  }
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   string payload;
   string original;
   if (EUS_CDF.Locate(record.m_sState, EUS_CDF.PEEL_LIMIT, payload, original) != 1) continue;
   string reason;
   EUS_UnitState state = EUS_UnitState.Decode(payload, reason);
   IEntity owner;
   if (record.m_Entity) owner = record.m_Entity.GetOwner();
   SCR_ChimeraCharacter saved = SCR_ChimeraCharacter.Cast(owner);
   if (state && saved && state.Code == saved.EUS_Script) Saved++;
   else Check(false, "a saved envelope decodes to its soldier's running script: " + reason);
  }
  Check(Saved == 3, string.Format("exactly the three scripted soldiers carry the envelope (%1)", Saved));
  if (!Check(document.SaveToFile(FILE), "document written to " + FILE))
  {
   Finish("file");
   return;
  }
  Loaded = new CDF_GMSaveDocument();
  Broken = new CDF_GMSaveDocument();
  Invalid = new CDF_GMSaveDocument();
  m_Stripped = new CDF_GMSaveDocument();
  if (!Check(Loaded.LoadFromFile(FILE) && Broken.LoadFromFile(FILE) && Invalid.LoadFromFile(FILE) && m_Stripped.LoadFromFile(FILE), "document read back from the file"))
  {
   Finish("file");
   return;
  }
  // 2. Load.
  ResetApplied();
  if (!Check(CDF_GMSaveRestore.Restore(Loaded), "CDF restore with clearBeforeLoad"))
  {
   Finish("restore");
   return;
  }
  Advance(3);
 }
 void CheckLoaded()
 {
  if (Bound() < 3 || Manager.CountPendingRestores() > 0)
  {
   Waited(EUS_Manager.RESTORE_WAIT + 20, string.Format("three restored soldiers run their scripts within the restore wait (%1)", Bound()));
   return;
  }
  if (Now() - PhaseStarted < SETTLE)
  {
   return;
  }
  array<SCR_ChimeraCharacter> back = {};
  Soldiers(back);
  Check(back.Count() == 4, string.Format("CDF restored the four soldiers once (%1)", back.Count()));
  foreach (SCR_ChimeraCharacter returned : back)
  {
   EUS_UnitControl returnedControl = Manager.FindControl(returned);
   if (!returnedControl)
   {
    Plain++;
    continue;
   }
   EUS_CdfExpect match = Match(returned);
   if (!match)
   {
    Check(false, "a restored scripted soldier stands near a saved spot");
    continue;
   }
   float off = vector.DistanceXZ(returnedControl.GetAnchor(), match.m_vAnchor);
   float dot = vector.Dot(returnedControl.GetForward(), match.m_vForward);
   PrintFormat("[EXPG EUS CDF ROUNDTRIP UNIT] script='%1' saved='%2' anchorOff=%3 headingDot=%4", EUS_Codes.Describe(returnedControl.GetCode()), EUS_Codes.Describe(match.m_iCode), off, dot);
   if (returnedControl.GetCode() == match.m_iCode && off <= 0.4 && dot >= 0.95) Restored++;
   if (EUS_Codes.IsAnimation(returnedControl.GetCode()) && Loitering(returned)) Posed++;
  }
  Check(Restored == 3, "the three saved scripts are back on their spots and headings");
  Check(Posed == 1, "the restored Smoke pose plays");
  Check(Plain == 1, "the returned without a script has none after the load");
  // Items now and right after CDF's inventory restore (compared with CDF alone in 5).
  Check(Settle(false) == 4, "CDF applied the state of the four saved soldiers");
  foreach (EUS_CdfExpect expect : Expect)
  {
   if (expect.m_sLoaded == expect.m_sApplied) continue;
   PrintFormat("[EXPG EUS CDF ROUNDTRIP ITEMS] unit='%1' changed after CDF applied his state: missing=%2 extra=%3", Label(expect), Missing(expect.m_sApplied, expect.m_sLoaded), Missing(expect.m_sLoaded, expect.m_sApplied));
  }
  // 3. A broken envelope: refused, nothing changes.
  foreach (CDF_GMSaveEntityRecord broken : Broken.m_aEntities)
  {
   string brokenPayload;
   string brokenOriginal;
   if (EUS_CDF.Locate(broken.m_sState, 1, brokenPayload, brokenOriginal) != 1) continue;
   JsonSaveContext context = new JsonSaveContext();
   context.WriteValue(EUS_CDF.KEY, brokenPayload);
   broken.m_sState = context.SaveToString();
   break;
  }
  bool refused = !CDF_GMSaveRestore.Restore(Broken);
  if (Check(refused && Bound() == 3, "a document whose envelope lost its CDF state is refused before anything changes")) BrokenRefused = 1;
  // 4. An invalid script payload: the load goes on without that script.
  foreach (CDF_GMSaveEntityRecord invalid : Invalid.m_aEntities)
  {
   if (!invalid.m_sState.Contains(EUS_CDF.KEY)) continue;
   invalid.m_sState.Replace("\\\"version\\\":1", "\\\"version\\\":9");
   break;
  }
  ResetApplied();
  if (!Check(CDF_GMSaveRestore.Restore(Invalid), "a document with one invalid script payload loads"))
  {
   Finish("invalid");
   return;
  }
  Advance(4);
 }
 void CheckInvalid()
 {
  if (Bound() < 2 || Manager.CountPendingRestores() > 0 || Now() - PhaseStarted < 6)
  {
   Waited(EUS_Manager.RESTORE_WAIT + 20, string.Format("two scripts restored after the invalid load (%1)", Bound()));
   return;
  }
  if (Check(Bound() == 2 && EUS_CDF.Skipped == 1, string.Format("exactly the invalid script is skipped (bound %1, skipped %2)", Bound(), EUS_CDF.Skipped))) InvalidSkipped = 1;
  // 5. Control: the same document without unit script envelopes, through CDF alone.
  int stripped;
  foreach (CDF_GMSaveEntityRecord record : m_Stripped.m_aEntities)
  {
   string payload;
   string original;
   if (EUS_CDF.Locate(record.m_sState, 1, payload, original) != 1) continue;
   record.m_sState = original;
   stripped++;
  }
  Check(stripped == 3, string.Format("the control document lost its three unit script envelopes (%1)", stripped));
  ResetApplied();
  if (!Check(CDF_GMSaveRestore.Restore(m_Stripped), "the control document without unit scripts loads"))
  {
   Finish("control");
   return;
  }
  Advance(5);
 }
 void CheckControl()
 {
  int applied;
  if (s_aApplied) applied = s_aApplied.Count();
  if (applied < 4 || Now() - PhaseStarted < SETTLE)
  {
   Waited(40, string.Format("CDF applied the four soldier states of the control load within 40 seconds (%1)", applied));
   return;
  }
  Check(Bound() == 0 && Manager.CountPendingRestores() == 0, "the control load restores no unit script");
  Check(Settle(true) == 4, "CDF applied the state of the four saved soldiers in the control load");
  foreach (EUS_CdfExpect expect : Expect)
  {
   string label = Label(expect);
   if (expect.m_bLoaded && expect.m_bControl && expect.m_sLoaded == expect.m_sControl) Inventories++;
   else PrintFormat("[EXPG EUS CDF ROUNDTRIP ITEMS] unit='%1' differs from CDF alone: missing=%2 extra=%3", label, Missing(expect.m_sControl, expect.m_sLoaded), Missing(expect.m_sLoaded, expect.m_sControl));
   if (expect.m_sControl == expect.m_sItems) continue;
   m_iCdfLoss++;
   PrintFormat("[EXPG EUS CDF ROUNDTRIP CDF ITEMS] unit='%1' CDF alone, against the save: missing=%2 extra=%3; right after CDF applied the state: missing=%4", label, Missing(expect.m_sItems, expect.m_sControl), Missing(expect.m_sControl, expect.m_sItems), Missing(expect.m_sItems, expect.m_sControlApplied));
  }
  Check(Inventories == 4, string.Format("every returned carries exactly the items CDF alone gives him without unit scripts (%1 of 4)", Inventories));
  Finish("completed");
 }
}

// TEST ONLY. Notes each character's items once CDF applied his state (deep state pass),
// whichever way the adapters wrap this call.
modded class CDF_GMSaveState
{
 override static void Apply(IEntity entity, string state)
 {
  super.Apply(entity, state);
  EXPG_CdfRoundTrip.NoteApplied(entity);
 }
}
