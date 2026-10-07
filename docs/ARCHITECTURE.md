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
| `garrison-cdf` | `Scripts/Game/EXPG_CDF/EXPG_CDFGarrisonBridge.c` | new |

## Identity

The standalone companions carried their identity and parent only in their
`.gproj` files; no script checked a loaded-addon GUID. The pack project replaces
all three with `07BC942D90324CD9` and the dependencies base game, EXPBG GM Tools
and CDF. EXPBG GM Tools checks for exactly this GUID:
`addon/unit-caching/Scripts/Game/EXPBG/EBG_FullSaveGate.c` (`CanCaptureForCDF`)
lets Unit Caching Full-cache under CDF, and
`addon/garrison/Scripts/Game/EXPG/EXPG_GarrisonManager.c` (`TrySleep`) lets
Garrison cache under CDF; the garrison bridge overrides
`EXPG_GarrisonManager.CdfBridgeVersion` to switch GM Tools to "CDF bridged". The pack does not depend on the standalone EXPBG mods.

EXPBG GM Tools keeps the original module class names, prefab GUIDs and paths.
At the GM Tools commit pinned in `tools/pack.json` every class, modded class,
method and resource the merged scripts use resolves, every override in a modded
GM Tools class matches a method GM Tools declares (same static, return and
parameter types; an override of a removed method breaks the whole Game module
compile), and no pack path, resource GUID or class name repeats one from GM
Tools (`tests/Test-DependencyOverlap.ps1`). Resource
identities used as literals: Unit Caching zone prefab `7E1080ED8F0633FD`,
Ambient Destruction zone prefab `EAD1000000000010`, the seven Intel Items prefabs
(`EII_CDFState.c`), and the garrison ledger key `expgGarrisons`.

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

Bridge: `addon/garrison-cdf/Scripts/Game/EXPG_CDF/EXPG_CDFGarrisonBridge.c`
(0.1.6; the 0.1.5 guard is retired). It uses only the GM Tools 0.1.11 API
`EXPG_GarrisonPersistence` and the ledger classes (`EXPG_Snapshot.c`).

Handshake: `EXPG_GarrisonManager.CdfBridgeVersion()` (a protected instance method,
so the override dispatches) returns `EXPG_GarrisonPersistence.BRIDGE_API`. GM
Tools then runs "CDF bridged": garrison-owned squads, waypoints and living guards
are `NON_SERIALIZABLE` (CDF's `Serialize()` filter skips them and their subtree)
and out of native tracking, and Full caching is allowed. Without the bridge (or
with 0.1.5) GM Tools runs "CDF legacy", the 0.1.8 rules.

Save (`CDF_GMSaveCapture.Capture`): `EXPG_GarrisonPersistence.ExportJson` first
(it synchronises the exclusion; a refusal returns no document and opens the
dialog), then CDF's capture, then a backstop that removes any record whose entity
the garrison still owns and remaps `m_iParent`/`m_iTarget`, then the envelope
`{"expgGarrisons": ledger, "cdfOriginal": world state}` (the Ambient Destruction
pattern). No garrison, no envelope. Nothing is spawned, woken or materialized.

Clear (`CDF_GMSaveCapture.IsManaged`): true for garrison-owned entities, so CDF's
Clear deletes the garrisons of the current scene with the rest of it.

Load (`CDF_GMSaveRestore.Restore`), every refusal before `super.Restore`:
1. Peel the envelope (top level, or under the Ambient Destruction layer, rebuilt)
   and read the ledger: schema, world, limits, soldier and squad prefabs,
   factions, orders. Unreadable: refused; the same document again within two
   minutes loads without garrisons (recovery, with a notice).
2. A ledger with garrisons requires `clearBeforeLoad`.
3. Refused while an earlier garrison load is still being placed.
4. `BeginImport` (Add Garrison waits), inner world state in place, `super`.
5. Mutation (CDF returned true, or `s_RestoredEntities` was replaced): with
   `clearBeforeLoad` the old garrisons are discarded without waking or
   respawning anyone; the ledger is queued (a save during the load writes it back
   verbatim). No mutation: `EndImport`, nothing changed.
6. A callback polls CDF's protected statics every 100 ms (at most 10 s) until
   finalization is done (`s_RestoredEntities` null, pending states, guarded
   groups and pending members empty), then `FinishImport` creates the
   garrisons: buildings, compositions, deep state and the Ambient Destruction
   replay are final by then.

No duplicates under `clearBeforeLoad`: CDF never holds a garrison entity, Clear
removes the scene's garrisons, the bridge discards their records, and ledger
tokens are unique (an import refuses a token that already exists). A partial
CDF load after the clear still imports the garrisons (they are self-contained).
Squads protected from deletion by the editor are not garrison-portable: GM Tools
leaves them to CDF as ordinary squads.

Not covered by the bridge: a save made with the bridge loaded without it (CDF
then cannot read its world attributes and the garrisons are lost, as with the
Ambient Destruction envelope); scenario-placed squads garrisoned in a scenario
that recreates them on a cold start (CDF does not own scenario entities either).

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
