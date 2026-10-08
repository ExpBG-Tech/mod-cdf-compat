# EXPBG CDF Compat

CDF Game Master Save support for the [EXPBG GM Tools](https://reforger.armaplatform.com/workshop/FC1402F65B2F4A45)
modpack, by M.Pac and K.Edgar. One addon and engine project
(`07BC942D90324CD9`, Unlisted):
<https://reforger.armaplatform.com/workshop/07BC942D90324CD9>.

Version 0.1.7 targets CDF Game Master Save 1.4.1 and EXPBG
GM Tools 0.1.14 (0.1.6: GM Tools 0.1.11; 0.1.5: GM Tools 0.1.8). It is an Unlisted testing release. In the first client test (dedicated server, full
production modset) a CDF save and a load in the same session succeeded; a load
after a server restart was refused by Ambient Destruction, which GM Tools 0.1.4
fixes (re-test pending). See [testing](docs/TESTING.md).

It merges the three existing EXPBG CDF companions. Each keeps its own module
folder under [`addon/`](addon) so it can be maintained on its own; the tooling
assembles them into one project, the same way EXPBG GM Tools is built.

| Module | Folder | Origin |
|---|---|---|
| Unit Caching CDF (formerly GM Optimizer CDF) | `addon/unit-caching-cdf` | `mod-gamemaster-optimizer` CDF companion 0.1.3 |
| Intel Items CDF | `addon/intel-items-cdf` | `mod-intel-items` CDF companion 0.0.4 |
| Ambient Destruction CDF | `addon/ambient-destruction-cdf` | `mod-ambient-destruction` CDF bridge 0.0.1 |
| Garrison CDF bridge | `addon/garrison-cdf` | Developed here (0.1.6; replaces the 0.1.5 guard) |
| Unit Dialog CDF | `addon/unit-dialog-cdf` | Developed here (0.1.4) |
| AI Global Skills CDF | `addon/ai-global-skills-cdf` | Developed here (0.1.4) |
| AI Surrender CDF | `addon/ai-surrender-cdf` | Developed here (0.1.4) |

Exact source commits, Git blob IDs of every imported file and the former
Workshop IDs are recorded in [`tools/pack.json`](tools/pack.json). Imported
scripts and textures are byte-identical to the published companions: class
names, resource GUIDs, prefab references and saved-data keys are unchanged.
Ambient Civilians and Ambient Sounds need no bridge: CDF saves their module
settings through native editor attributes (not exact civilians, playback
positions or transient emitters). Persistent Battlefield has no CDF state.

## Use it instead of the standalone companions

Do not enable this pack together with EXPBG GM Optimizer CDF
(`8C5A6D9E73B241F0`), EXPBG Intel Items - CDF (`E110000000000002`) or EXPBG
Ambient Destruction CDF (`D7A82F4139C60BE5`): they declare the same classes and
will not load. Those companions require the standalone EXPBG mods, which are
themselves replaced by EXPBG GM Tools.

## Requirements

Load all three on the server and every client:

| Addon | Workshop ID | Notes |
|---|---|---|
| Arma Reforger | `58D0FB3206B6F859` | Base game |
| [EXPBG GM Tools](https://reforger.armaplatform.com/workshop/FC1402F65B2F4A45) | `FC1402F65B2F4A45` | Requires 0.1.11 or later (pinned in `tools/pack.json`) |
| [CDF Game Master Save](https://reforger.armaplatform.com/workshop/6A1876F37D65AB09) | `6A1876F37D65AB09` | Targets 1.4.1; separately authored and licensed |

Not the standalone EXPBG GM Optimizer, Intel Items or Ambient Destruction mods.

CDF settings: keep `clearBeforeLoad` on (required for Unit Caching Full
snapshots, garrisons and Ambient Destruction), `repairDuplicatesOnLoad` on (required by
Ambient Destruction) and `usePersistenceBlob` off (CDF's default; required
whenever Unit Caching state is involved). Intel inventories follow CDF's
`saveInventories`.

## What is saved

- **Unit Caching**: stable Full Cache survivor snapshots (prefab, world
  transform, author) and every cache module's settings are embedded in the CDF
  file, even with CDF `saveAttributes` off. Previously enabled modules resume
  after a clear-before-load import; casualties never refill. Wait for cache
  transitions to finish before saving. Global controller commands are not
  saved or replayed. Native mission saving still needs **Prepare for save**.
  Without this pack GM Tools refuses new Full removals while CDF is loaded;
  with it, Full caching is available again and these snapshots carry it.
- **Intel Items**: each item's title, text, computer startup (spent) state and
  diagnostics setting, for world intel and the inventories CDF captures, such as
  AI and containers. Restore suppresses startup audio.
- **Ambient Destruction**: module settings, exact scenery transforms
  (wrecks, bodies, dressings, suppressed records) and the map-building
  destruction ledger, replayed before saved actors spawn.
- **Garrison**: nothing; assignments are mission-only (see below).

Invalid or unsupported snapshots are rejected before CDF clears the scene. A
Unit Caching or Ambient Destruction import that fails after clearing keeps the
original document for recovery and blocks further saves until recovery or a
fresh world.

## Limits

- CDF excludes active player characters, so their inventories are not saved.
  Restored inventory intel may use a different pocket; at most 400 intel items
  per captured inventory.
- Full survivors return with prefab-default equipment and health. Corpse
  inventories are not virtualized; mounted Full survivors must be restored
  before export.
- Ambient Destruction refuses to save while scenery is still generating or
  when the ledger would include CDF-managed buildings. Old snapshots without
  building identities (0.0.4 era) reject rather than drop history.
- Saves made **without** this pack that contain Intel Items or an Ambient
  Destruction module are rejected before clearing (they hold no Intel payload
  or exact layout); load them without the pack. Unit Caching modules without a
  snapshot load as ordinary CDF records.
- Saves made with the standalone mods and companions use the same prefabs and
  payload keys and are expected to load with GM Tools and this pack, but that
  has not been tested yet.
- A CDF save made while Full caching was active without this pack (or with an
  old standalone Optimizer build) holds the cached groups and their waypoints
  but not their removed members, so loading it recreates those groups empty.
  CDF logs `N groupes sont restes sans aucune IA` and this pack lists each one as
  `[EBG CDF EMPTY GROUP] members=never-saved ... cacheSnapshot=0`; those members
  cannot be recovered. `members=spawn-failed` means saved members failed to load
  (for example a missing mod prefab).
- Large CDF loads can freeze the server for a few seconds. Load saves before
  players join; `[CDF TIMING]` lines show how much of a freeze is adapter work.
- No universal mod compatibility or performance gain is claimed.

## Garrison

With EXPBG GM Tools 0.1.11 or later, garrisons are saved in CDF files and loaded
back exactly: building, squads, posts and patrol stops, cache state (awake,
Simulation or Full), wake and sleep distances, casualties (never respawned) and
the AI Surrender and AI Global Skills overrides; loadouts and wounds restore as
prefab defaults. The CDF document carries the GM Tools garrison ledger; CDF
never captures a garrison squad or soldier itself. All cache modes work under
CDF, Full included, and saves (autosaves too) need no Prepare for Save.

On load (`clearBeforeLoad` on) the scene's garrisons are cleared with the rest
of the scene and the saved ones are created once CDF has finished: each waits
for its building's analysis, takes its posts back and wakes with its soldiers'
AI held until they are bound (Full garrisons stay cached). A garrison whose
building is gone restores as an ordinary squad. Refused before anything changes,
with the reason in a dialog: an append load of a save with garrisons, and a save
whose garrison ledger cannot be read (another world, a missing prefab or
faction, damaged data); loading the same save again within two minutes then
loads it without its garrisons. Every Game Master is told how many garrisons
were loaded. Details in
[architecture](docs/ARCHITECTURE.md#garrison-under-cdf).

## Building

`addon/EXPBG_CDF_Compat.gproj` is the pack project. [`tools/Assemble-Pack.ps1`](tools/Assemble-Pack.ps1)
copies each module's runtime files into one project; undeclared path
collisions, duplicate resource GUIDs and Workbench-only scripts fail. The
companions share no config overrides, so nothing is merged.

With PowerShell 7:

- Portable checks: `./tests/Test-Tools.ps1`. With `mod-gm-tools` and the source
  repositories checked out beside this one, it also checks the pinned GM Tools
  commit (paths, GUIDs, classes, referenced symbols) and the source blobs.
- Native build and local install: `./build.ps1 -NonInteractive`.
- Workbench editing: `./tools/Assemble-Pack.ps1 -Destination <empty folder>`,
  then copy intended changes back into the module folder.

Native builds freeze CDF and EXPBG GM Tools into the run's `dependencies`
folder. Each is looked up by directory name in `DependencyAddonsRoots` from
`.local/config.json` (ordered, `;`-separated). The default searches the Workshop
downloads (`InstalledAddonsRoot`, folders `CDFGameMasterSave_6A1876F37D65AB09`
and `EXPBGGMTools_FC1402F65B2F4A45`) and then the Workbench addons directory,
where `mod-gm-tools/build.ps1` installs its local build as `EXPBG_GM_Tools`.
A folder with the right name but a different project GUID stops the build.
Their own dependencies are frozen too, found by project GUID in the same roots:
EXPBG GM Tools 0.1.16 and later depend on EXPBG Audio Data (`198987BE7BAC4C84`,
its sounds), so that item (a Workshop download, or the `EXPBG_Ambient_Radio_Audio`
build of its mod-audio-data folder) must be installed in one of the roots; the CDF
round-trip fixture links it like CDF. The chosen sources and hashes are recorded
in the build receipt. See
[`tools/local-config.example.json`](tools/local-config.example.json).

See [architecture](docs/ARCHITECTURE.md), [testing](docs/TESTING.md),
[artwork](docs/ASSETS.md) and [module licenses](docs/licenses).

## License

Arma Public License Share Alike (APL-SA), like all three published companions;
the Workshop item uses the same license. Each module keeps its original notices
in [`docs/licenses`](docs/licenses) and, for Intel Items, in its packaged
`EII_CDFNotices.c`. CDF Game Master Save and EXPBG GM Tools are not
redistributed and keep their own terms.
