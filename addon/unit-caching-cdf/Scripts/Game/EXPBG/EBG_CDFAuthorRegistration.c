// CDF 1.4.1 assigns saved authors without registering their entity contribution.
// Attribute only its synchronous, initially unauthored spawns; never rebuild UID totals.
class EBG_CDFAuthors
{
 static bool Restoring;
 static ref set<SCR_EditableEntityAuthor> Registered = new set<SCR_EditableEntityAuthor>();
 static SCR_EditableEntityComponent NativeTarget;
 static ref array<SCR_EditableEntityComponent> Pending = {};
 static int NextRecord;
 static const int REGISTERED = 0;
 static const int ALREADY_COUNTED = 1;
 static const int LOST = 2;
 // Deletes of an unregistered author, keyed by type and bookkeeping; printed once per frame.
 static ref map<string, int> Unregistered = new map<string, int>();
 static ref map<string, string> UnregisteredPrefab = new map<string, string>();
 static void Reset()
 {
  Restoring = false;
  Registered.Clear();
  NativeTarget = null;
  Pending.Clear();
  NextRecord = 0;
 }
 static void FinishAssignments(CDF_GMSaveDocument document = null)
 {
  // Observe the entire synchronous override chain, including registration after SetAuthor.
  int matched = Pending.Count();
  int registered, counted, lost;
  foreach (SCR_EditableEntityComponent entity : Pending)
  {
   int outcome = LOST;
   if (entity) outcome = entity.EBG_CDFRegisterMissingAuthor();
   if (outcome == REGISTERED) registered++;
   else if (outcome == ALREADY_COUNTED) counted++;
   else lost++;
  }
  Pending.Clear();
  Registered.Clear();
  Restoring = false;
  if (!document) return;
  // Restore summary: every spawned authored record should end registered or already counted.
  int authored, spawned;
  foreach (CDF_GMSaveEntityRecord record : document.m_aEntities)
  {
   if (!record || record.m_sAuthorUID.IsEmpty()) continue;
   authored++;
   if (record.m_Entity) spawned++;
  }
  if (spawned > 0 || matched > 0) PrintFormat("[EBG CDF AUTHORS] restore authored=%1 spawned=%2 matched=%3 registered=%4 alreadyCounted=%5 lost=%6 unmatched=%7", authored, spawned, matched, registered, counted, lost, spawned - matched);
 }
 static void NoteUnregisteredDelete(string key, string prefab)
 {
  if (Unregistered.IsEmpty() && GetGame()) GetGame().GetCallqueue().CallLater(PrintUnregistered, 0, false);
  Unregistered.Set(key, Unregistered.Get(key) + 1);
  if (!UnregisteredPrefab.Contains(key)) UnregisteredPrefab.Set(key, prefab);
 }
 static void PrintUnregistered()
 {
  int total;
  string details;
  foreach (string key, int count : Unregistered)
  {
   total += count;
   details += string.Format(" | %1 x%2 e.g. %3", key, count, UnregisteredPrefab.Get(key));
  }
  Unregistered.Clear();
  UnregisteredPrefab.Clear();
  if (total > 0) Print(string.Format("[EBG CDF AUTHORS] %1 deletes of entities whose author is not registered", total) + details, LogLevel.WARNING);
 }
}
[BaseContainerProps(configRoot: true)]
modded class SCR_EditableEntityCore
{
 override void RegisterAuthorServer(SCR_EditableEntityAuthor newAuthor)
 {
  super.RegisterAuthorServer(newAuthor);
  if (EBG_CDFAuthors.Restoring && Replication.IsServer()) EBG_CDFAuthors.Registered.Insert(newAuthor);
 }
}
modded class SCR_EditableEntityComponent
{
 protected ref CDF_GMSaveEntityRecord m_EBG_CDFExpectedAuthor;
 protected ref SCR_EditableEntityAuthor m_EBG_CDFCountedAuthor;
 protected ref SCR_EditableEntityAuthor m_EBG_CDFPendingAuthor;
 protected string m_EBG_CDFCountedUID;
 protected BaseWorld m_EBG_CDFAuthorWorld;
 protected bool m_EBG_CDFRestored;

 override void EOnEditorSessionLoad(SCR_EditableEntityComponent parent)
 {
  super.EOnEditorSessionLoad(parent);
  // The forward-only lookup returns null on a repeat call; never drop a found record.
  if (!Replication.IsServer() || !GetAuthorUID().IsEmpty() || m_EBG_CDFExpectedAuthor) return;
  m_EBG_CDFExpectedAuthor = CDF_GMSaveRestore.EBG_FindAuthorRecord(this);
 }
 override bool Delete(bool changedByUser = false, bool updateNavmesh = false)
 {
  // Name what vanilla AuthorEntityRemovedServer reports as "This should not happen".
  SCR_EditableEntityAuthor author = GetAuthor();
  if (Replication.IsServer() && author && !author.m_sAuthorUID.IsEmpty() && GetOwner() && !GetOwner().IsDeleted() && !HasEntityFlag(EEditableEntityFlag.NON_DELETABLE))
  {
   SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
   if (core && !core.FindAuthorByIdentity(author.m_sAuthorUID))
   {
    string prefab;
    EntityPrefabData prefabData = GetOwner().GetPrefabData();
    if (prefabData) prefab = prefabData.GetPrefabName();
    EBG_CDFAuthors.NoteUnregisteredDelete(string.Format("%1 cdfRestored=%2 adapterCounted=%3", typename.EnumToString(EEditableEntityType, GetEntityType()), m_EBG_CDFRestored, m_EBG_CDFCountedAuthor != null), prefab);
   }
  }
  return super.Delete(changedByUser, updateNavmesh);
 }
 bool EBG_CDFOwnsAuthorCount()
 {
  return m_EBG_CDFCountedAuthor && GetAuthor() == m_EBG_CDFCountedAuthor && GetAuthorUID() == m_EBG_CDFCountedUID && GetOwner() && GetOwner().GetWorld() == m_EBG_CDFAuthorWorld;
 }
 override void SetAuthor(SCR_EditableEntityAuthor author)
 {
  SCR_EditableEntityAuthor oldAuthor = GetAuthor();
  bool initiallyUnauthored = !oldAuthor || oldAuthor.m_sAuthorUID.IsEmpty();
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  bool nativeReplacement = core && author && EBG_CDFAuthors.NativeTarget == this && EBG_CDFOwnsAuthorCount() && core.FindAuthorByIdentity(author.m_sAuthorUID) == author;
  super.SetAuthor(author);
  if (!core || !author || GetAuthor() != author || !Replication.IsServer()) return;
  if (nativeReplacement)
  {
   // The inspected native Deserialize has already incremented the new canonical
   // author. Transfer only our known old contribution, including the same-UID case.
   core.AuthorEntityRemovedServer(m_EBG_CDFCountedAuthor);
   m_EBG_CDFCountedAuthor = null;
   return;
  }
  CDF_GMSaveEntityRecord expected = m_EBG_CDFExpectedAuthor;
  // Vanilla SCR_EditableCharacterComponent.EOnEditorSessionLoad skips super, so restored
  // characters arrive here uncached. CDF binds record.m_Entity before calling SetAuthor.
  if (!expected && initiallyUnauthored)
  {
   expected = CDF_GMSaveRestore.EBG_FindAuthorRecord(this);
   m_EBG_CDFExpectedAuthor = expected;
  }
  if (!EBG_CDFAuthors.Restoring || !expected || expected.m_Entity != this) return;
  m_EBG_CDFRestored = true;
  if (author.m_sAuthorUID != expected.m_sAuthorUID || author.m_sAuthorPlatformID != expected.m_sAuthorPlatformID || author.m_ePlatform != expected.m_iAuthorPlatform || author.m_iAuthorID != -1) return;
  m_EBG_CDFExpectedAuthor = null;
  if (!initiallyUnauthored) return;
  m_EBG_CDFPendingAuthor = author;
  EBG_CDFAuthors.Pending.Insert(this);
 }
 int EBG_CDFRegisterMissingAuthor()
 {
  SCR_EditableEntityAuthor author = m_EBG_CDFPendingAuthor;
  m_EBG_CDFPendingAuthor = null;
  if (!author || !GetOwner() || GetAuthor() != author || !Replication.IsServer()) return EBG_CDFAuthors.LOST;
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (!core) return EBG_CDFAuthors.LOST;
  if (author.m_iEntityCount != 0 || core.FindAuthorByIdentity(author.m_sAuthorUID) == author || EBG_CDFAuthors.Registered.Contains(author)) return EBG_CDFAuthors.ALREADY_COUNTED;
  core.RegisterAuthorServer(author);
  m_EBG_CDFCountedAuthor = author;
  m_EBG_CDFCountedUID = author.m_sAuthorUID;
  m_EBG_CDFAuthorWorld = GetOwner().GetWorld();
  return EBG_CDFAuthors.REGISTERED;
 }
}
modded class SCR_EditableEntityComponentSerializer
{
 override protected bool Deserialize(notnull IEntity owner, notnull GenericComponent component, notnull LoadContext context)
 {
  SCR_EditableEntityComponent previousTarget = EBG_CDFAuthors.NativeTarget;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(component);
  EBG_CDFAuthors.NativeTarget = null;
  if (editable && editable.EBG_CDFOwnsAuthorCount()) EBG_CDFAuthors.NativeTarget = editable;
  bool result = super.Deserialize(owner, component, context);
  EBG_CDFAuthors.NativeTarget = previousTarget;
  return result;
 }
}
