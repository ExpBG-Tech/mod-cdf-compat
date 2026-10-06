# EXPBG CDF Compat design

## Pack assembly

EXPBG CDF Compat is one engine project (`addon/EXPBG_CDF_Compat.gproj`,
`07BC942D90324CD9`) built from the module folders listed in `tools/pack.json`.
`tools/Assemble-Pack.ps1` (shared with EXPBG GM Tools) copies every module file
to its original runtime path, so script paths, resource GUIDs and `{GUID}path`
references are unchanged. Undeclared path collisions (case-insensitive),
duplicate metadata GUIDs, module `.gproj` files and Workbench-only scripts stop
the assembly. The companions override no vanilla or CDF config, so `merge` and
`relocate` are empty; a future shared override must be declared there.

| Module | Runtime files | Former project |
|---|---|---|
| `unit-caching-cdf` | `Scripts/Game/EXPBG/EBG_CDF*.c`, unused banner PNG | `EXPBG_GM_Optimizer_CDF` `8C5A6D9E73B241F0` -> `F3B7C6FB18AB1F79` |
| `intel-items-cdf` | `Scripts/Game/EXPII/EII_CDF*.c`, banner PNG/EDDS (`E110000000000041`) | `EXPBG_Intel_Items_CDF` `E110000000000002` -> `E110000000000001` |
| `ambient-destruction-cdf` | `Scripts/Game/EAD_CDF/EAD_CDF.c` | `EXPBG_Ambient_Destruction_CDF` `D7A82F4139C60BE5` -> `E2A47D19C8B6503F` |
| `garrison-cdf` | `Scripts/Game/EXPG_CDF/EXPG_CDFGarrisonGuard.c` | new |

## Identity

The standalone companions carried their identity and parent only in their
`.gproj` files; no script checked a loaded-addon GUID. The pack project replaces
all three with `07BC942D90324CD9` and the dependencies base game, EXPBG GM Tools
and CDF. EXPBG GM Tools checks for exactly this GUID:
`addon/unit-caching/Scripts/Game/EXPBG/EBG_FullSaveGate.c` (`CanCaptureForCDF`)
lets Unit Caching Full-cache under CDF, and
`addon/garrison/Scripts/Game/EXPG/EXPG_GarrisonManager.c` (`TrySleep`) lets
Garrison cache under CDF. The pack does not depend on the standalone EXPBG mods.

EXPBG GM Tools keeps the original module class names, prefab GUIDs and paths.
At the GM Tools commit pinned in `tools/pack.json` every class, modded class,
method and resource the merged scripts use resolves, every override in a modded
GM Tools class matches a method GM Tools declares (same static, return and
parameter types; an override of a removed method breaks the whole Game module
compile), and no pack path, resource GUID or class name repeats one from GM
Tools (`tests/Test-DependencyOverlap.ps1`). Resource
identities used as literals: Unit Caching zone prefab `7E1080ED8F0633FD`,
Ambient Destruction zone prefab `EAD1000000000010`, the seven Intel Items prefabs
(`EII_CDFState.c`), and the CDF addon `6A1876F37D65AB09` in the Garrison guard.

## Saved data

Every bridge wraps CDF's opaque per-entity or world state and validates all of
its input before CDF clears the scene. The keys are unchanged from the standalone
companions:

| Module | Envelope | Contents |
|---|---|---|
| Unit Caching | `ebgCache` + `cdfState` on each cache zone | `ebgCacheVersion` 1, zone settings (`ebgZoneVersion` 4/5), cached groups: group snapshot, survivors (prefab, matrix, author), casualty count |
| Intel Items | `eiiIntel` + `cdfState` on intel items and inventory carriers | version 1: prefab, title, content, spent, diagnostics; carried copies per inventory (max 400) |
| Ambient Destruction | `eadZone` + `cdfOriginal` per zone; `eadBuildings` + `cdfOriginal` around the world state | zone snapshot (settings, exact scenery records); building ledger (schema 1/2) |

CDF documents record prefab resource names, not addon IDs, so a document written
with the standalone mods and companions references the same prefabs and payload
keys. Two load-time rules of the original bridges remain: a document that holds
an Intel prefab without an `eiiIntel` payload, or an Ambient Destruction zone
without an exact snapshot, is rejected before clearing. Documents saved with
EXPBG GM Tools but without this pack therefore cannot be loaded while it is
active if they contain those modules.

## Adapter layering

All three bridges mod `CDF_GMSaveState`, `CDF_GMSaveCapture` and
`CDF_GMSaveRestore`; Ambient Destruction also mods `CDF_GMSaveRepair` and Unit
Caching mods `SCR_EditableEntityCore`, `SCR_EditableEntityComponent` and its
serializer (CDF 1.4.1 author registration) plus `EBG_CacheSnapshot` (world
cleanup). Standalone, their relative order
followed the addon load order; in the pack it follows script compilation order.
Each layer filters by entity type and calls `super`, and the 0.0.4 Intel and
0.0.1 Destruction bridges were written to preserve another adapter's refusal in
either order: a capture refusal returns no document, and a restore preflight
rejection before CDF's own `Restore` leaves the scene untouched. The combined
order inside one module still needs native confirmation.

## Garrison under CDF

Inputs (pinned GM Tools, CDF 1.4.1):

- CDF saves through `CDF_GMSaveCapture.Capture` (manual and autosave). The Unit
  Caching bridge first calls `EBG_CacheSnapshot.CanSave`; GM Tools Garrison
  (`EXPG_SaveSafety.c`) refuses there while `EXPG_GarrisonManager.HasActive()`.
  With this pack, no CDF document is written while any garrison is active.
- CDF loads through `CDF_GMSaveRestore.Restore`. With `clearBeforeLoad` it
  deletes the topmost managed (serializable, authored or author-related,
  deletable) editable entity of each hierarchy. No bridge checks garrisons.
- Garrison Full (`EXPG_FullCache`) is an `EBG_PrefabFullCache` without a
  portable group snapshot: survivors are deleted, the native group is retained
  empty, and the transaction is not in `EBG_CacheManager.Records`, so the Unit
  Caching bridge never exports it.

Full-cached garrison during a clear-before-load: when CDF deletes the retained
empty group (a squad placed through the GM picker carries the GM author), the
next garrison tick wakes the record;
`BeginWake` finds neither snapshot nor group and refuses with "Original group no
longer exists". The record retries every 5 s forever, `HasActive()` stays true,
and every later CDF save, native save (`EBG_MissionPersistenceSerializer`) and
Prepare for Save stays blocked until a new world. When CDF keeps that group
(for example an unauthored scenario group, or if CDF does not serialize an empty
group), wake respawns the survivors into the newly loaded scene, possibly next
to an older saved copy of the same squad. Both outcomes are reasoned from source;
neither has been reproduced natively.

Simulation-cached garrison: the original actors stay in their group with pinned
AI LOD. Clear deletes the group with its members like any squad. The garrison
tick sees the group missing, discards snapshots whose actors are gone,
`EBG_SimulationCache.Restore` completes with nothing left, and the record
releases. Nobody is recreated. With an append load or an unmanaged group nothing
is deleted and the garrison continues.

Decision: `EXPG_CDFGarrisonGuard.c` mods `EXPG_GarrisonManager.TryFullSleep`.
While CDF (`6A1876F37D65AB09`) is loaded it reports "Full cache held: CDF saves
cannot keep Garrison Full survivors and this EXPBG GM Tools has no Simulation
fallback. Update GM Tools, or choose Simulation or Off." and returns before any
deletion; otherwise it calls the original. It does not change the GM-selected
mode, Simulation, wake/sleep or release. Because the pack depends on CDF,
Garrison Full is effectively off whenever the pack is loaded. Real Garrison
persistence would need a portable garrison ledger (building identity, posts,
patrol state, survivor snapshots) in the CDF document and is not attempted.

Since GM Tools 0.1.8, `EXPG_GarrisonManager.CacheModeInUse` itself runs a
garrison set to Full in Simulation while CDF is loaded (status "Simulation
cached (CDF loaded)"), so `TryFullSleep` is not reached under CDF and the guard
is a backstop that reports only next to an older GM Tools. Each state through a
CDF save and load: awake and Simulation-cached garrisons block the save (Prepare
for Save restores and releases them first); a clear-before-load deletes the
Simulation originals with their group and the garrison releases without
recreating anyone; an append load leaves the garrison running.

## Native builds and dependencies

`tools/project.json` declares the dependencies the native build snapshots:
CDF (`CDFGameMasterSave_6A1876F37D65AB09`) and EXPBG GM Tools
(`EXPBGGMTools_FC1402F65B2F4A45`, then the local build folder `EXPBG_GM_Tools`).
`tools/Copy-AddonDependencies.ps1` searches `DependencyAddonsRoots` in order
(default: Workshop downloads, then the Workbench addons directory), verifies
each project GUID, copies it to a GUID-named folder and writes
`dependencies.json` (source, Workshop version if any, packed or loose, hashes).
Build and release receipts embed that record.

## Coordination with EXPBG GM Tools

The Unit Caching bridge serializes no cleanup-ledger fields. It depends on
`EBG_CacheManager.Instance`, `Records` and `IsPortableWorldReady`, on
`EBG_CacheSnapshot` (`CanSave`, `WriteZone`, `ReadZone`, `ValidateZone`,
`PrepareImport`, `EndImport`) and on `EBG_PrefabFullCache` export/import, which
recreates casualties from a count through `EBG_CacheManager.AddCachedMember` and
registers restored survivors with `EBG_CacheCleanup`. A GM Tools change to
per-casualty cleanup in `EBG_CacheCleanup.c`/`EBG_CacheManager.c` that alters
those signatures, the casualty representation or the snapshot schema requires a
rebuild here, a new pinned commit and a CDF round trip. Snapshots written before
such a change must keep loading or be rejected before clearing.
