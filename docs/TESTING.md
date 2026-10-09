# EXPBG CDF Compat validation gates

## Garrison bridge (Unreleased)

Portable: `tests/Test-PackProvenance.ps1` checks the bridge (handshake, capture
with backstop, clear predicate, validation and refusals before CDF changes the
scene, recovery, finalization, dialog) and that the retired guard is gone.
`tests/Test-DependencyOverlap.ps1` skips until the pinned GM Tools commit exists
(`PENDING` until release); run it again after the pin is filled in.

Native (orchestrator slot), in one diagnostic server with installed CDF 1.4.1:

```powershell
pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EXPG_CDFGarrisonRoundTrip.c -ExpectResult '\[EXPG CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 garrisons=3 documentWritten=1 ownedRecords=0 ordinary=1 appendRefused=1 posts=1 patrollers=[1-9]\d* respawnedDead=0 duplicates=0 aiBeforeBind=0 fullWoke=1 corruptRefused=1 corruptSkipped=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
```

Garrisons A (awake), B (Simulation) and C (Full, interior patrollers), one
casualty in A and in C, an ordinary authored squad D: real CDF capture, the
document written to and read from a file, an append load refused untouched, a
load with `clearBeforeLoad` restoring exactly the three garrisons (no casualty
respawned, no duplicate, D once, AI held until bound), C woken onto its posts,
and a save with an unreadable ledger refused once and loaded without garrisons
on confirmation. It is warm (one process): a cold restart, GM dialogs and
multiplayer remain live gates.

## Prisoner bridge (Unreleased)

Portable: `tests/Test-PrisonersCdf.ps1` checks the `eprPrisoner`/`eprSquad`
envelopes, unwrap before super, prisoner records added inside CDF's Capture,
refusals before the clear, the spawn-time hold, the bounded pass after CDF's
deferred state pass, the summary line, legacy clearing, players untouched, ACE
reached by name only and the fixture wiring.

Native (orchestrator slot), in one diagnostic server with installed CDF 1.4.1:

```powershell
pwsh -File tests/Run-CdfRoundTrip.ps1 -GmToolsSnapshot <built EXPBG_GM_Tools> -SourceSnapshot <built EXPBG_CDF_Compat> -FixturePath tests/EPR_CDFPrisonersRoundTrip.c -ExpectResult '\[EXPG PRISONER CDF ROUNDTRIP RESULT\] checks=[1-9]\d* failures=0 saved=(?:2 restored=2 esr=2 ace=skipped held=2|3 restored=3 esr=2 ace=1 held=3) duplicates=0 brokenRefused=1 legacyCleared=1 reason=completed' -TimeoutSeconds 600 -OrchestratorSlotGranted
```

An authored US fire team; two soldiers surrender through AI Surrender (one
plain, one partly interrogated: attempts, revealed squad, intel answer, squad
override, dossier). Real CDF capture (both prisoners added once, the team
tokened), a broken prisoner envelope refused before the clear, a load holding
every prisoner passive as CDF spawns it, then the same prisoners once each with
their record, squad link, dossier, side, interrogation point, passive AI, no
weapons and pose; a save without prisoner records (0.1.10 behaviour) clears
them. The runner loads no ACE, so the ACE handcuffed case prints one skip line
(`ace=skipped`); with ACE Captives loaded it expects `saved=3 ... ace=1`.
Warm (one process): a cold restart, prisoners in vehicles, ACE escort, GM
dialogs and multiplayer remain live gates.

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
| Garrison | Native in-process round trip `tests/Run-CdfRoundTrip.ps1` (see below); then a live GM save (and an autosave) with awake, Simulation and Full garrisons active, a cold server restart and load: same garrisons, posts, cache states, no duplicates, no `[EBG CDF HOLD]`; append load and an unreadable ledger refused with the dialog; a client joining after the load sees the squads |
| Prisoners | Native round trip `tests/EPR_CDFPrisonersRoundTrip.c` (see above); then a live GM save with AI Surrender prisoners (seated, mid-interrogation) and, in the production modset, ACE surrendered and handcuffed AI, a cold restart and load: same prisoners once, interrogation points answer as before, nobody re-armed or hostile, `[EXPBG CDF PRISONERS] ... failed=0`; a 0.1.10 save still removes stale prisoners |
| Older saves | A document written with the standalone mods and companions loads with GM Tools and this pack; a GM Tools-only document with Intel or Destruction rejects before clearing |
| Multiplayer | Dedicated server and client with the same frozen modset |
| Publication | Immutable `v0.1.1` tag (0.1.0 retired: its Publish Project step timed out before upload), Workbench Publish Project (first publication), Unlisted APL-SA listing, five package files and hashes |

For cold CDF proof save a new file, preserve and hash it, end the world, start a
fresh one and load that file. Warm reloads, JSON round trips and direct setters
are not cold restoration.

Do not load the standalone CDF companions (or their local builds) in the same
session: their classes duplicate this pack's.
