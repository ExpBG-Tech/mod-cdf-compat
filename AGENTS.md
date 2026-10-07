# EXPBG CDF Compat

CDF Game Master Save support for the EXPBG GM Tools pack, in one addon.
Module folders under `addon/` hold the merged companions byte-for-byte; keep class
names, resource GUIDs, prefab paths and saved-data keys so older CDF saves stay readable.
Record any intentional change to an imported file in `tools/pack.json` and the changelog.
Depend only on APIs present in the pinned EXPBG GM Tools commit; never edit mod-gm-tools here.
Garrisons are saved through the GM Tools garrison ledger (garrison-cdf bridge); never let CDF capture a garrison-owned entity.
Source is authoritative; installed addons and frozen builds are never edited.
Native build, testing and publication require the orchestrator's explicit native-slot
release. Never stop unrelated engine processes. Keep credentials, profiles and evidence ignored.
Run `./tests/Test-Tools.ps1`, `./build.ps1`, native CDF round trips and multiplayer
acceptance separately. Report each gate honestly. Compile success is not gameplay.
