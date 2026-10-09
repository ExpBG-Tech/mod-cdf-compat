// TEST ONLY. CDF Game Master Save round trip of EXPBG AI Surrender prisoners (and, when ACE
// Captives is loaded, one ACE handcuffed soldier) through the EXPBG CDF Compat prisoner
// bridge (addon/ai-surrender-cdf, EPR_CDFPrisoners.c), in one diagnostic server.
// pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EPR_CDFPrisonersRoundTrip.c -ExpectResult '\[EXPG PRISONER CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 saved=(?:2 restored=2 esr=2 ace=skipped held=2|3 restored=3 esr=2 ace=1 held=3) duplicates=0 brokenRefused=1 legacyCleared=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
// The runner loads no ACE: the ACE case then prints one skip line and stays out of the
// pass criteria (ace=skipped). The runner copies this file into its fixture addon; the
// driver class name EXPG_CdfRoundTrip is fixed by the runner's layer.
// Real CDF 1.4.1 code throughout: CDF_GMSaveCapture.Capture, SaveToFile/LoadFromFile,
// CDF_GMSaveRestore.Restore (Clear, spawning, deferred state pass), clearBeforeLoad on.
// One authored vanilla US fire team. Cases, in order:
//  1. Two soldiers surrender through ESR_SurrenderManager.Surrender: one plain, one partly
//     interrogated (attempts, revealed squad, intel answer, squad override, dossier name).
//     With ACE Captives, a third is handcuffed through ACE.
//  2. Capture: each prisoner has exactly one record with an eprPrisoner envelope (they are
//     not authored: the bridge adds them), the team record an eprSquad token.
//  3. A document whose prisoner envelope lost its CDF state is refused before the clear:
//     both prisoners are still there.
//  4. Load: every prisoner record is held (AI off) the moment CDF spawns it. After the
//     bridge's pass: the same prisoners once each (no duplicates, nobody else extra), each
//     an AI Surrender prisoner again with his saved record, squad link, dossier, side,
//     interrogation point, passive AI, no weapons and his pose; the ACE captive tied again.
//  5. Legacy: the same file without prisoner records (a 0.1.10 save) clears the restored
//     prisoners (they are CDF-managed now) and restores nobody as a prisoner.
// Not covered: GM UI, a cold server restart, multiplayer and JIP, prisoners in vehicles,
// ACE escort (a player carrier is never saved), player characters (never touched).
class EPR_FixtureExpect
{
 vector m_vSpot;
 bool m_bEsr;
 bool m_bPosed;
 bool m_bPoint;
 string m_sFaction;
 string m_sRecord;
 string m_sName;
 IEntity m_Actor;
}

class EXPG_CdfRoundTripClass : GenericEntityClass {}
class EXPG_CdfRoundTrip : GenericEntity
{
 static const float FIXTURE_SECONDS = 300;
 // Seconds after the bridge's pass before prisoners are compared (AI Surrender poses 1.5 s
 // after a surrender).
 static const float SETTLE = 3;
 static const string FILE = "$profile:epr-cdf-roundtrip.json";
 vector m_vPoint = "4773.46 0 7094.57";
 SCR_AIGroup m_Team;
 ref array<ref EPR_FixtureExpect> m_aExpect = {};
 ref CDF_GMSaveDocument m_Loaded;
 ref CDF_GMSaveDocument m_Broken;
 ref CDF_GMSaveDocument m_Stripped;
 IEntity m_Free;
 int m_iPhase;
 int m_iChecks;
 int m_iFailures;
 int m_iSaved;
 int m_iRestored;
 int m_iEsr;
 int m_iHeld;
 int m_iDuplicates;
 int m_iBrokenRefused;
 int m_iLegacyCleared;
 int m_iPasses;
 int m_iExpected;
 int m_iSoldiers;
 bool m_bAce;
 float m_fStarted;
 float m_fNext;
 float m_fPhaseStarted;
 float m_fIdleSince;
 bool m_bFinished;

 void EXPG_CdfRoundTrip(IEntitySource src, IEntity parent) { SetEventMask(EntityEvent.INIT | EntityEvent.FRAME); }
 float Now() { return GetGame().GetWorld().GetWorldTime() * 0.001; }
 override void EOnInit(IEntity owner)
 {
  if (!Replication.IsServer())
  {
   ClearEventMask(EntityEvent.FRAME);
   return;
  }
  m_fStarted = Now();
  m_fNext = m_fStarted + 15;
  PrintFormat("[EXPG PRISONER CDF ROUNDTRIP BEGIN] cdf=%1 deadline=%2", CDF_GMSave.VERSION, FIXTURE_SECONDS);
 }
 bool Check(bool value, string label)
 {
  m_iChecks++;
  if (!value) m_iFailures++;
  PrintFormat("[EXPG PRISONER CDF ROUNDTRIP CHECK] pass=%1 %2", value, label);
  return value;
 }
 void Finish(string reason)
 {
  if (m_bFinished)
  {
   return;
  }
  m_bFinished = true;
  ClearEventMask(EntityEvent.FRAME);
  string ace = "skipped";
  int aceRestored = m_iRestored - m_iEsr;
  if (m_bAce) ace = aceRestored.ToString();
  string first = string.Format("checks=%1 failures=%2 saved=%3 restored=%4 esr=%5 ace=%6 held=%7 duplicates=%8", m_iChecks, m_iFailures, m_iSaved, m_iRestored, m_iEsr, ace, m_iHeld, m_iDuplicates);
  string second = string.Format("brokenRefused=%1 legacyCleared=%2 reason=%3", m_iBrokenRefused, m_iLegacyCleared, reason);
  PrintFormat("[EXPG PRISONER CDF ROUNDTRIP RESULT] %1 %2", first, second);
  GetGame().RequestClose();
 }
 void Advance(int phase)
 {
  m_iPhase = phase;
  m_fPhaseStarted = Now();
  m_fIdleSince = 0;
  m_iPasses = EPR_CDF.s_iPasses;
  PrintFormat("[EXPG PRISONER CDF ROUNDTRIP PHASE] phase=%1 elapsed=%2", m_iPhase, Now() - m_fStarted);
 }
 bool Waited(float seconds, string label)
 {
  if (Now() - m_fPhaseStarted <= seconds)
  {
   return false;
  }
  Check(false, label);
  Finish("phase " + m_iPhase.ToString());
  return true;
 }
 // True once a prisoner pass finished after this phase began and SETTLE seconds passed.
 bool PassDone()
 {
  if (EPR_CDF.s_iPasses <= m_iPasses || EPR_CDF.s_bActive)
  {
   m_fIdleSince = 0;
   return false;
  }
  if (m_fIdleSince <= 0) m_fIdleSince = Now();
  return Now() - m_fIdleSince >= SETTLE;
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
  author.Initialize("epr-cdf-roundtrip", "", 0, -1);
  editable.SetAuthor(author);
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (core) core.RegisterAuthorServer(author);
 }
 protected array<IEntity> m_aNear;
 bool CollectSoldier(IEntity entity)
 {
  if (m_aNear && SCR_ChimeraCharacter.Cast(entity)) m_aNear.Insert(entity);
  return true;
 }
 // Living soldiers within 30 m of the team's spot.
 int Soldiers(notnull array<SCR_ChimeraCharacter> found)
 {
  found.Clear();
  array<IEntity> near = {};
  m_aNear = near;
  GetGame().GetWorld().QueryEntitiesBySphere(m_vPoint, 30, CollectSoldier);
  m_aNear = null;
  foreach (IEntity entity : near)
  {
   SCR_ChimeraCharacter soldier = SCR_ChimeraCharacter.Cast(entity);
   if (soldier && !soldier.IsDeleted() && soldier.GetCharacterController() && !soldier.GetCharacterController().IsDead()) found.Insert(soldier);
  }
  return found.Count();
 }
 // AI Surrender prisoners within 30 m of the team's spot.
 int PrisonersNear()
 {
  int count = 0;
  for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && prisoner.Character && vector.DistanceXZ(prisoner.Character.GetOrigin(), m_vPoint) < 30) count++;
  }
  return count;
 }
 // The soldier nearest the spot (within 1.5 m), or null.
 SCR_ChimeraCharacter At(vector spot)
 {
  array<SCR_ChimeraCharacter> soldiers = {};
  Soldiers(soldiers);
  SCR_ChimeraCharacter best;
  float bestDistance = 1.5;
  foreach (SCR_ChimeraCharacter soldier : soldiers)
  {
   float distance = vector.DistanceXZ(soldier.GetOrigin(), spot);
   if (distance >= bestDistance) continue;
   best = soldier;
   bestDistance = distance;
  }
  return best;
 }
 // The AI Surrender prisoner nearest the spot (within 1.5 m), or null.
 SCR_ChimeraCharacter AtPrisoner(vector spot)
 {
  SCR_ChimeraCharacter best;
  float bestDistance = 1.5;
  for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (!prisoner || !prisoner.Character) continue;
   float distance = vector.DistanceXZ(prisoner.Character.GetOrigin(), spot);
   if (distance >= bestDistance) continue;
   best = prisoner.Character;
   bestDistance = distance;
  }
  return best;
 }
 static string FactionKey(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character || !character.GetFaction())
  {
   return "none";
  }
  return character.GetFaction().GetFactionKey();
 }
 static bool Posed(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character)
  {
   return false;
  }
  if (EPR_CDF.InAceHelper(character))
  {
   return true;
  }
  SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.GetCharacterController());
  return controller && controller.IsLoitering();
 }
 static bool AiOn(IEntity entity)
 {
  if (!entity)
  {
   return false;
  }
  AIControlComponent control = AIControlComponent.Cast(entity.FindComponent(AIControlComponent));
  return control && control.IsAIActivated();
 }
 static int Weapons(IEntity entity)
 {
  if (!entity)
  {
   return -1;
  }
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(entity.FindComponent(BaseWeaponManagerComponent));
  if (!weapons)
  {
   return 0;
  }
  array<IEntity> carried = {};
  return weapons.GetWeaponsList(carried);
 }
 static bool AceCaptive(IEntity entity)
 {
  bool surrendered;
  bool captive;
  bool carried;
  return ESR_AceCaptives.ReadState(entity, surrendered, captive, carried) && captive;
 }
 // The saved prisoner record as one comparable line.
 static string Describe(ESR_Prisoner prisoner)
 {
  if (!prisoner)
  {
   return "none";
  }
  string intel;
  int count = prisoner.IntelDistances.Count();
  for (int i = 0; i < count && i < prisoner.IntelBearings.Count(); i++) intel += string.Format("%1@%2;", prisoner.IntelDistances[i], prisoner.IntelBearings[i]);
  string overrides;
  foreach (int value : prisoner.SquadOverrides) overrides += value.ToString() + ",";
  string first = string.Format("side=%1 attempts=%2 outcome=%3 reveal=%4/%5/%6", prisoner.SideKey, prisoner.Attempts, prisoner.Outcome, prisoner.RevealCount, prisoner.RevealDistance, prisoner.RevealBearing);
  return first + string.Format(" intelRolled=%1 intel=%2 overrides=%3 aceFailed=%4", prisoner.IntelRolled, intel, overrides, prisoner.AceFailed);
 }
 static string DossierName(ESR_Prisoner prisoner)
 {
  if (!prisoner || !prisoner.Dossier)
  {
   return "none";
  }
  return prisoner.Dossier.Name + "|" + prisoner.Dossier.Surname;
 }
 override void EOnFrame(IEntity owner, float timeSlice)
 {
  if (m_bFinished || Now() < m_fNext)
  {
   return;
  }
  m_fNext = Now() + 0.5;
  if (Now() - m_fStarted > FIXTURE_SECONDS)
  {
   Check(false, string.Format("%1 second deadline; last phase %2", FIXTURE_SECONDS, m_iPhase));
   Finish("timeout");
   return;
  }
  Step();
 }
 void Step()
 {
  if (m_iPhase == 0)
  {
   Setup();
   return;
  }
  if (m_iPhase == 1)
  {
   MakePrisoners();
   return;
  }
  if (m_iPhase == 2)
  {
   CaptureAndLoad();
   return;
  }
  if (m_iPhase == 3)
  {
   CheckLoaded();
   return;
  }
  if (m_iPhase == 4)
  {
   CheckLegacy();
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
  m_vPoint[1] = GetGame().GetWorld().GetSurfaceY(m_vPoint[0], m_vPoint[2]);
  ResourceName teamName = "{84E5BBAB25EA23E5}Prefabs/Groups/BLUFOR/Group_US_FireTeam.et";
  Resource teamResource = Resource.Load(teamName);
  IEntity teamEntity = GetGame().SpawnEntityPrefab(teamResource, GetGame().GetWorld(), Params(m_vPoint));
  m_Team = SCR_AIGroup.Cast(teamEntity);
  if (!Check(m_Team != null, "authored US fire team spawned"))
  {
   Finish("setup");
   return;
  }
  Author(m_Team);
  m_Team.EBG_Exclude = true;
  m_bAce = ESR_AceCaptives.Available();
  if (!m_bAce) Print("[EXPG PRISONER CDF ROUNDTRIP ACE] skipped: ACE Captives is not loaded in this fixture; the ACE handcuffed case is not part of this run");
  Advance(1);
 }
 // 1. Two prisoners (one partly interrogated) and, with ACE, one handcuffed soldier.
 void MakePrisoners()
 {
  if (m_Team.GetAgentsCount() < 4 || Now() - m_fPhaseStarted < 6)
  {
   Waited(40, "the team has four AI members within 40 seconds");
   return;
  }
  array<AIAgent> agents = {};
  m_Team.GetAgents(agents);
  array<SCR_ChimeraCharacter> members = {};
  foreach (AIAgent agent : agents)
  {
   SCR_ChimeraCharacter member;
   if (agent) member = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
   if (member) members.Insert(member);
  }
  if (!Check(members.Count() >= 4, string.Format("four team members found (%1)", members.Count())))
  {
   Finish("team");
   return;
  }
  SCR_ChimeraCharacter plain = members[1];
  SCR_ChimeraCharacter questioned = members[2];
  m_Free = members[0];
  Check(ESR_SurrenderManager.Surrender(plain, m_Team), "the first soldier surrenders");
  Check(ESR_SurrenderManager.Surrender(questioned, m_Team), "the second soldier surrenders");
  ESR_Prisoner prisoner = ESR_SurrenderManager.FindPrisoner(questioned);
  if (!Check(prisoner != null && ESR_SurrenderManager.FindPrisoner(plain) != null, "both are AI Surrender prisoners"))
  {
   Finish("surrender");
   return;
  }
  // Partly interrogated: two questions asked, his squad revealed, one intel item pointed out.
  prisoner.Attempts = 2;
  prisoner.Outcome = ESR_SurrenderManager.OUTCOME_REVEAL;
  prisoner.RevealCount = 3;
  prisoner.RevealDistance = 150;
  prisoner.RevealBearing = 2;
  prisoner.IntelRolled = true;
  prisoner.IntelDistances.Clear();
  prisoner.IntelBearings.Clear();
  prisoner.IntelMarkers.Clear();
  prisoner.IntelDistances.Insert(75);
  prisoner.IntelBearings.Insert(1);
  prisoner.IntelMarkers.Insert(-1);
  prisoner.SquadOverrides.Clear();
  for (int slot = 0; slot < ESR_Overrides.COUNT; slot++) prisoner.SquadOverrides.Insert(-1);
  prisoner.SquadOverrides[ESR_Overrides.REVEAL] = 40;
  if (prisoner.Dossier) prisoner.Dossier.Name = "Fixture";
  Check(EPR_CDF.IsCaptiveSeat(plain) && !EPR_CDF.IsCaptiveSeat(m_Free), "IsCaptiveSeat: true for a prisoner, false for a free soldier");
  if (m_bAce)
  {
   Check(EPR_CDF.SetAceCaptive(members[3]), "ACE Captives handcuffs the third soldier");
   EPR_FixtureExpect captive = new EPR_FixtureExpect();
   captive.m_Actor = members[3];
   m_aExpect.Insert(captive);
  }
  EPR_FixtureExpect first = new EPR_FixtureExpect();
  first.m_Actor = plain;
  first.m_bEsr = true;
  m_aExpect.Insert(first);
  EPR_FixtureExpect second = new EPR_FixtureExpect();
  second.m_Actor = questioned;
  second.m_bEsr = true;
  m_aExpect.Insert(second);
  Advance(2);
 }
 // 2.-4. Snapshot, capture, broken refusal, load.
 void CaptureAndLoad()
 {
  if (Now() - m_fPhaseStarted < SETTLE)
  {
   return;
  }
  foreach (EPR_FixtureExpect expect : m_aExpect)
  {
   expect.m_vSpot = expect.m_Actor.GetOrigin();
   expect.m_bPosed = Posed(expect.m_Actor);
   expect.m_sFaction = FactionKey(expect.m_Actor);
   if (!expect.m_bEsr) continue;
   ESR_Prisoner prisoner = ESR_SurrenderManager.FindPrisoner(expect.m_Actor);
   expect.m_sRecord = Describe(prisoner);
   expect.m_sName = DossierName(prisoner);
   expect.m_bPoint = prisoner && prisoner.Point;
   PrintFormat("[EXPG PRISONER CDF ROUNDTRIP SAVED] %1 posed=%2 faction=%3 point=%4 name=%5", expect.m_sRecord, expect.m_bPosed, expect.m_sFaction, expect.m_bPoint, expect.m_sName);
  }
  m_iExpected = m_aExpect.Count();
  CDF_GMSaveDocument document = CDF_GMSaveCapture.Capture("epr-cdf-roundtrip", "fixture");
  if (!Check(document != null, "CDF captured a document"))
  {
   Finish("capture");
   return;
  }
  int squads = 0;
  int teamTokens = 0;
  foreach (EPR_FixtureExpect saved : m_aExpect)
  {
   int records = 0;
   foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
   {
    if (!record.m_Entity || record.m_Entity.GetOwner() != saved.m_Actor) continue;
    records++;
    string payload;
    string original;
    if (EPR_CDF.Locate(record.m_sState, EPR_CDF.KEY, EPR_CDF.PEEL_LIMIT, payload, original) == 1) m_iSaved++;
   }
   Check(records == 1, string.Format("one record per prisoner (%1)", records));
  }
  foreach (CDF_GMSaveEntityRecord squadRecord : document.m_aEntities)
  {
   string squadPayload;
   string squadOriginal;
   if (EPR_CDF.Locate(squadRecord.m_sState, EPR_CDF.SQUAD_KEY, EPR_CDF.PEEL_LIMIT, squadPayload, squadOriginal) != 1) continue;
   squads++;
   if (squadRecord.m_Entity && squadRecord.m_Entity.GetOwner() == m_Team) teamTokens++;
  }
  Check(m_iSaved == m_iExpected, string.Format("every prisoner carries a prisoner envelope (%1 of %2)", m_iSaved, m_iExpected));
  Check(squads == 1 && teamTokens == 1, string.Format("the team record carries the squad token (%1 squads, team %2)", squads, teamTokens));
  if (!Check(document.SaveToFile(FILE), "document written to " + FILE))
  {
   Finish("file");
   return;
  }
  m_Loaded = new CDF_GMSaveDocument();
  m_Broken = new CDF_GMSaveDocument();
  m_Stripped = new CDF_GMSaveDocument();
  if (!Check(m_Loaded.LoadFromFile(FILE) && m_Broken.LoadFromFile(FILE) && m_Stripped.LoadFromFile(FILE), "document read back from the file three times"))
  {
   Finish("file");
   return;
  }
  // 3. A prisoner envelope without its CDF state: refused before the clear.
  int broken = 0;
  foreach (CDF_GMSaveEntityRecord brokenRecord : m_Broken.m_aEntities)
  {
   string brokenPayload;
   string brokenOriginal;
   if (broken > 0 || EPR_CDF.Locate(brokenRecord.m_sState, EPR_CDF.KEY, EPR_CDF.PEEL_LIMIT, brokenPayload, brokenOriginal) != 1) continue;
   brokenRecord.m_sState = "{\"eprPrisoner\":\"x\"}";
   broken++;
  }
  bool refused = !CDF_GMSaveRestore.Restore(m_Broken);
  int stillThere = 0;
  foreach (EPR_FixtureExpect kept : m_aExpect)
  {
   if (kept.m_Actor && !kept.m_Actor.IsDeleted()) stillThere++;
  }
  if (Check(broken == 1 && refused && stillThere == m_iExpected, string.Format("a broken prisoner envelope is refused before the clear (%1 prisoners still there)", stillThere))) m_iBrokenRefused = 1;
  // 4. Load.
  Advance(3);
  if (!Check(CDF_GMSaveRestore.Restore(m_Loaded), "CDF restore with clearBeforeLoad"))
  {
   Finish("restore");
   return;
  }
  // CDF bound each record to its spawned entity (record.m_Entity) before the hold ran.
  m_iHeld = EPR_CDF.s_iHeld;
  int active = 0;
  int spawnedPrisoners = 0;
  foreach (CDF_GMSaveEntityRecord loadedRecord : m_Loaded.m_aEntities)
  {
   string loadedPayload;
   string loadedOriginal;
   if (!loadedRecord.m_Entity || EPR_CDF.Locate(loadedRecord.m_sState, EPR_CDF.KEY, EPR_CDF.PEEL_LIMIT, loadedPayload, loadedOriginal) != 1) continue;
   spawnedPrisoners++;
   if (AiOn(loadedRecord.m_Entity.GetOwner())) active++;
  }
  Check(m_iHeld == m_iExpected && spawnedPrisoners == m_iExpected && active == 0, string.Format("every prisoner record is held passive as CDF spawns it (%1 held, %2 spawned, %3 with AI on)", m_iHeld, spawnedPrisoners, active));
 }
 void CheckLoaded()
 {
  if (!PassDone())
  {
   Waited(60, "the prisoner pass finished within 60 seconds");
   return;
  }
  m_iRestored = EPR_CDF.s_iLastRestored;
  m_iEsr = EPR_CDF.s_iLastEsr;
  Check(EPR_CDF.s_iLastFailed == 0 && EPR_CDF.s_iLastExpected == m_iExpected, string.Format("the pass restored every expected prisoner without failure (restored %1 expected %2 failed %3)", m_iRestored, EPR_CDF.s_iLastExpected, EPR_CDF.s_iLastFailed));
  array<SCR_ChimeraCharacter> soldiers = {};
  int found = Soldiers(soldiers);
  if (found > 4) m_iDuplicates = found - 4;
  Check(found == 4, string.Format("four soldiers near the team, nobody twice (%1)", found));
  Check(PrisonersNear() == 2, string.Format("two AI Surrender prisoners near the team (%1)", PrisonersNear()));
  SCR_AIGroup team;
  foreach (SCR_ChimeraCharacter soldier : soldiers)
  {
   if (ESR_SurrenderManager.FindPrisoner(soldier) || AceCaptive(soldier)) continue;
   team = ESR_SurrenderManager.GroupOf(soldier);
   if (team) break;
  }
  Check(team != null && team != m_Team, "the team came back as a new squad");
  foreach (EPR_FixtureExpect expect : m_aExpect)
  {
   SCR_ChimeraCharacter actor;
   if (expect.m_bEsr) actor = AtPrisoner(expect.m_vSpot);
   else actor = At(expect.m_vSpot);
   if (!Check(actor != null, "a soldier stands on the saved prisoner spot"))
   {
    continue;
   }
   Check(Weapons(actor) == 0 || !expect.m_bEsr, string.Format("not re-armed (%1 weapons)", Weapons(actor)));
   Check(FactionKey(actor) == expect.m_sFaction, string.Format("same side as saved (%1 against %2)", FactionKey(actor), expect.m_sFaction));
   Check(Posed(actor) || !expect.m_bPosed, "the saved pose is back");
   if (!expect.m_bEsr)
   {
    Check(AceCaptive(actor), "the ACE handcuffed soldier is captive again");
    continue;
   }
   ESR_Prisoner prisoner = ESR_SurrenderManager.FindPrisoner(actor);
   if (!Check(prisoner != null, "the soldier is an AI Surrender prisoner again"))
   {
    continue;
   }
   string record = Describe(prisoner);
   Check(record == expect.m_sRecord, "same prisoner record: " + record + " against " + expect.m_sRecord);
   Check(DossierName(prisoner) == expect.m_sName, string.Format("same dossier (%1 against %2)", DossierName(prisoner), expect.m_sName));
   Check(prisoner.Group == team && team != null, "linked to the squad he surrendered from");
   Check(prisoner.Point != null || !expect.m_bPoint, "his interrogation point is back");
   Check(!AiOn(actor), "his AI stays off");
   Check(EPR_CDF.IsCaptiveSeat(actor), "IsCaptiveSeat is true for the restored prisoner");
  }
  // 5. Legacy: the same save without prisoner records (and ACE envelopes unwrapped).
  array<int> remap = {};
  array<ref CDF_GMSaveEntityRecord> kept = {};
  foreach (CDF_GMSaveEntityRecord entry : m_Stripped.m_aEntities)
  {
   string payload;
   string original;
   bool esr = false;
   if (EPR_CDF.Locate(entry.m_sState, EPR_CDF.KEY, EPR_CDF.PEEL_LIMIT, payload, original) == 1)
   {
    string why;
    EPR_State state = EPR_State.Decode(payload, why);
    esr = state && state.m_bEsr;
    if (!esr && EPR_CDF.Locate(entry.m_sState, EPR_CDF.KEY, 1, payload, original) == 1) entry.m_sState = original;
   }
   else if (EPR_CDF.Locate(entry.m_sState, EPR_CDF.SQUAD_KEY, 1, payload, original) == 1)
   {
    entry.m_sState = original;
   }
   if (esr)
   {
    remap.Insert(-1);
    continue;
   }
   remap.Insert(kept.Count());
   kept.Insert(entry);
  }
  foreach (CDF_GMSaveEntityRecord left : kept)
  {
   if (left.m_iParent >= 0) left.m_iParent = remap[left.m_iParent];
   if (left.m_iTarget >= 0) left.m_iTarget = remap[left.m_iTarget];
  }
  m_Stripped.m_aEntities = kept;
  Advance(4);
  if (!Check(CDF_GMSaveRestore.Restore(m_Stripped), "CDF restore of the save without prisoner records"))
  {
   Finish("legacy");
  }
 }
 void CheckLegacy()
 {
  if (!PassDone())
  {
   Waited(60, "the legacy load's prisoner pass finished within 60 seconds");
   return;
  }
  array<SCR_ChimeraCharacter> soldiers = {};
  int found = Soldiers(soldiers);
  // The two soldiers who stayed in the team (an ACE captive among them loads as CDF alone
  // restores him: the stripped save holds no prisoner envelope).
  bool cleared = PrisonersNear() == 0 && found == 2 && EPR_CDF.s_iLastRestored == 0;
  if (Check(cleared, string.Format("the legacy load cleared the restored prisoners and restored none (%1 prisoners, %2 soldiers, restored %3)", PrisonersNear(), found, EPR_CDF.s_iLastRestored))) m_iLegacyCleared = 1;
  Finish("completed");
 }
}
