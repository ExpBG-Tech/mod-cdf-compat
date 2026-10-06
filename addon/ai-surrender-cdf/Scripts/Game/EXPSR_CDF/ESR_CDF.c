// EXPBG CDF Compat - AI Surrender. Copyright 2026 ExpBG Tech (M.Pac and K.Edgar).
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 keeps the AI Surrender module and its ten settings through
// the module's session attributes; interrogation points carry no editable component and
// are never saved. A prisoner, however, has no CDF representation: dropped weapons,
// civilian side, passive AI, seated pose, interrogation point and answers are live state.
// - A prisoner who carries an author (placed alone or moved by a Game Master) was saved
//   as an ordinary character and restored as an unarmed, active soldier of his side.
// - A prisoner of a Game Master squad left the squad's hierarchy without an author, so
//   CDF neither saved nor cleared him: loading a save taken before his surrender restored
//   him inside his squad next to his own prisoner copy.
// Under CDF prisoners are session-only. They never enter a document, and a CDF load
// removes the prisoners whose squad CDF manages (recorded at surrender, because an
// emptied squad deletes itself). Prisoners of mission squads stay, like their squads.
class ESR_CDF
{
 static const int MAX_TRACKED = 256;
 // Prisoners whose squad CDF managed when they surrendered (weak; pruned).
 static ref array<SCR_ChimeraCharacter> FromManagedSquads = {};

 static ESR_Prisoner Find(SCR_EditableEntityComponent entity)
 {
  // Clear asks for every editable entity: only characters can be prisoners.
  if (!entity || ESR_SurrenderManager.PrisonerCount() == 0 || entity.GetEntityType() != EEditableEntityType.CHARACTER) return null;
  return ESR_SurrenderManager.FindPrisoner(entity.GetOwner());
 }

 static void Prune()
 {
  for (int i = FromManagedSquads.Count() - 1; i >= 0; i--)
  {
   if (!FromManagedSquads[i]) FromManagedSquads.Remove(i);
  }
 }

 // Server, when the interrogation point of a new prisoner is set up: his squad still
 // exists (an emptied group deletes itself one frame later).
 static void NoteSquad(SCR_ChimeraCharacter character)
 {
  if (!character || !Replication.IsServer() || FromManagedSquads.Contains(character)) return;
  ESR_Prisoner prisoner = ESR_SurrenderManager.FindPrisoner(character);
  if (!prisoner || !prisoner.Group) return;
  SCR_EditableEntityComponent squad = SCR_EditableEntityComponent.GetEditableEntity(prisoner.Group);
  if (!squad || !CDF_GMSaveCapture.IsManaged(squad)) return;
  if (FromManagedSquads.Count() >= MAX_TRACKED) Prune();
  if (FromManagedSquads.Count() >= MAX_TRACKED) FromManagedSquads.RemoveOrdered(0);
  FromManagedSquads.Insert(character);
 }

 // Clear predicate for a prisoner CDF would otherwise keep. Never a player or possessed body.
 static bool ClearedWithSquad(SCR_EditableEntityComponent entity)
 {
  ESR_Prisoner prisoner = Find(entity);
  if (!prisoner || !prisoner.Character || ESR_SurrenderManager.IsPlayerCharacter(prisoner.Character)) return false;
  if (FromManagedSquads.Contains(prisoner.Character)) return true;
  if (!prisoner.Group) return false;
  SCR_EditableEntityComponent squad = SCR_EditableEntityComponent.GetEditableEntity(prisoner.Group);
  return squad && squad != entity && CDF_GMSaveCapture.IsManaged(squad);
 }

 // Leave every prisoner (and anything parented to one) out of the document.
 static bool RemovePrisoners(CDF_GMSaveDocument document)
 {
  if (!document || !document.m_aEntities) return false;
  if (ESR_SurrenderManager.PrisonerCount() == 0) return true;
  array<int> remap = {};
  array<ref CDF_GMSaveEntityRecord> kept = {};
  int removed;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record) return false;
   bool drop = Find(record.m_Entity) != null;
   // Records are parent-first: a child follows its removed parent.
   if (!drop && record.m_iParent >= 0 && record.m_iParent < remap.Count() && remap[record.m_iParent] < 0) drop = true;
   if (drop)
   {
    remap.Insert(-1);
    removed++;
    continue;
   }
   remap.Insert(kept.Count());
   kept.Insert(record);
  }
  if (removed == 0) return true;
  foreach (CDF_GMSaveEntityRecord entry : kept)
  {
   if (entry.m_iParent >= 0)
   {
    if (entry.m_iParent >= remap.Count() || remap[entry.m_iParent] < 0) return false;
    entry.m_iParent = remap[entry.m_iParent];
   }
   if (entry.m_iTarget >= 0)
   {
    if (entry.m_iTarget >= remap.Count()) return false;
    // A target that was a prisoner is dropped (TARGET_NONE is -1).
    entry.m_iTarget = remap[entry.m_iTarget];
   }
  }
  document.m_aEntities = kept;
  PrintFormat("[ESR CDF] %1 prisoner records left out of the CDF save (prisoners are session-only)", removed);
  return true;
 }

 // Prisoner records whose character still exists (at most MAX_PRISONERS).
 static int LiveCount()
 {
  int live;
  for (int i = ESR_SurrenderManager.PrisonerCount() - 1; i >= 0; i--)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && prisoner.Character) live++;
  }
  return live;
 }

 // After a CDF load: release the records of prisoners the load removed, so their
 // interrogation points go now instead of at the next 5 s upkeep. When the load restores
 // the first surrender module, that module has already released them as stale, so the
 // count compares living records before and after the load.
 static void ReleaseRemoved(int liveBefore)
 {
  for (int i = ESR_SurrenderManager.PrisonerCount() - 1; i >= 0; i--)
  {
   ESR_Prisoner prisoner = ESR_SurrenderManager.GetPrisonerAt(i);
   if (prisoner && !prisoner.Character) ESR_SurrenderManager.Release(prisoner, "removed by a CDF load");
  }
  Prune();
  int removed = liveBefore - LiveCount();
  if (removed > 0) PrintFormat("[ESR CDF] %1 prisoners removed by the CDF load", removed);
 }
}

modded class ESR_InterrogationPoint
{
 override void Setup(RplId prisonerId, ESR_Dossier dossier)
 {
  super.Setup(prisonerId, dossier);
  ESR_CDF.NoteSquad(GetPrisoner());
 }
}

modded class CDF_GMSaveCapture
{
 override static CDF_GMSaveDocument Capture(string displayName, string author)
 {
  CDF_GMSaveDocument document = super.Capture(displayName, author);
  // Preserve an upstream refusal; there is no document to filter.
  if (!document) return null;
  if (!ESR_CDF.RemovePrisoners(document))
  {
   Print("[ESR CDF] Save rejected: prisoner records could not be removed consistently", LogLevel.ERROR);
   return null;
  }
  return document;
 }

 // Clear calls this public predicate across classes.
 override static bool IsManaged(SCR_EditableEntityComponent entity)
 {
  if (super.IsManaged(entity)) return true;
  return ESR_CDF.ClearedWithSquad(entity);
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  int live = ESR_CDF.LiveCount();
  bool result = super.Restore(document);
  ESR_CDF.ReleaseRemoved(live);
  return result;
 }
}
