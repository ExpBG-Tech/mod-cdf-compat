// EXPBG CDF Compat - AI Global Skills. Copyright 2026 ExpBG Tech (M.Pac and K.Edgar).
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// CDF GameMaster Save 1.4.1 carries the module settings through the hidden session
// attributes of AI Global Skills (global ROE, ammunition and refills, up to eight touched
// factions) and each AI group's ROE override through its group attribute. Two gaps:
// - CDF writes the saved factions onto the running session's table, so a faction touched
//   after the save kept its newer values. A CDF load now starts from vanilla before the
//   first saved module value, and a load that restores no module leaves vanilla behind
//   instead of the replaced session's values.
// - CDF reads a group's vanilla combat mode after the EXPBG ROE changed it. The restored
//   group then took the EXPBG mode as its own and Vanilla ROE (or deleting the module)
//   no longer returned it. A session save now records the mode the group had before
//   the EXPBG ROE touched it; the module applies its ROE again after the load.
class EGS_CDFLoad
{
 // True only inside a CDF restore (entity spawn and attribute pass are synchronous).
 static bool Restoring;
 // The saved module values replaced the running session's values during this load.
 static bool Replaced;

 static void ResetToVanilla(string reason)
 {
  array<string> keys = {};
  array<int> values = {};
  EGS_Settings.ImportPersistent(keys, values, EGS_Settings.ROE_VANILLA, EGS_Settings.AMMO_VANILLA, EGS_Settings.REFILLS_DEFAULT);
  Print("[EGS CDF] Settings reset to vanilla: " + reason);
 }

 // Same contract as the module's own session-load check: no editor manager, player -1.
 static void BeforeModuleValue(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  if (!Restoring || Replaced || manager || playerID != -1 || !var || !Replication.IsServer()) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable || !EGS_Module.Cast(editable.GetOwner())) return;
  Replaced = true;
  ResetToVanilla("a CDF load restores the saved module values");
 }
}

// Modded config classes repeat the original decorator; without it Edit.conf reports
// "Unknown class" and drops the attribute (combat mode, saved module values).
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
modded class EGS_SavedGlobalAttribute
{
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EGS_CDFLoad.BeforeModuleValue(item, var, manager, playerID);
  super.WriteVariable(item, var, manager, playerID);
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
modded class EGS_SavedFactionAttribute
{
 override void WriteVariable(Managed item, SCR_BaseEditorAttributeVar var, SCR_AttributesManagerEditorComponent manager, int playerID)
 {
  EGS_CDFLoad.BeforeModuleValue(item, var, manager, playerID);
  super.WriteVariable(item, var, manager, playerID);
 }
}

modded class CDF_GMSaveRestore
{
 override static bool Restore(notnull CDF_GMSaveDocument document)
 {
  EGS_CDFLoad.Restoring = true;
  EGS_CDFLoad.Replaced = false;
  bool result = super.Restore(document);
  EGS_CDFLoad.Restoring = false;
  // The load cleared the scene and restored no module: a module placed later starts
  // from vanilla, not from the settings of the session the load replaced.
  if (result && !EGS_CDFLoad.Replaced && CDF_GMSaveConfig.GetInstance().m_bClearBeforeLoad && !EGS_Module.HasAny() && !EGS_Settings.IsVanilla())
   EGS_CDFLoad.ResetToVanilla("the CDF load restored no AI Global Skills module");
  return result;
 }
}

modded class SCR_AIGroup
{
 // The combat mode this group had before the EXPBG rules of engagement changed it.
 bool EGS_CDFGetOriginalCombatMode(out EAIGroupCombatMode mode)
 {
  if (!m_bEGS_RoeTouched) return false;
  mode = m_eEGS_OriginalMode;
  return true;
 }
}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
modded class SCR_AIGroupCombatModeAttribute
{
 // Session saves read without an editor manager. Keep the group's own combat mode; the
 // EXPBG ROE is module state and is applied again once the load has restored the module.
 override SCR_BaseEditorAttributeVar ReadVariable(Managed item, SCR_AttributesManagerEditorComponent manager)
 {
  SCR_BaseEditorAttributeVar var = super.ReadVariable(item, manager);
  if (manager || !var) return var;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(item);
  if (!editable) return var;
  SCR_AIGroup group = SCR_AIGroup.Cast(editable.GetOwner());
  EAIGroupCombatMode original;
  if (!group || !group.EGS_CDFGetOriginalCombatMode(original)) return var;
  int index = ConvertValueToIndex(original);
  if (index < 0) return var;
  return SCR_BaseEditorAttributeVar.CreateInt(index);
 }
}
