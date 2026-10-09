// EXPBG CDF Compat - Prisoners (EXPBG AI Surrender and ACE Captives). Copyright 2026 ExpBG Tech.
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 knows nothing about prisoners. A soldier who surrendered to
// EXPBG AI Surrender (GM Tools ESR_SurrenderManager) left his squad without an author, so
// CDF neither saved nor cleared him; one with an author came back as an armed, active
// soldier of his side (prefab weapons stay in CDF's equipment slots). ACE Captives state
// (surrendered, handcuffed, escorted) lives on the character controller and in ACE's
// animation helper compartments, which CDF does not see either. Until 0.1.10 prisoners
// were session-only. This bridge saves them and gives them back as prisoners.
//
// Save:
//  - CDF_GMSaveState.Capture (wrapped) puts an "eprPrisoner" envelope on each living,
//    non-player prisoner it captures: EXPBG prisoner record (side, attempts, answer, reveal,
//    intel answers, squad overrides, dossier, the squad he surrendered from, ACE fallback)
//    and/or ACE Captives flags, the weapons he holds and whether his AI was on. His squad's
//    record gets an "eprSquad" token so the load can link him back to it.
//  - A prisoner of a CDF-managed squad (recorded at surrender) is added to the document
//    as a root record built like CDF's own (CaptureEntity's ShouldCapture is bound
//    statically and cannot be extended). The record is added while CDF's Capture is
//    still running (its closing GetEntityCount call), so every adapter's document checks
//    see it; if that call ever moves, it is added right after CDF's Capture instead.
//  - A prisoner sitting in a real vehicle is saved on foot beside it (seats belong to
//    CDF's vehicles, never to captives; see IsCaptiveSeat).
// Load:
//  - A structurally broken envelope is refused before CDF clears the scene (WARNING; the
//    Game Master sees why). An unreadable payload only loses that prisoner's state: he is
//    held passive and counted as failed.
//  - Each prisoner record is held the moment CDF spawns it (AI off, civilian side), so a
//    restored prisoner never fights or is fought over while CDF finishes.
//  - CDF_GMSaveState.Apply (wrapped) unwraps before super, so inventory, damage and the
//    other envelopes reach CDF and the other adapters unchanged, then queues the prisoner.
//  - After CDF's deferred state pass, at most PER_PUMP prisoners per PUMP_MS: weapons
//    beyond the save are deleted (never dropped), EXPBG prisoners surrender again through
//    ESR_SurrenderManager.Surrender (interrogation point, pose, passive AI) and get their
//    saved record back; ACE states are re-applied through ACE's own controller methods.
//  - One summary per load: [EXPBG CDF PRISONERS] restored=N esr=A ace=B failed=F ...
// Old saves (no envelopes): unchanged behaviour. A load clears the prisoners of
// CDF-managed squads and releases their interrogation points; prisoners of mission
// squads stay, like their squads. Players are never touched.
// ACE is optional: no ACE class is named in code. GM Tools' ESR_AceCaptives detects it at
// runtime; the captive call and the helper type are resolved by name.
class EPR_State
{
 static const int VERSION = 1;
 static const int MAX_LIST = 16;
 static const int MAX_TEXT = 4096;
 int m_iVersion = VERSION;
 bool m_bEsr;
 string m_sSide;
 int m_iAttempts;
 int m_iOutcome = -1;
 int m_iRevealCount;
 int m_iRevealDistance;
 int m_iRevealBearing;
 bool m_bIntelRolled;
 ref array<int> m_aIntelDistances = {};
 ref array<int> m_aIntelBearings = {};
 ref array<int> m_aSquadOverrides = {};
 bool m_bAceFailed;
 ref ESR_Dossier m_Dossier;
 int m_iSquad = -1;
 ref array<string> m_aArmed = {};
 bool m_bAceSurrendered;
 bool m_bAceCaptive;
 bool m_bAceCarried;
 bool m_bAiActive;

 bool HasAce()
 {
  return m_bAceSurrendered || m_bAceCaptive || m_bAceCarried;
 }

 // Empty when the state is not valid.
 string Encode()
 {
  if (!Validate().IsEmpty())
  {
   return string.Empty;
  }
  ESR_Dossier dossier = m_Dossier;
  if (!dossier) dossier = new ESR_Dossier();
  JsonSaveContext context = new JsonSaveContext();
  bool written = context.WriteValue("v", m_iVersion) && context.WriteValue("esr", m_bEsr) && context.WriteValue("side", m_sSide);
  written = written && context.WriteValue("att", m_iAttempts) && context.WriteValue("out", m_iOutcome) && context.WriteValue("rc", m_iRevealCount);
  written = written && context.WriteValue("rd", m_iRevealDistance) && context.WriteValue("rb", m_iRevealBearing) && context.WriteValue("ir", m_bIntelRolled);
  written = written && context.WriteValue("idist", m_aIntelDistances) && context.WriteValue("ibear", m_aIntelBearings) && context.WriteValue("sqo", m_aSquadOverrides);
  written = written && context.WriteValue("aceFail", m_bAceFailed) && context.WriteValue("squad", m_iSquad) && context.WriteValue("armed", m_aArmed);
  written = written && context.WriteValue("as", m_bAceSurrendered) && context.WriteValue("ac", m_bAceCaptive) && context.WriteValue("acr", m_bAceCarried) && context.WriteValue("ai", m_bAiActive);
  written = written && context.WriteValue("dnf", dossier.NameFormat) && context.WriteValue("dn", dossier.Name) && context.WriteValue("da", dossier.Alias) && context.WriteValue("ds", dossier.Surname);
  written = written && context.WriteValue("dbio", dossier.Bio) && context.WriteValue("dorg", dossier.Origin) && context.WriteValue("dage", dossier.Age) && context.WriteValue("dldr", dossier.Leader);
  written = written && context.WriteValue("dlf", dossier.LeaderFormat) && context.WriteValue("dln", dossier.LeaderName) && context.WriteValue("dla", dossier.LeaderAlias) && context.WriteValue("dls", dossier.LeaderSurname);
  if (!written)
  {
   return string.Empty;
  }
  return context.SaveToString();
 }

 // Empty when valid, else the reason.
 string Validate()
 {
  if (m_iVersion != VERSION)
  {
   return string.Format("unknown prisoner state version %1", m_iVersion);
  }
  if (!m_bEsr && !HasAce())
  {
   return "neither an EXPBG prisoner nor an ACE captive";
  }
  if (m_iOutcome < -1 || m_iOutcome > ESR_SurrenderManager.OUTCOME_NO_SQUAD || m_iAttempts < 0 || m_iAttempts > 1000)
  {
   return "interrogation progress out of range";
  }
  if (m_iRevealBearing < 0 || m_iRevealBearing > 7 || m_iRevealCount < 0 || m_iRevealDistance < 0)
  {
   return "revealed squad out of range";
  }
  if (!m_aIntelDistances || !m_aIntelBearings || !m_aSquadOverrides || !m_aArmed)
  {
   return "missing list";
  }
  if (m_aIntelDistances.Count() != m_aIntelBearings.Count() || m_aIntelDistances.Count() > MAX_LIST || m_aArmed.Count() > MAX_LIST)
  {
   return "list too long or uneven";
  }
  if (!m_aSquadOverrides.IsEmpty() && m_aSquadOverrides.Count() != ESR_Overrides.COUNT)
  {
   return "squad overrides of the wrong size";
  }
  foreach (int bearing : m_aIntelBearings)
  {
   if (bearing < 0 || bearing > 7)
   {
    return "intel bearing out of range";
   }
  }
  if (m_iSquad < -1 || m_sSide.Length() > MAX_TEXT)
  {
   return "squad token or side out of range";
  }
  if (m_Dossier && (m_Dossier.Bio.Length() > MAX_TEXT || m_Dossier.Name.Length() > MAX_TEXT || m_Dossier.Origin.Length() > MAX_TEXT))
  {
   return "dossier text too long";
  }
  return string.Empty;
 }

 static EPR_State Decode(string text, out string reason)
 {
  JsonLoadContext context = new JsonLoadContext();
  if (text.IsEmpty() || !context.LoadFromString(text))
  {
   reason = "unreadable prisoner state";
   return null;
  }
  int version;
  if (!context.ReadValue("v", version) || version != VERSION)
  {
   reason = string.Format("unknown prisoner state version %1", version);
   return null;
  }
  bool esr;
  string side;
  int attempts;
  int outcome;
  int revealCount;
  int revealDistance;
  int revealBearing;
  bool intelRolled;
  array<int> distances = {};
  array<int> bearings = {};
  array<int> overrides = {};
  bool aceFailed;
  int squad;
  array<string> armed = {};
  bool aceSurrendered;
  bool aceCaptive;
  bool aceCarried;
  bool aiActive;
  bool read = context.ReadValue("esr", esr) && context.ReadValue("side", side) && context.ReadValue("att", attempts) && context.ReadValue("out", outcome);
  read = read && context.ReadValue("rc", revealCount) && context.ReadValue("rd", revealDistance) && context.ReadValue("rb", revealBearing) && context.ReadValue("ir", intelRolled);
  read = read && context.ReadValue("idist", distances) && context.ReadValue("ibear", bearings) && context.ReadValue("sqo", overrides) && context.ReadValue("aceFail", aceFailed);
  read = read && context.ReadValue("squad", squad) && context.ReadValue("armed", armed) && context.ReadValue("as", aceSurrendered) && context.ReadValue("ac", aceCaptive);
  read = read && context.ReadValue("acr", aceCarried) && context.ReadValue("ai", aiActive);
  ESR_Dossier dossier = new ESR_Dossier();
  string nameFormat;
  string name;
  string alias;
  string surname;
  string bio;
  string origin;
  int age;
  bool leader;
  string leaderFormat;
  string leaderName;
  string leaderAlias;
  string leaderSurname;
  read = read && context.ReadValue("dnf", nameFormat) && context.ReadValue("dn", name) && context.ReadValue("da", alias) && context.ReadValue("ds", surname);
  read = read && context.ReadValue("dbio", bio) && context.ReadValue("dorg", origin) && context.ReadValue("dage", age) && context.ReadValue("dldr", leader);
  read = read && context.ReadValue("dlf", leaderFormat) && context.ReadValue("dln", leaderName) && context.ReadValue("dla", leaderAlias) && context.ReadValue("dls", leaderSurname);
  if (!read || !distances || !bearings || !overrides || !armed)
  {
   reason = "incomplete prisoner state";
   return null;
  }
  EPR_State state = new EPR_State();
  state.m_bEsr = esr;
  state.m_sSide = side;
  state.m_iAttempts = attempts;
  state.m_iOutcome = outcome;
  state.m_iRevealCount = revealCount;
  state.m_iRevealDistance = revealDistance;
  state.m_iRevealBearing = revealBearing;
  state.m_bIntelRolled = intelRolled;
  foreach (int distance : distances) state.m_aIntelDistances.Insert(distance);
  foreach (int bearing : bearings) state.m_aIntelBearings.Insert(bearing);
  foreach (int value : overrides) state.m_aSquadOverrides.Insert(value);
  state.m_bAceFailed = aceFailed;
  state.m_iSquad = squad;
  foreach (string weapon : armed) state.m_aArmed.Insert(weapon);
  state.m_bAceSurrendered = aceSurrendered;
  state.m_bAceCaptive = aceCaptive;
  state.m_bAceCarried = aceCarried;
  state.m_bAiActive = aiActive;
  dossier.NameFormat = nameFormat;
  dossier.Name = name;
  dossier.Alias = alias;
  dossier.Surname = surname;
  dossier.Bio = bio;
  dossier.Origin = origin;
  dossier.Age = age;
  dossier.Leader = leader;
  dossier.LeaderFormat = leaderFormat;
  dossier.LeaderName = leaderName;
  dossier.LeaderAlias = leaderAlias;
  dossier.LeaderSurname = leaderSurname;
  state.m_Dossier = dossier;
  reason = state.Validate();
  if (!reason.IsEmpty())
  {
   return null;
  }
  return state;
 }

 static ESR_Dossier CopyDossier(ESR_Dossier source)
 {
  if (!source)
  {
   return null;
  }
  ESR_Dossier copy = new ESR_Dossier();
  copy.NameFormat = source.NameFormat;
  copy.Name = source.Name;
  copy.Alias = source.Alias;
  copy.Surname = source.Surname;
  copy.Bio = source.Bio;
  copy.Origin = source.Origin;
  copy.Age = source.Age;
  copy.Leader = source.Leader;
  copy.LeaderFormat = source.LeaderFormat;
  copy.LeaderName = source.LeaderName;
  copy.LeaderAlias = source.LeaderAlias;
  copy.LeaderSurname = source.LeaderSurname;
  return copy;
 }
}

// One restored prisoner waiting for the pump.
class EPR_Entry
{
 IEntity m_Entity;
 ref EPR_State m_State;
 string m_sReason;
 int m_iTries;
 int m_iDue;
}

class EPR_CDF
{
 static const string KEY = "eprPrisoner";
 static const string SQUAD_KEY = "eprSquad";
 static const string INNER = "cdfState";
 static const int SQUAD_VERSION = 1;
 // Envelopes this one may sit under in a stored state.
 static const int PEEL_LIMIT = 8;
 static const int MAX_TRACKED = 256;
 static const int FIRST_PUMP_MS = 600;
 static const int PUMP_MS = 100;
 static const int PER_PUMP = 4;
 static const int RETRY_MS = 500;
 // About 30 s for a prisoner to wake up or leave a seat, and for CDF's deferred pass.
 static const int MAX_TRIES = 60;
 static const int MAX_WAITS = 300;
 static const int REASONS_LIMIT = 4;
 static const float VEHICLE_CLEARANCE = 1.5;
 static const string FN_SET_CAPTIVE = "ACE_Captives_SetCaptive";
 static const string TYPE_TOGGLE_CAPTIVE = "ACE_Captives_ToggleCaptiveContextAction";

 // Prisoners whose squad CDF managed when they surrendered, and prisoners a CDF load gave
 // back (weak; pruned). Clear removes them and a save keeps them.
 protected static ref array<SCR_ChimeraCharacter> s_aManaged;
 protected static bool s_bHelperResolved;
 protected static typename s_tHelper;

 // Capture in progress.
 static bool s_bCapturing;
 static bool s_bInjectArmed;
 static bool s_bCaptureFailed;
 static string s_sCaptureReason;
 static int s_iInjected;
 protected static ref array<SCR_AIGroup> s_aSquadTokens;

 // Load in progress.
 static bool s_bSpawning;
 protected static ref array<ref CDF_GMSaveEntityRecord> s_aHold;
 protected static ref array<ref EPR_Entry> s_aPending;
 protected static ref map<int, SCR_AIGroup> s_mSquads;
 static bool s_bActive;
 protected static bool s_bScheduled;
 protected static int s_iWaits;
 static int s_iExpected, s_iRestored, s_iEsr, s_iAce, s_iFailed, s_iHeld, s_iSquads, s_iReleased;
 protected static int s_iNamed;
 protected static string s_sReasons;
 // Finished passes and the last pass's totals (native fixture).
 static int s_iPasses, s_iLastRestored, s_iLastEsr, s_iLastAce, s_iLastFailed, s_iLastExpected, s_iLastHeld, s_iLastSquads;

 //------------------------------------------------------------------------------------------------
 // Captive seats (shared rule with the vehicle crew bridge)
 //------------------------------------------------------------------------------------------------
 // True when this character belongs to the prisoner bridge, not to a vehicle crew: he sits
 // in one of ACE's animation helper compartments (surrendered, tied, escorted; not an
 // editable vehicle), or he is an EXPBG AI Surrender prisoner, or ACE marks him surrendered,
 // captive or carried, wherever he sits. A crew bridge skips such a character entirely:
 // this bridge saves him on foot (beside a real vehicle) and re-applies his prisoner state.
 // Server for the ACE flags; the helper and EXPBG checks work on any machine.
 static bool IsCaptiveSeat(IEntity character)
 {
  SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
  if (!chimera)
  {
   return false;
  }
  if (InAceHelper(chimera))
  {
   return true;
  }
  if (ESR_SurrenderManager.PrisonerCount() > 0 && ESR_SurrenderManager.FindPrisoner(chimera))
  {
   return true;
  }
  bool surrendered;
  bool captive;
  bool carried;
  if (!ESR_AceCaptives.ReadState(chimera, surrendered, captive, carried))
  {
   return false;
  }
  return surrendered || captive || carried;
 }

 // His direct parent is an ACE animation helper (type resolved by name; false without ACE).
 static bool InAceHelper(IEntity character)
 {
  if (!character)
  {
   return false;
  }
  IEntity parent = character.GetParent();
  if (!parent)
  {
   return false;
  }
  typename helper = HelperType();
  if (!helper)
  {
   return false;
  }
  return parent.IsInherited(helper);
 }

 protected static typename HelperType()
 {
  if (!s_bHelperResolved)
  {
   s_bHelperResolved = true;
   string helperName = ESR_AceCaptives.TYPE_HELPER;
   s_tHelper = helperName.ToType();
  }
  return s_tHelper;
 }

 //------------------------------------------------------------------------------------------------
 // Managed prisoners (clear predicate)
 //------------------------------------------------------------------------------------------------
 protected static void EnsureManaged()
 {
  if (!s_aManaged) s_aManaged = {};
 }

 static void Prune()
 {
  EnsureManaged();
  for (int i = s_aManaged.Count() - 1; i >= 0; i--)
  {
   if (!s_aManaged[i]) s_aManaged.Remove(i);
  }
 }

 static void Track(SCR_ChimeraCharacter character)
 {
  EnsureManaged();
  if (!character || s_aManaged.Contains(character))
  {
   return;
  }
  if (s_aManaged.Count() >= MAX_TRACKED) Prune();
  if (s_aManaged.Count() >= MAX_TRACKED) s_aManaged.RemoveOrdered(0);
  s_aManaged.Insert(character);
 }

 static bool IsTracked(IEntity character)
 {
  EnsureManaged();
  SCR_ChimeraCharacter chimera = SCR_ChimeraCharacter.Cast(character);
  return chimera && s_aManaged.Contains(chimera);
 }

 // Clear asks for every editable entity: only characters can be prisoners.
 static ESR_Prisoner Find(SCR_EditableEntityComponent entity)
 {
  if (!entity || ESR_SurrenderManager.PrisonerCount() == 0 || entity.GetEntityType() != EEditableEntityType.CHARACTER)
  {
   return null;
  }
  return ESR_SurrenderManager.FindPrisoner(entity.GetOwner());
 }

 // Server, when the interrogation point of a new prisoner is set up: his squad still
 // exists (an emptied group deletes itself one frame later).
 static void NoteSurrender(SCR_ChimeraCharacter character)
 {
  if (!character || !Replication.IsServer() || IsTracked(character))
  {
   return;
  }
  ESR_Prisoner prisoner = ESR_SurrenderManager.FindPrisoner(character);
  if (!prisoner || !prisoner.Group)
  {
   return;
  }
  SCR_EditableEntityComponent squad = SCR_EditableEntityComponent.GetEditableEntity(prisoner.Group);
  if (!squad || !CDF_GMSaveCapture.IsManaged(squad))
  {
   return;
  }
  Track(character);
 }

 // Clear predicate for a prisoner CDF would otherwise keep. Never a player or possessed body.
 static bool ManagedPrisoner(SCR_EditableEntityComponent entity)
 {
  ESR_Prisoner prisoner = Find(entity);
  if (!prisoner || !prisoner.Character || ESR_SurrenderManager.IsPlayerCharacter(prisoner.Character))
  {
   return false;
  }
  if (IsTracked(prisoner.Character))
  {
   return true;
  }
  if (!prisoner.Group)
  {
   return false;
  }
  SCR_EditableEntityComponent squad = SCR_EditableEntityComponent.GetEditableEntity(prisoner.Group);
  return squad && squad != entity && CDF_GMSaveCapture.IsManaged(squad);
 }

 // Prisoner records whose character still exists (at most MAX_PRISONERS).
 static int LiveCount()
 {
  int live = 0;
  for (int i = ESR_SurrenderManager.PrisonerCount() - 1; i >= 0; i--)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && prisoner.Character) live++;
  }
  return live;
 }

 // After CDF's restore: release the records of prisoners the load removed, so their
 // interrogation points go now instead of at the next 5 s upkeep. When the load restores
 // the first surrender module, that module has already released them as stale, so the
 // count compares living records before and after the load (restored prisoners surrender
 // again only later, in the pump).
 static void ReleaseRemoved(int liveBefore)
 {
  for (int i = ESR_SurrenderManager.PrisonerCount() - 1; i >= 0; i--)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && !prisoner.Character) ESR_SurrenderManager.Release(prisoner, "removed by a CDF load");
  }
  Prune();
  int removed = liveBefore - LiveCount();
  if (removed > 0) s_iReleased += removed;
 }

 //------------------------------------------------------------------------------------------------
 // Envelopes
 //------------------------------------------------------------------------------------------------
 // 1: envelope found (payload and the state it wraps); 0: none; -1: envelope without its
 // wrapped state, or a declared key that is not a string. levels 1 reads only the
 // outermost state (Apply); document checks also look under other adapters' "cdfState".
 static int Locate(string state, string key, int levels, out string payload, out string original)
 {
  string text = state;
  for (int depth = 0; depth < levels; depth++)
  {
   if (!text.Contains(key))
   {
    return 0;
   }
   JsonLoadContext context = new JsonLoadContext();
   if (!context.LoadFromString(text))
   {
    return 0;
   }
   if (context.ReadValue(key, payload))
   {
    if (context.ReadValue(INNER, original))
    {
     return 1;
    }
    return -1;
   }
   // A wrongly typed key is still this envelope: refuse it rather than hand the whole
   // envelope to CDF as plain state. Quotes inside nested states are escaped, so a quoted
   // name followed by a colon at this level is a key.
   if (text.Contains("\"" + key + "\":"))
   {
    return -1;
   }
   string inner;
   if (!context.ReadValue(INNER, inner))
   {
    return 0;
   }
   text = inner;
  }
  return 0;
 }

 static string EncodeSquad(int token)
 {
  JsonSaveContext context = new JsonSaveContext();
  if (!context.WriteValue("v", SQUAD_VERSION) || !context.WriteValue("t", token))
  {
   return string.Empty;
  }
  return context.SaveToString();
 }

 // The squad token, or -1.
 static int DecodeSquad(string payload)
 {
  JsonLoadContext context = new JsonLoadContext();
  int version;
  int token = -1;
  if (payload.IsEmpty() || !context.LoadFromString(payload) || !context.ReadValue("v", version) || version != SQUAD_VERSION || !context.ReadValue("t", token))
  {
   return -1;
  }
  if (token < 0)
  {
   return -1;
  }
  return token;
 }

 //------------------------------------------------------------------------------------------------
 // Capture
 //------------------------------------------------------------------------------------------------
 // Before CDF's Capture: the squads living prisoners surrendered from get tokens first, so
 // a squad record captured before its prisoner is still marked.
 static void BeginCapture()
 {
  s_bCaptureFailed = false;
  s_sCaptureReason = "";
  s_iInjected = 0;
  s_aSquadTokens = {};
  s_bCapturing = true;
  s_bInjectArmed = true;
  for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && prisoner.Character && prisoner.Group && !s_aSquadTokens.Contains(prisoner.Group)) s_aSquadTokens.Insert(prisoner.Group);
  }
 }

 static void EndCapture(CDF_GMSaveDocument document)
 {
  // CDF's Capture did not reach its closing GetEntityCount call: add the records now.
  if (s_bInjectArmed && document) Inject(document);
  s_bInjectArmed = false;
  s_bCapturing = false;
 }

 static void FailCapture(string reason)
 {
  if (!s_bCaptureFailed) s_sCaptureReason = reason;
  s_bCaptureFailed = true;
 }

 // The envelope payload for one captured entity and its key; empty when it has none.
 static string EnvelopeFor(IEntity entity, out string key)
 {
  key = string.Empty;
  if (!entity)
  {
   return string.Empty;
  }
  SCR_AIGroup group = SCR_AIGroup.Cast(entity);
  if (group)
  {
   if (!s_bCapturing || !s_aSquadTokens || s_aSquadTokens.IsEmpty())
   {
    return string.Empty;
   }
   int token = s_aSquadTokens.Find(group);
   if (token < 0)
   {
    return string.Empty;
   }
   string squadPayload = EncodeSquad(token);
   if (squadPayload.IsEmpty())
   {
    FailCapture("A prisoner squad token could not be encoded");
    return string.Empty;
   }
   key = SQUAD_KEY;
   return squadPayload;
  }
  EPR_State state = CaptureState(entity);
  if (!state)
  {
   return string.Empty;
  }
  string payload = state.Encode();
  if (payload.IsEmpty())
  {
   FailCapture("A prisoner state failed validation or could not be encoded: " + state.Validate());
   return string.Empty;
  }
  key = KEY;
  return payload;
 }

 // A living, non-player EXPBG prisoner or ACE captive; null for everyone else.
 static EPR_State CaptureState(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character)
  {
   return null;
  }
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.GetLifeState() == ECharacterLifeState.DEAD || ESR_SurrenderManager.IsPlayerCharacter(character))
  {
   return null;
  }
  ESR_Prisoner prisoner;
  if (ESR_SurrenderManager.PrisonerCount() > 0) prisoner = ESR_SurrenderManager.FindPrisoner(character);
  bool surrendered;
  bool captive;
  bool carried;
  bool ace = AceCandidate(character) && ESR_AceCaptives.ReadState(character, surrendered, captive, carried) && (surrendered || captive || carried);
  if (!prisoner && !ace)
  {
   return null;
  }
  EPR_State state = new EPR_State();
  if (prisoner)
  {
   state.m_bEsr = true;
   state.m_sSide = prisoner.SideKey;
   state.m_iAttempts = prisoner.Attempts;
   state.m_iOutcome = prisoner.Outcome;
   state.m_iRevealCount = prisoner.RevealCount;
   state.m_iRevealDistance = prisoner.RevealDistance;
   state.m_iRevealBearing = prisoner.RevealBearing;
   state.m_bIntelRolled = prisoner.IntelRolled;
   if (prisoner.IntelDistances && prisoner.IntelBearings && prisoner.IntelDistances.Count() == prisoner.IntelBearings.Count())
   {
    foreach (int distance : prisoner.IntelDistances) state.m_aIntelDistances.Insert(distance);
    foreach (int bearing : prisoner.IntelBearings) state.m_aIntelBearings.Insert(bearing);
   }
   if (prisoner.SquadOverrides && prisoner.SquadOverrides.Count() == ESR_Overrides.COUNT)
   {
    foreach (int value : prisoner.SquadOverrides) state.m_aSquadOverrides.Insert(value);
   }
   state.m_bAceFailed = prisoner.AceFailed;
   state.m_Dossier = EPR_State.CopyDossier(prisoner.Dossier);
   if (prisoner.Group && s_bCapturing && s_aSquadTokens) state.m_iSquad = s_aSquadTokens.Find(prisoner.Group);
  }
  state.m_bAceSurrendered = ace && surrendered;
  state.m_bAceCaptive = ace && captive;
  state.m_bAceCarried = ace && carried;
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  state.m_bAiActive = control && control.IsAIActivated();
  Armed(character, state.m_aArmed);
  return state;
 }

 // ACE flags are read only where ACE could have set them: in its helper, or on a
 // civilian side (ACE makes captives and surrendered soldiers "CIV").
 protected static bool AceCandidate(SCR_ChimeraCharacter character)
 {
  if (!ESR_AceCaptives.Available())
  {
   return false;
  }
  if (InAceHelper(character))
  {
   return true;
  }
  Faction faction = character.GetFaction();
  if (!faction)
  {
   return true;
  }
  SCR_Faction scripted = SCR_Faction.Cast(faction);
  return faction.GetFactionKey() == ESR_SurrenderManager.CIVILIAN_FACTION || (scripted && !scripted.IsMilitary());
 }

 // Prefabs of the weapons in his weapon slots (the list AI Surrender drops).
 static void Armed(SCR_ChimeraCharacter character, notnull array<string> prefabs)
 {
  prefabs.Clear();
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
  if (!weapons)
  {
   return;
  }
  array<IEntity> carried = {};
  weapons.GetWeaponsList(carried);
  foreach (IEntity weapon : carried)
  {
   if (!weapon || prefabs.Count() >= EPR_State.MAX_LIST) continue;
   string prefab = CDF_GMSaveState.GetPrefabName(weapon);
   if (!prefab.IsEmpty()) prefabs.Insert(prefab);
  }
 }

 // Adds a root record for every living prisoner of a CDF-managed squad CDF did not
 // capture itself (no author): the same predicate decides that Clear removes him.
 static void Inject(CDF_GMSaveDocument document)
 {
  s_bInjectArmed = false;
  if (!document || !document.m_aEntities || ESR_SurrenderManager.PrisonerCount() == 0)
  {
   return;
  }
  set<SCR_EditableEntityComponent> present = new set<SCR_EditableEntityComponent>();
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (record && record.m_Entity) present.Insert(record.m_Entity);
  }
  for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (!prisoner || !prisoner.Character || ESR_SurrenderManager.IsPlayerCharacter(prisoner.Character)) continue;
   CharacterControllerComponent controller = prisoner.Character.GetCharacterController();
   if (!controller || controller.GetLifeState() == ECharacterLifeState.DEAD) continue;
   SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(prisoner.Character);
   if (!editable || present.Contains(editable) || editable.HasEntityFlag(EEditableEntityFlag.NON_DELETABLE)) continue;
   if (!CDF_GMSaveCapture.IsManaged(editable)) continue;
   CDF_GMSaveEntityRecord added = BuildRecord(editable);
   if (!added) continue;
   document.m_aEntities.Insert(added);
   present.Insert(editable);
   s_iInjected++;
  }
 }

 // A root record exactly as CDF_GMSaveCapture.CaptureEntity writes one.
 static CDF_GMSaveEntityRecord BuildRecord(SCR_EditableEntityComponent editable)
 {
  IEntity owner = editable.GetOwner();
  SCR_EditableEntityComponent target;
  int targetValue = CDF_GMSaveEntityRecord.TARGET_NONE;
  EEditableEntitySaveFlag saveFlags = 0;
  if (!owner || !editable.Serialize(target, targetValue, saveFlags))
  {
   return null;
  }
  CDF_GMSaveEntityRecord record = new CDF_GMSaveEntityRecord();
  record.m_Entity = editable;
  record.m_iParent = -1;
  record.m_sPrefab = editable.GetPrefab(true);
  record.m_iSaveFlags = saveFlags;
  record.m_iEntityFlags = editable.GetEntityFlags();
  record.m_iEntityType = editable.GetEntityType();
  record.m_fScale = owner.GetScale();
  record.m_bDirtyHierarchy = editable.HasEntityFlag(EEditableEntityFlag.INDIVIDUAL_CHILDREN) || editable.HasEntityFlag(EEditableEntityFlag.DIRTY_HIERARCHY);
  vector transform[4];
  owner.GetWorldTransform(transform);
  record.m_vPosition = transform[3];
  float quat[4];
  Math3D.MatrixToQuat(transform, quat);
  record.m_fQuatX = quat[0];
  record.m_fQuatY = quat[1];
  record.m_fQuatZ = quat[2];
  record.m_fQuatW = quat[3];
  record.m_iTargetValue = targetValue;
  record.m_sAuthorUID = editable.GetAuthorUID();
  record.m_sAuthorPlatformID = editable.GetAuthorPlatformID();
  record.m_iAuthorPlatform = editable.GetAuthorPlatform();
  record.m_iAuthorUpdated = editable.GetAuthorLastUpdated();
  if (CDF_GMSaveConfig.GetInstance().m_bSaveAttributes) CDF_GMSaveAttributes.Read(editable, record.m_aAttributeIds, record.m_aAttributeX, record.m_aAttributeY, record.m_aAttributeZ);
  record.m_sState = CDF_GMSaveState.Capture(owner);
  return record;
 }

 // Prisoners sitting in a real vehicle are saved standing beside it: CDF has no seat for
 // them and the crew bridge skips captives (IsCaptiveSeat). Returns how many moved.
 static int MoveOutOfVehicles(CDF_GMSaveDocument document)
 {
  int moved = 0;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record || !record.m_Entity || !record.m_sState.Contains(KEY)) continue;
   string payload;
   string original;
   if (Locate(record.m_sState, KEY, PEEL_LIMIT, payload, original) != 1) continue;
   IEntity owner = record.m_Entity.GetOwner();
   if (!owner || InAceHelper(owner)) continue;
   IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(owner);
   if (!vehicle || vehicle == owner) continue;
   vector mins;
   vector maxs;
   vehicle.GetWorldBounds(mins, maxs);
   vector center = (mins + maxs) * 0.5;
   float reach = Math.Max(maxs[0] - mins[0], maxs[2] - mins[2]) * 0.5 + VEHICLE_CLEARANCE;
   vector right = vehicle.GetWorldTransformAxis(0);
   right[1] = 0;
   right.Normalize();
   vector forward = vehicle.GetWorldTransformAxis(2);
   forward[1] = 0;
   forward.Normalize();
   // Side by side along the vehicle, one metre apart.
   int slot = moved - (moved / 5) * 5;
   float shift = slot - 2;
   vector spot = center + right * reach + forward * shift;
   spot[1] = GetGame().GetWorld().GetSurfaceY(spot[0], spot[2]);
   vector angles = vehicle.GetYawPitchRoll();
   vector upright[4];
   Math3D.AnglesToMatrix(Vector(angles[0], 0, 0), upright);
   float quat[4];
   Math3D.MatrixToQuat(upright, quat);
   record.m_vPosition = spot;
   record.m_fQuatX = quat[0];
   record.m_fQuatY = quat[1];
   record.m_fQuatZ = quat[2];
   record.m_fQuatW = quat[3];
   moved++;
  }
  return moved;
 }

 //------------------------------------------------------------------------------------------------
 // Document checks (save and load)
 //------------------------------------------------------------------------------------------------
 // Every envelope is structurally sound. A save also requires every payload to decode and
 // to sit on the right record type; a load counts the rest at Apply. Intel Items state
 // hidden under a prisoner envelope passes Intel's own check (it reads only the outermost
 // state). With hold, the load's prisoner records are remembered for the spawn-time hold.
 static bool ValidDocument(CDF_GMSaveDocument document, bool forLoad, out int prisoners, out int squads, out string reason)
 {
  prisoners = 0;
  squads = 0;
  if (!document || !document.m_aEntities)
  {
   reason = "The save has no entity list";
   return false;
  }
  CDF_GMSaveDocument carriers = new CDF_GMSaveDocument();
  foreach (int index, CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record || !record.m_sState.Contains("epr")) continue;
   string payload;
   string original;
   int found = Locate(record.m_sState, KEY, PEEL_LIMIT, payload, original);
   if (found < 0)
   {
    reason = string.Format("Invalid prisoner envelope on record %1 (%2)", index, record.m_sPrefab);
    return false;
   }
   if (found > 0)
   {
    bool character = record.m_iEntityType < 0 || record.m_iEntityType == EEditableEntityType.CHARACTER;
    string why;
    EPR_State state = EPR_State.Decode(payload, why);
    if (!forLoad && (!state || !character))
    {
     reason = string.Format("Prisoner on record %1 (%2) cannot be saved: %3", index, record.m_sPrefab, why);
     return false;
    }
    // CDF destroys a saved wreck or corpse at once and never applies its state.
    if (!SCR_Enum.HasFlag(record.m_iSaveFlags, EEditableEntitySaveFlag.DESTROYED))
    {
     prisoners++;
     if (forLoad) s_aHold.Insert(record);
    }
    if (original.IsEmpty()) continue;
    CDF_GMSaveEntityRecord carried = new CDF_GMSaveEntityRecord();
    carried.m_sPrefab = record.m_sPrefab;
    carried.m_sState = original;
    carriers.m_aEntities.Insert(carried);
    continue;
   }
   string squadPayload;
   string squadOriginal;
   int squadFound = Locate(record.m_sState, SQUAD_KEY, PEEL_LIMIT, squadPayload, squadOriginal);
   if (squadFound < 0)
   {
    reason = string.Format("Invalid prisoner squad envelope on record %1 (%2)", index, record.m_sPrefab);
    return false;
   }
   if (squadFound == 0) continue;
   bool group = record.m_iEntityType < 0 || record.m_iEntityType == EEditableEntityType.GROUP;
   if (!forLoad && (DecodeSquad(squadPayload) < 0 || !group))
   {
    reason = string.Format("Prisoner squad token on record %1 (%2) cannot be saved", index, record.m_sPrefab);
    return false;
   }
   squads++;
  }
  if (!carriers.m_aEntities.IsEmpty() && !EII_CDFPayload.ValidDocument(carriers))
  {
   reason = "Intel Items state (carried intel or USB drives) of a prisoner failed Intel Items validation";
   return false;
  }
  return true;
 }

 static void ReportCapture(CDF_GMSaveDocument document, int prisoners, int squads, int moved)
 {
  int left = 0;
  set<IEntity> saved = new set<IEntity>();
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (record && record.m_Entity && record.m_Entity.GetOwner()) saved.Insert(record.m_Entity.GetOwner());
  }
  for (int i = 0; i < ESR_SurrenderManager.PrisonerCount(); i++)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && prisoner.Character && !saved.Contains(prisoner.Character)) left++;
  }
  if (prisoners == 0 && left == 0)
  {
   return;
  }
  PrintFormat("[EXPBG CDF PRISONERS] saved=%1 squads=%2 added=%3 movedFromVehicles=%4 notSaved=%5 (prisoners of mission squads stay in the world through a load)", prisoners, squads, s_iInjected, moved, left);
 }

 //------------------------------------------------------------------------------------------------
 // Load
 //------------------------------------------------------------------------------------------------
 protected static void EnsureLoad()
 {
  if (!s_aHold) s_aHold = {};
  if (!s_aPending) s_aPending = {};
  if (!s_mSquads) s_mSquads = new map<int, SCR_AIGroup>();
 }

 protected static void ResetCounters()
 {
  s_iExpected = 0;
  s_iRestored = 0;
  s_iEsr = 0;
  s_iAce = 0;
  s_iFailed = 0;
  s_iHeld = 0;
  s_iSquads = 0;
  s_iReleased = 0;
  s_iWaits = 0;
  s_iNamed = 0;
  s_sReasons = "";
 }

 // Before CDF's Restore: a new load ends the previous pass, then the document is checked.
 static bool PrepareLoad(CDF_GMSaveDocument document, out string reason)
 {
  EnsureLoad();
  if (s_bActive) Finish(true);
  if (GetGame()) GetGame().GetCallqueue().Remove(EPR_CDF.Pump);
  s_bScheduled = false;
  s_aHold.Clear();
  s_aPending.Clear();
  s_mSquads.Clear();
  ResetCounters();
  int prisoners;
  int squads;
  if (!ValidDocument(document, true, prisoners, squads, reason))
  {
   s_aHold.Clear();
   return false;
  }
  s_iExpected = prisoners;
  return true;
 }

 // After CDF's Restore returned: the spawn-time hold ends, the pass waits for CDF's
 // deferred state pass.
 static void EndSpawn(bool result)
 {
  s_bSpawning = false;
  EnsureLoad();
  s_aHold.Clear();
  if (!result)
  {
   return;
  }
  s_bActive = true;
  s_iWaits = 0;
  Schedule(FIRST_PUMP_MS);
 }

 // SCR_EditableCharacterComponent.EOnEditorSessionLoad, while CDF spawns: CDF has bound
 // the record to this entity (SpawnRecord sets m_Entity first).
 static void HoldSpawned(SCR_EditableEntityComponent editable)
 {
  if (!s_aHold || !editable || !Replication.IsServer())
  {
   return;
  }
  foreach (CDF_GMSaveEntityRecord record : s_aHold)
  {
   if (!record || record.m_Entity != editable) continue;
   if (Hold(editable.GetOwner())) s_iHeld++;
   return;
  }
 }

 // AI off and a civilian side until the prisoner state is back. Never a player.
 static bool Hold(IEntity entity)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
  if (!character || ESR_SurrenderManager.IsPlayerCharacter(character))
  {
   return false;
  }
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (control && control.IsAIActivated()) control.DeactivateAI();
  SetCivilian(character);
  return true;
 }

 // AI Surrender's civilian side: "CIV" unless it is military, else the first
 // non-military faction; none keeps his own.
 static bool SetCivilian(SCR_ChimeraCharacter character)
 {
  FactionManager factions = GetGame().GetFactionManager();
  FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(character.FindComponent(FactionAffiliationComponent));
  if (!factions || !affiliation)
  {
   return false;
  }
  Faction civilian = factions.GetFactionByKey(ESR_SurrenderManager.CIVILIAN_FACTION);
  SCR_Faction scripted = SCR_Faction.Cast(civilian);
  if (scripted && scripted.IsMilitary()) civilian = null;
  if (!civilian)
  {
   array<Faction> all = {};
   factions.GetFactionsList(all);
   foreach (Faction candidate : all)
   {
    SCR_Faction candidateScripted = SCR_Faction.Cast(candidate);
    if (!candidateScripted || candidateScripted.IsMilitary()) continue;
    civilian = candidate;
    break;
   }
  }
  if (!civilian)
  {
   return false;
  }
  if (affiliation.GetAffiliatedFaction() != civilian) affiliation.SetAffiliatedFaction(civilian);
  return true;
 }

 // An ACE captive whose state cannot come back is left as CDF alone restores him: his
 // default side and his saved AI state.
 protected static void Unhold(SCR_ChimeraCharacter character, EPR_State state)
 {
  if (!character || ESR_SurrenderManager.IsPlayerCharacter(character))
  {
   return;
  }
  FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(character.FindComponent(FactionAffiliationComponent));
  if (affiliation && affiliation.GetDefaultAffiliatedFaction()) affiliation.SetAffiliatedFaction(affiliation.GetDefaultAffiliatedFaction());
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (control && state && state.m_bAiActive && !control.IsAIActivated()) control.ActivateAI();
 }

 // CDF_GMSaveState.Apply (wrapped), after the inner state was applied.
 static void RegisterSquad(IEntity entity, string payload)
 {
  EnsureLoad();
  SCR_AIGroup group = SCR_AIGroup.Cast(entity);
  int token = DecodeSquad(payload);
  if (!group || token < 0 || s_mSquads.Contains(token))
  {
   return;
  }
  s_mSquads.Set(token, group);
  s_iSquads++;
 }

 static void Queue(IEntity entity, string payload)
 {
  EnsureLoad();
  if (!s_bActive)
  {
   // Applied outside a prepared load: a pass of its own.
   ResetCounters();
   s_bActive = true;
   Schedule(PUMP_MS);
  }
  Hold(entity);
  EPR_Entry entry = new EPR_Entry();
  entry.m_Entity = entity;
  string reason;
  entry.m_State = EPR_State.Decode(payload, reason);
  entry.m_sReason = reason;
  s_aPending.Insert(entry);
 }

 protected static void Schedule(int delay)
 {
  if (s_bScheduled || !GetGame())
  {
   return;
  }
  s_bScheduled = true;
  GetGame().GetCallqueue().CallLater(EPR_CDF.Pump, delay, false);
 }

 // Bounded: at most PER_PUMP prisoners every PUMP_MS, after CDF's deferred state pass.
 static void Pump()
 {
  s_bScheduled = false;
  if (!s_bActive)
  {
   return;
  }
  EnsureLoad();
  bool deferredDone = CDF_GMSaveRestore.EPR_DeferredDone();
  if (!deferredDone && s_iWaits < MAX_WAITS)
  {
   s_iWaits++;
   Schedule(PUMP_MS);
   return;
  }
  int now = System.GetTickCount();
  int worked = 0;
  int index = 0;
  while (index < s_aPending.Count() && worked < PER_PUMP)
  {
   EPR_Entry entry = s_aPending[index];
   if (entry.m_iDue - now > 0)
   {
    index++;
    continue;
   }
   worked++;
   string reason;
   int outcome = RestoreEntry(entry, reason);
   if (outcome == 0)
   {
    entry.m_iTries++;
    if (entry.m_iTries < MAX_TRIES)
    {
     entry.m_iDue = now + RETRY_MS;
     index++;
     continue;
    }
    Fail(entry, "not ready after retries: " + reason);
   }
   else if (outcome > 0)
   {
    s_iRestored++;
   }
   else
   {
    Fail(entry, reason);
   }
   s_aPending.RemoveOrdered(index);
  }
  if (s_aPending.IsEmpty())
  {
   Finish(false);
   return;
  }
  Schedule(PUMP_MS);
 }

 protected static void Fail(EPR_Entry entry, string reason)
 {
  s_iFailed++;
  s_iNamed++;
  if (s_iNamed <= REASONS_LIMIT)
  {
   if (!s_sReasons.IsEmpty()) s_sReasons += "; ";
   s_sReasons += reason;
  }
  // An EXPBG prisoner who could not surrender again stays held (passive, civilian).
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entry.m_Entity);
  if (character && entry.m_State && !entry.m_State.m_bEsr) Unhold(character, entry.m_State);
 }

 // 1 restored, 0 not yet (retried later), -1 failed (reason).
 protected static int RestoreEntry(EPR_Entry entry, out string reason)
 {
  SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entry.m_Entity);
  if (!character || character.IsDeleted())
  {
   reason = "deleted before restore";
   return -1;
  }
  if (ESR_SurrenderManager.IsPlayerCharacter(character))
  {
   reason = "player-controlled";
   return -1;
  }
  if (!entry.m_State)
  {
   reason = "unreadable saved state (" + entry.m_sReason + ")";
   return -1;
  }
  CharacterControllerComponent controller = character.GetCharacterController();
  if (!controller || controller.GetLifeState() == ECharacterLifeState.DEAD)
  {
   reason = "dead";
   return -1;
  }
  if (entry.m_State.m_bEsr)
  {
   return RestoreEsr(character, controller, entry.m_State, reason);
  }
  return RestoreAce(character, controller, entry.m_State, reason);
 }

 protected static int RestoreEsr(SCR_ChimeraCharacter character, CharacterControllerComponent controller, EPR_State state, out string reason)
 {
  ESR_Prisoner prisoner = ESR_SurrenderManager.FindPrisoner(character);
  if (!prisoner)
  {
   // AI Surrender takes only an awake soldier on foot.
   if (controller.GetLifeState() != ECharacterLifeState.ALIVE || character.IsInVehicle())
   {
    reason = "down or in a vehicle";
    return 0;
   }
   RemoveWeapons(character, state.m_aArmed);
   if (!ESR_SurrenderManager.Surrender(character, ESR_SurrenderManager.GroupOf(character)))
   {
    reason = "AI Surrender refused him";
    return 0;
   }
   prisoner = ESR_SurrenderManager.FindPrisoner(character);
   if (!prisoner)
   {
    reason = "no prisoner record after surrender";
    return -1;
   }
  }
  ApplyEsr(character, prisoner, state);
  Track(character);
  s_iEsr++;
  // Tied (or escorted) through ACE: tied again; ACE-surrendered prisoners take ACE's pose
  // through AI Surrender itself (its pose step runs 1.5 s after the surrender).
  if ((state.m_bAceCaptive || state.m_bAceCarried) && SetAceCaptive(character)) s_iAce++;
  return 1;
 }

 protected static int RestoreAce(SCR_ChimeraCharacter character, CharacterControllerComponent controller, EPR_State state, out string reason)
 {
  if (!ESR_AceCaptives.Available())
  {
   reason = "ACE Captives is not loaded";
   return -1;
  }
  RemoveWeapons(character, state.m_aArmed);
  bool applied;
  if (state.m_bAceCaptive || state.m_bAceCarried)
  {
   applied = SetAceCaptive(character);
  }
  else
  {
   if (controller.GetLifeState() != ECharacterLifeState.ALIVE)
   {
    reason = "down";
    return 0;
   }
   applied = SetAceSurrender(character);
  }
  if (!applied)
  {
   reason = "ACE Captives did not take him";
   return 0;
  }
  AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
  if (control && state.m_bAiActive && !control.IsAIActivated()) control.ActivateAI();
  s_iAce++;
  return 1;
 }

 // The saved prisoner record over the fresh one AI Surrender made (its new map markers
 // are not saved: answers repeat without new markers).
 protected static void ApplyEsr(SCR_ChimeraCharacter character, ESR_Prisoner prisoner, EPR_State state)
 {
  if (!state.m_sSide.IsEmpty()) prisoner.SideKey = state.m_sSide;
  prisoner.Attempts = state.m_iAttempts;
  prisoner.Outcome = state.m_iOutcome;
  prisoner.RevealCount = state.m_iRevealCount;
  prisoner.RevealDistance = state.m_iRevealDistance;
  prisoner.RevealBearing = state.m_iRevealBearing;
  prisoner.MarkerId = -1;
  prisoner.IntelRolled = state.m_bIntelRolled;
  if (!prisoner.IntelDistances) prisoner.IntelDistances = {};
  if (!prisoner.IntelBearings) prisoner.IntelBearings = {};
  if (!prisoner.IntelMarkers) prisoner.IntelMarkers = {};
  prisoner.IntelDistances.Clear();
  prisoner.IntelBearings.Clear();
  prisoner.IntelMarkers.Clear();
  foreach (int distance : state.m_aIntelDistances)
  {
   prisoner.IntelDistances.Insert(distance);
   prisoner.IntelMarkers.Insert(-1);
  }
  foreach (int bearing : state.m_aIntelBearings) prisoner.IntelBearings.Insert(bearing);
  if (state.m_aSquadOverrides.Count() == ESR_Overrides.COUNT)
  {
   if (!prisoner.SquadOverrides) prisoner.SquadOverrides = {};
   prisoner.SquadOverrides.Clear();
   foreach (int value : state.m_aSquadOverrides) prisoner.SquadOverrides.Insert(value);
  }
  if (state.m_bAceFailed) prisoner.AceFailed = true;
  SCR_AIGroup squad;
  if (state.m_iSquad >= 0 && s_mSquads) s_mSquads.Find(state.m_iSquad, squad);
  prisoner.Group = squad;
  if (state.m_Dossier)
  {
   prisoner.Dossier = EPR_State.CopyDossier(state.m_Dossier);
   RplComponent rpl = character.GetRplComponent();
   if (rpl && prisoner.Point) prisoner.Point.Setup(rpl.Id(), prisoner.Dossier);
  }
 }

 // Weapons in his slots beyond the saved ones are deleted, never dropped: CDF keeps the
 // prefab's weapons in its equipment slots, the prisoner had dropped his.
 static int RemoveWeapons(SCR_ChimeraCharacter character, notnull array<string> saved)
 {
  BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
  InventoryStorageManagerComponent inventory = InventoryStorageManagerComponent.Cast(character.FindComponent(InventoryStorageManagerComponent));
  if (!weapons || !inventory)
  {
   return 0;
  }
  array<string> allowed = {};
  foreach (string prefab : saved) allowed.Insert(prefab);
  array<IEntity> carried = {};
  weapons.GetWeaponsList(carried);
  int removed = 0;
  foreach (IEntity weapon : carried)
  {
   if (!weapon) continue;
   int at = allowed.Find(CDF_GMSaveState.GetPrefabName(weapon));
   if (at >= 0)
   {
    allowed.Remove(at);
    continue;
   }
   if (inventory.TryDeleteItem(weapon)) removed++;
  }
  return removed;
 }

 //------------------------------------------------------------------------------------------------
 // ACE Captives (runtime only)
 //------------------------------------------------------------------------------------------------
 // ACE's own captive call (ACE_Captives_SetCaptive(true): registers him with ACE's captive
 // system, civilian side, tied pose), by name; ACE's GM "Toggle captive" action as a
 // fallback. Judged by ACE's captive flag.
 static bool SetAceCaptive(SCR_ChimeraCharacter character)
 {
  if (!character || !Replication.IsServer() || !ESR_AceCaptives.Available())
  {
   return false;
  }
  bool surrendered;
  bool captive;
  bool carried;
  if (ESR_AceCaptives.ReadState(character, surrendered, captive, carried) && captive)
  {
   return true;
  }
  CharacterControllerComponent controller = character.GetCharacterController();
  ScriptModule scripts = GetGame().GetScriptModule();
  if (controller && scripts)
  {
   // ACE's method returns nothing; the slot only satisfies the call signature.
   int unused;
   bool requested = true;
   scripts.Call(controller, FN_SET_CAPTIVE, false, unused, requested);
  }
  if (ESR_AceCaptives.ReadState(character, surrendered, captive, carried) && captive)
  {
   return true;
  }
  string actionName = TYPE_TOGGLE_CAPTIVE;
  typename actionType = actionName.ToType();
  if (!actionType || !actionType.IsInherited(SCR_SelectedEntitiesContextAction))
  {
   return false;
  }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.GetEditableEntity(character);
  if (!editable)
  {
   return false;
  }
  // Keep the spawned instance in a local before casting.
  Managed spawned = actionType.Spawn();
  SCR_SelectedEntitiesContextAction toggle = SCR_SelectedEntitiesContextAction.Cast(spawned);
  if (!toggle || !toggle.CanBePerformed(editable, vector.Zero, 0))
  {
   return false;
  }
  toggle.Perform(editable, vector.Zero);
  return ESR_AceCaptives.ReadState(character, surrendered, captive, carried) && captive;
 }

 // ACE's surrender (helper and hands-up pose) through GM Tools' ACE adapter.
 static bool SetAceSurrender(SCR_ChimeraCharacter character)
 {
  bool surrendered;
  bool captive;
  bool carried;
  if (ESR_AceCaptives.ReadState(character, surrendered, captive, carried) && surrendered && InAceHelper(character))
  {
   return true;
  }
  return ESR_AceCaptives.SetSurrender(character, true);
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
  if (interrupted && s_aPending)
  {
   cut = 1;
   foreach (EPR_Entry pending : s_aPending) Fail(pending, "interrupted by another load");
  }
  if (s_aPending) s_aPending.Clear();
  s_iPasses++;
  s_iLastRestored = s_iRestored;
  s_iLastEsr = s_iEsr;
  s_iLastAce = s_iAce;
  s_iLastFailed = s_iFailed;
  s_iLastExpected = s_iExpected;
  s_iLastHeld = s_iHeld;
  s_iLastSquads = s_iSquads;
  string reasons;
  if (!s_sReasons.IsEmpty()) reasons = " reasons=" + s_sReasons;
  if (s_iNamed > REASONS_LIMIT) reasons += string.Format(" (+%1)", s_iNamed - REASONS_LIMIT);
  LogLevel severity = LogLevel.NORMAL;
  if (s_iFailed > 0 || cut > 0) severity = LogLevel.WARNING;
  Print(string.Format("[EXPBG CDF PRISONERS] restored=%1 esr=%2 ace=%3 failed=%4 expected=%5 held=%6 squads=%7 released=%8 interrupted=%9", s_iRestored, s_iEsr, s_iAce, s_iFailed, s_iExpected, s_iHeld, s_iSquads, s_iReleased, cut) + reasons, severity);
 }

 //------------------------------------------------------------------------------------------------
 // First refusal of the current request, shown to the requesting GM with CDF's feedback.
 static string s_sLastRefusal;
 static int s_iLastRefusalTick;
 static const int REFUSAL_WINDOW_MS = 2000;
 static void Reject(string reason)
 {
  Print("[EXPBG CDF PRISONERS HOLD] " + reason, LogLevel.WARNING);
  int now = System.GetTickCount();
  int age = now - s_iLastRefusalTick;
  if (s_sLastRefusal.IsEmpty() || age < 0 || age > REFUSAL_WINDOW_MS)
  {
   s_sLastRefusal = reason;
   s_iLastRefusalTick = now;
  }
 }
 // CDF sends its save/load result in the same server frame as the refused request.
 static string TakeRefusal()
 {
  string reason = s_sLastRefusal;
  int age = System.GetTickCount() - s_iLastRefusalTick;
  s_sLastRefusal = "";
  if (age < 0 || age > REFUSAL_WINDOW_MS)
  {
   return "";
  }
  if (reason.Length() > 1024) reason = reason.Substring(0, 1024);
  return reason;
 }
}

modded class ESR_InterrogationPoint
{
 override void Setup(RplId prisonerId, ESR_Dossier dossier)
 {
  super.Setup(prisonerId, dossier);
  EPR_CDF.NoteSurrender(GetPrisoner());
 }
}

// CDF spawns a record, binds it (record.m_Entity) and then calls this; the vanilla
// character override activates his AI and skips super.
modded class SCR_EditableCharacterComponent
{
 override void EOnEditorSessionLoad(SCR_EditableEntityComponent parent)
 {
  super.EOnEditorSessionLoad(parent);
  if (EPR_CDF.s_bSpawning) EPR_CDF.HoldSpawned(this);
 }
}

// CDF_GMSaveCapture.Capture calls this on its new document once every record and the
// world state are captured: the last moment to add records that every adapter's
// document checks still see.
modded class CDF_GMSaveDocument
{
 override int GetEntityCount()
 {
  if (EPR_CDF.s_bInjectArmed) EPR_CDF.Inject(this);
  return super.GetEntityCount();
 }
}

modded class CDF_GMSaveState
{
 override static string Capture(IEntity entity)
 {
  string original = super.Capture(entity);
  string key;
  string payload = EPR_CDF.EnvelopeFor(entity, key);
  if (payload.IsEmpty())
  {
   return original;
  }
  JsonSaveContext context = new JsonSaveContext();
  string wrapped;
  if (context.WriteValue(key, payload) && context.WriteValue(EPR_CDF.INNER, original)) wrapped = context.SaveToString();
  if (wrapped.IsEmpty())
  {
   EPR_CDF.FailCapture("A prisoner envelope could not be encoded");
   return original;
  }
  return wrapped;
 }

 override static void Apply(IEntity entity, string state)
 {
  string payload;
  string original;
  int found = 0;
  bool squad = false;
  if (entity && state.Contains("epr"))
  {
   found = EPR_CDF.Locate(state, EPR_CDF.KEY, 1, payload, original);
   if (found == 0)
   {
    found = EPR_CDF.Locate(state, EPR_CDF.SQUAD_KEY, 1, payload, original);
    squad = found != 0;
   }
  }
  if (found == 0)
  {
   super.Apply(entity, state);
   return;
  }
  if (found < 0)
  {
   // Restore refuses such a document before clearing; never guess the wrapped state.
   Print("[EXPBG CDF PRISONERS] Invalid prisoner envelope (wrongly typed or without its CDF state); state not applied", LogLevel.WARNING);
   return;
  }
  super.Apply(entity, original);
  if (squad)
  {
   EPR_CDF.RegisterSquad(entity, payload);
   return;
  }
  EPR_CDF.Queue(entity, payload);
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  EPR_CDF.BeginCapture();
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  EPR_CDF.EndCapture(document);
  // Another adapter refused; keep its refusal.
  if (!document)
  {
   return null;
  }
  if (EPR_CDF.s_bCaptureFailed)
  {
   EPR_CDF.Reject(EPR_CDF.s_sCaptureReason + "; nothing was written");
   return null;
  }
  int prisoners;
  int squads;
  string reason;
  if (!EPR_CDF.ValidDocument(document, false, prisoners, squads, reason))
  {
   EPR_CDF.Reject(reason + "; nothing was written");
   return null;
  }
  int moved = EPR_CDF.MoveOutOfVehicles(document);
  EPR_CDF.ReportCapture(document, prisoners, squads, moved);
  return document;
 }

 // Clear calls this public predicate across classes.
 override static bool IsManaged(SCR_EditableEntityComponent entity)
 {
  if (super.IsManaged(entity))
  {
   return true;
  }
  return EPR_CDF.ManagedPrisoner(entity);
 }
}

modded class CDF_GMSaveRestore
{
 // CDF's deferred state pass (FinishRestore) has run.
 static bool EPR_DeferredDone()
 {
  return !s_RestoredEntities && s_aPendingStates && s_aPendingStates.IsEmpty();
 }

 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  string reason;
  if (!EPR_CDF.PrepareLoad(document, reason))
  {
   EPR_CDF.Reject(reason + "; the scene was not cleared");
   return false;
  }
  int live = EPR_CDF.LiveCount();
  EPR_CDF.s_bSpawning = true;
  bool result = super.Restore(document);
  EPR_CDF.s_bSpawning = false;
  EPR_CDF.ReleaseRemoved(live);
  EPR_CDF.EndSpawn(result);
  return result;
 }
}

// GM-facing refusal: CDF reports save/load results as hints only and never says why an
// adapter refused. Show the prisoner bridge's reason in a CDF-style dialog.
modded class SCR_PlayerController
{
 override protected void CDF_GMSave_Feedback(notnull CDF_GMSaveResult result)
 {
  super.CDF_GMSave_Feedback(result);
  string action;
  if (result.m_sKey == "#CDF_GMSave_Msg_LoadAborted") action = "Load";
  else if (result.m_sKey == "#CDF_GMSave_Msg_CaptureFailed") action = "Save";
  string reason;
  if (!action.IsEmpty()) reason = EPR_CDF.TakeRefusal();
  if (reason.IsEmpty())
  {
   return;
  }
  string message = action + " refused by EXPBG prisoners (AI Surrender / ACE Captives):\n\n" + reason + "\n\nDetails: [EXPBG CDF PRISONERS HOLD] in the server log.";
  // A hosting GM owns this controller locally; an owner RPC would not reach it.
  if (GetGame().GetPlayerController() == this) EPR_CDF_RpcDo_Refused(message);
  else Rpc(EPR_CDF_RpcDo_Refused, message);
 }
 [RplRpc(RplChannel.Reliable, RplRcver.Owner)]
 protected void EPR_CDF_RpcDo_Refused(string message)
 {
  EPR_CDFRefusalDialog.Open(message);
 }
}
class EPR_CDFRefusalDialog : CDF_GMSaveBaseDialog
{
 static void Open(string message)
 {
  if (System.IsConsoleApp() || !GetGame() || !GetGame().GetMenuManager())
  {
   return;
  }
  SCR_ConfigurableDialogUiPreset preset = CDF_GMSaveDialogUtils.CreatePreset("EXPBG_EPR_CDF_REFUSED", "#CDF_GMSave_Hint_Title");
  preset.m_eVisualStyle = EDialogType.WARNING;
  preset.m_sMessage = message;
  preset.m_aButtons.Insert(CDF_GMSaveDialogUtils.CreateButtonPreset(SCR_ConfigurableDialogUi.BUTTON_CANCEL, "#CDF_GMSave_Btn_Close", EConfigurableDialogUiButtonAlign.LEFT, "MenuBack"));
  EPR_CDFRefusalDialog dialog = new EPR_CDFRefusalDialog();
  CreateByPreset(preset, dialog);
 }
}
