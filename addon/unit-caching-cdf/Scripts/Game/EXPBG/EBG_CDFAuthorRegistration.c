// CDF 1.4.1 assigns saved authors without registering their entity contribution.
// Attribute only its synchronous, initially unauthored spawns; never rebuild UID totals.
class EBG_CDFAuthors
{
 static bool Restoring;
 static ref set<SCR_EditableEntityAuthor> Registered = new set<SCR_EditableEntityAuthor>();
 static SCR_EditableEntityComponent NativeTarget;
 static ref array<SCR_EditableEntityComponent> Pending = {};
 static int NextRecord;
 static void Reset()
 {
  Restoring = false;
  Registered.Clear();
  NativeTarget = null;
  Pending.Clear();
  NextRecord = 0;
 }
 static void FinishAssignments()
 {
  // Observe the entire synchronous override chain, including registration after SetAuthor.
  foreach (SCR_EditableEntityComponent entity : Pending)
  {
   if (entity) entity.EBG_CDFRegisterMissingAuthor();
  }
  Pending.Clear();
  Registered.Clear();
  Restoring = false;
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

 override void EOnEditorSessionLoad(SCR_EditableEntityComponent parent)
 {
  super.EOnEditorSessionLoad(parent);
  if (!Replication.IsServer() || !GetAuthorUID().IsEmpty()) return;
  m_EBG_CDFExpectedAuthor = CDF_GMSaveRestore.EBG_FindAuthorRecord(this);
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
  if (!EBG_CDFAuthors.Restoring || !expected || expected.m_Entity != this) return;
  if (author.m_sAuthorUID != expected.m_sAuthorUID || author.m_sAuthorPlatformID != expected.m_sAuthorPlatformID || author.m_ePlatform != expected.m_iAuthorPlatform || author.m_iAuthorID != -1) return;
  m_EBG_CDFExpectedAuthor = null;
  if (!initiallyUnauthored) return;
  m_EBG_CDFPendingAuthor = author;
  EBG_CDFAuthors.Pending.Insert(this);
 }
 void EBG_CDFRegisterMissingAuthor()
 {
  SCR_EditableEntityAuthor author = m_EBG_CDFPendingAuthor;
  m_EBG_CDFPendingAuthor = null;
  if (!author || !GetOwner() || GetAuthor() != author || !Replication.IsServer()) return;
  SCR_EditableEntityCore core = SCR_EditableEntityCore.Cast(SCR_EditableEntityCore.GetInstance(SCR_EditableEntityCore));
  if (!core) return;
  if (author.m_iEntityCount != 0 || core.FindAuthorByIdentity(author.m_sAuthorUID) == author || EBG_CDFAuthors.Registered.Contains(author)) return;
  core.RegisterAuthorServer(author);
  m_EBG_CDFCountedAuthor = author;
  m_EBG_CDFCountedUID = author.m_sAuthorUID;
  m_EBG_CDFAuthorWorld = GetOwner().GetWorld();
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
