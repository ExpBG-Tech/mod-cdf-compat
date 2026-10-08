// TEST ONLY. CDF Game Master Save round trip of EXPBG Unit Scripts (Hold, Freeze, an
// ambient animation) through the EXPBG CDF Compat unit-scripts bridge (EUS_CDFState.c),
// in one diagnostic server.
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EUS_CDFScriptsRoundTrip.c -ExpectResult '\[EXPG EUS CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 saved=3 restored=3 posed=1 plain=1 inventories=4 brokenRefused=1 invalidSkipped=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
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
//     Smoke plays, the fourth soldier has no script, and every soldier's inventory holds
//     the same items as before the save (CDF loadout restore through the envelope).
//  3. A document whose envelope lost its wrapped CDF state is refused before anything
//     changes (the three scripts stay bound).
//  4. A document whose script payload is invalid (other version) loads: that soldier has
//     no script, the two others do; the bridge counts it as skipped.
// Not covered: GM UI and dialogs, a cold server restart, multiplayer and JIP.
class EUS_CdfExpect
{
 int Code;
 vector Anchor;
 vector Forward;
 string Items;
}

class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 420;
 static const string FILE = "$profile:eus-cdf-roundtrip.json";
 vector Point = "4773.46 0 7094.57";
 SCR_AIGroup Team;
 EUS_Manager Manager;
 ref EUS_Report Report = new EUS_Report();
 ref array<ref EUS_CdfExpect> Expect = {};
 ref array<string> PlainItems = {};
 ref CDF_GMSaveDocument Loaded;
 ref CDF_GMSaveDocument Broken;
 ref CDF_GMSaveDocument Invalid;
 int Phase;
 int Checks;
 int Failures;
 int Saved;
 int Restored;
 int Posed;
 int Plain;
 int Inventories;
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
  string second = string.Format("inventories=%1 brokenRefused=%2 invalidSkipped=%3 reason=%4", Inventories, BrokenRefused, InvalidSkipped, reason);
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
 string Items(SCR_ChimeraCharacter soldier)
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
 // The saved expectation whose spot is nearest this soldier (within 1.5 m), or null.
 EUS_CdfExpect Match(SCR_ChimeraCharacter soldier)
 {
  EUS_CdfExpect best;
  float bestDistance = 1.5;
  foreach (EUS_CdfExpect expect : Expect)
  {
   float distance = vector.DistanceXZ(soldier.GetOrigin(), expect.Anchor);
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
  CDF_GMSaveConfig cfg = CDF_GMSaveConfig.GetInstance();
  if (Phase == 0)
  {
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
   return;
  }
  if (Phase == 1)
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
    else PlainItems.Insert(Items(member));
    index++;
   }
   Advance(2);
   return;
  }
  if (Phase == 2)
  {
   if (Now() - PhaseStarted < 6)
   {
    return;
   }
   // 1. Capture.
   array<SCR_ChimeraCharacter> soldiers = {};
   Soldiers(soldiers);
   foreach (SCR_ChimeraCharacter soldier : soldiers)
   {
    EUS_UnitControl control = Manager.FindControl(soldier);
    if (!control) continue;
    EUS_CdfExpect expect = new EUS_CdfExpect();
    expect.Code = control.GetCode();
    expect.Anchor = control.GetAnchor();
    expect.Forward = control.GetForward();
    expect.Items = Items(soldier);
    Expect.Insert(expect);
   }
   CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("eus-cdf-roundtrip", "fixture");
   if (!Check(document != null && Expect.Count() == 3, "CDF captured a document with three scripted soldiers"))
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
   if (!Check(Loaded.LoadFromFile(FILE) && Broken.LoadFromFile(FILE) && Invalid.LoadFromFile(FILE), "document read back from the file"))
   {
    Finish("file");
    return;
   }
   // 2. Load.
   if (!Check(CDF_GMSaveRestore.Restore(Loaded), "CDF restore with clearBeforeLoad"))
   {
    Finish("restore");
    return;
   }
   Advance(3);
   return;
  }
  if (Phase == 3)
  {
   if (Bound() < 3 || Manager.CountPendingRestores() > 0)
   {
    Waited(EUS_Manager.RESTORE_WAIT + 20, string.Format("three restored soldiers run their scripts within the restore wait (%1)", Bound()));
    return;
   }
   if (Now() - PhaseStarted < 8)
   {
    return;
   }
   array<SCR_ChimeraCharacter> back = {};
   Soldiers(back);
   Check(back.Count() == 4, string.Format("CDF restored the four soldiers once (%1)", back.Count()));
   foreach (SCR_ChimeraCharacter returned : back)
   {
    EUS_UnitControl returnedControl = Manager.FindControl(returned);
    string carried = Items(returned);
    if (!returnedControl)
    {
     Plain++;
     if (PlainItems.Contains(carried)) Inventories++;
     else PrintFormat("[EXPG EUS CDF ROUNDTRIP ITEMS] plain soldier now=%1", carried);
     continue;
    }
    EUS_CdfExpect match = Match(returned);
    if (!match)
    {
     Check(false, "a restored scripted soldier stands near a saved spot");
     continue;
    }
    float off = vector.DistanceXZ(returnedControl.GetAnchor(), match.Anchor);
    float dot = vector.Dot(returnedControl.GetForward(), match.Forward);
    PrintFormat("[EXPG EUS CDF ROUNDTRIP UNIT] script='%1' saved='%2' anchorOff=%3 headingDot=%4", EUS_Codes.Describe(returnedControl.GetCode()), EUS_Codes.Describe(match.Code), off, dot);
    if (returnedControl.GetCode() == match.Code && off <= 0.4 && dot >= 0.95) Restored++;
    if (EUS_Codes.IsAnimation(returnedControl.GetCode()) && Loitering(returned)) Posed++;
    if (carried == match.Items) Inventories++;
    else PrintFormat("[EXPG EUS CDF ROUNDTRIP ITEMS] before=%1 now=%2", match.Items, carried);
   }
   Check(Restored == 3, "the three saved scripts are back on their spots and headings");
   Check(Posed == 1, "the restored Smoke pose plays");
   Check(Plain == 1, "the returned without a script has none after the load");
   Check(Inventories == 4, string.Format("every returned carries the items he carried before the save (%1 of 4)", Inventories));
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
   if (!Check(CDF_GMSaveRestore.Restore(Invalid), "a document with one invalid script payload loads"))
   {
    Finish("invalid");
    return;
   }
   Advance(4);
   return;
  }
  if (Phase == 4)
  {
   if (Bound() < 2 || Manager.CountPendingRestores() > 0 || Now() - PhaseStarted < 6)
   {
    Waited(EUS_Manager.RESTORE_WAIT + 20, string.Format("two scripts restored after the invalid load (%1)", Bound()));
    return;
   }
   if (Check(Bound() == 2 && EUS_CDF.Skipped == 1, string.Format("exactly the invalid script is skipped (bound %1, skipped %2)", Bound(), EUS_CDF.Skipped))) InvalidSkipped = 1;
   Finish("completed");
  }
 }
}
