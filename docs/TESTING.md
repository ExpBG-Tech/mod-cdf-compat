# EXPBG CDF Compat validation gates

## 0.1.0 status (2026-10-05)

- Native compile (2026-10-05, `build/local-20261005-110226-638`): `./build.ps1
  -NonInteractive` exit 0 against installed CDF Game Master Save 1.4.1 and the
  local EXPBG GM Tools build; no `Can't compile` or `SCRIPT (E)`. Only CDF's own
  obsolete `Deserialize` warning and the stock Workbench resource-leak report at
  teardown. Rebuilt against the EXPBG GM Tools 0.1.2 build (`build/local-20261005-115720-872`),
  same result. No game session or CDF save/load was run.

- `tests/Test-Tools.ps1` (PowerShell 7) passes: repository structure, pack
  assembly (11 files, no merges, no duplicate GUIDs), assembler negative
  fixtures, imported-file provenance (9 files equal their recorded Git blobs and
  the pinned source commits), identity/dependency/Workshop metadata, the
  Garrison guard, GM Tools overlap and symbol resolution against `v0.1.2`
  (`8d3ff39`), local install, dependency snapshots and release guards.
- The imported scripts and the Intel EDDS are byte-identical to the installed
  published Workshop payloads (Optimizer CDF 0.1.3, Intel Items - CDF 0.0.4,
  Ambient Destruction CDF 0.0.1), extracted read-only from their `data.pak`.
- The standalone companions passed their own native/CDF gates against the
  standalone mods; see their repositories. That evidence does not cover this
  pack, EXPBG GM Tools or the three adapters compiled together.

## Open gates

| Gate | Required evidence |
| --- | --- |
| Native compile | Passed 2026-10-05 (see above); repeat against the published GM Tools version before each release |
| Adapter layering | Each adapter's refusal is preserved with all three in one module (capture refusal, restore preflight rejection before clearing) |
| Unit Caching | Full-cache zones under CDF, export, cold import with clear before load; survivors at captured transforms, casualties not refilled, settings restored |
| Intel Items | World item and AI/container inventory round trips; title/text/spent/diagnostics; no startup audio on restore |
| Ambient Destruction | Exact scenery and building ledger replay with `clearBeforeLoad` and `repairDuplicatesOnLoad`; rejection when either is off |
| Garrison | Full selected under CDF reports the hold and keeps actors awake; Simulation sleep/wake; CDF save refused while a garrison is active; Prepare for Save releases garrisons, then save; clear-before-load with a Simulation-cached garrison releases it without recreating soldiers |
| Older saves | A document written with the standalone mods and companions loads with GM Tools and this pack; a GM Tools-only document with Intel or Destruction rejects before clearing |
| Multiplayer | Dedicated server and client with the same frozen modset |
| Publication | Immutable `v0.1.0` tag, Workbench Publish Project (first publication), Unlisted APL-SA listing, five package files and hashes |

For cold CDF proof save a new file, preserve and hash it, end the world, start a
fresh one and load that file. Warm reloads, JSON round trips and direct setters
are not cold restoration.

Do not load the standalone CDF companions (or their local builds) in the same
session: their classes duplicate this pack's.
