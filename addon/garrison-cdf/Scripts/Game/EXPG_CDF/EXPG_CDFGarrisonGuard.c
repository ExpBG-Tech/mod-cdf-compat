// EXPBG CDF Compat - Garrison guard. Copyright 2026 ExpBG Tech (M.Pac and K.Edgar).
// Arma Public License Share Alike (APL-SA):
// https://www.bohemia.net/en/licenses/arma-public-license-share-alike
//
// Loading this pack satisfies the GM Tools CDF gate (07BC942D90324CD9), which would
// otherwise let Garrison use Full caching under CDF. Garrison Full records are
// standalone transactions outside EBG_CacheManager.Records, so the Unit Caching CDF
// bridge never exports them. Saves are already refused while any garrison is active
// (EBG_CacheSnapshot.CanSave). A CDF clear-before-load, however, deletes the retained
// empty group of a Full-cached garrison: BeginWake then refuses forever and the record
// blocks every later CDF and native save. If the group survives, the deleted survivors
// are respawned into the newly loaded scene. Keep Full refused while CDF is loaded.
// Simulation keeps the original actors and group in the world; CDF Clear deletes them
// like any ordinary squad and the garrison releases without recreating anyone.
modded class EXPG_GarrisonManager
{
 override protected void TryFullSleep(EXPG_GarrisonRecord record)
 {
  array<string> addons = {};
  GameProject.GetLoadedAddons(addons);
  if (addons.Contains("6A1876F37D65AB09"))
  {
   record.Report("Full cache held: CDF saves cannot keep Garrison Full survivors. Choose Simulation or Off.");
   return;
  }
  super.TryFullSleep(record);
 }
}
