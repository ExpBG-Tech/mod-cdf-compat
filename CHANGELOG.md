# EXPBG CDF Compat changelog

## 0.1.3 (unreleased)

- Unit Caching: restored AI characters now count for their saved Game Master
  author, so clearing or deleting restored AI no longer logs
  `AuthorEntityRemovedServer - This should not happen` or lowers another
  entity's count. `[EBG CDF AUTHORS]` lines summarise each restore and name any
  delete whose author is not registered.
- Unit Caching: after a CDF load, every group that ended without AI is listed
  (`[EBG CDF EMPTY GROUP]`) with the reason (`never-saved`, `spawn-failed`,
  `saved-dead`, `not-in-group`) and whether the save held a Unit Caching
  snapshot for it.
- Intel Items: restoring carried intel is matched per inventory by prefab (one
  scan before and one after spawning missing items) and reuses the load's
  validation instead of parsing every payload twice.
- All three adapters print `[CDF TIMING]` lines once per load for Restore and
  Apply, separating their own time from CDF's.

## 0.1.2

- Ambient Destruction: when a CDF save or load is refused, the Game Master now
  sees a dialog with the reason instead of only a server log line (CDF itself
  reports results as hints, which are invisible with hints disabled). A load
  started while a previous Ambient Destruction import is still running or failed
  now says so.
- Requires EXPBG GM Tools 0.1.4, which also lets a saved destroyed building load
  after a server restart when native mission persistence already removed it.

## 0.1.1

- First Workshop publication of EXPBG CDF Compat; content identical to 0.1.0.
  The 0.1.0 publish attempt timed out at the Workbench Publish Project step before
  any upload, and that version stays retired under the release guard.

## 0.1.0

- EXPBG CDF Compat becomes one addon (`07BC942D90324CD9`, the identity reserved by
  EXPBG GM Tools) for EXPBG GM Tools 0.1.2 (`FC1402F65B2F4A45`) and CDF Game Master
  Save 1.4.1 (`6A1876F37D65AB09`). It merges the published CDF companions, each in
  its own module folder with unchanged script and texture bytes:
  - Unit Caching CDF, formerly EXPBG GM Optimizer CDF 0.1.3 (`cdf-v0.1.3`,
    `c22b354`; identical at Optimizer 0.1.31 `1ae137d`).
  - Intel Items CDF 0.0.4 (`cdf-v0.0.4`, `8b947b9`; identical at `aa08748`).
  - Ambient Destruction CDF 0.0.1 (published from `d0176bb`; identical at
    v0.0.9 `2d4eb24`).
- Class names, resource GUIDs, prefab references and saved-data keys are kept, so
  saves written by the standalone companions are expected to stay loadable
  (not yet tested). The former parents (`F3B7C6FB18AB1F79`, `E110000000000001`,
  `E2A47D19C8B6503F`) and companion IDs appear only in provenance records.
- Do not combine with the standalone CDF companions or the standalone EXPBG mods.
- Garrison: new guard keeps Full garrison caching refused while CDF is loaded and
  reports why; Simulation stays available. CDF saves still wait for Unit Caching
  Prepare for Save to release every garrison.
- Build tooling mirrors EXPBG GM Tools. Native builds snapshot CDF and EXPBG GM
  Tools from Workshop downloads or the local Workbench build (ordered
  `DependencyAddonsRoots`) and record which copy was used. New portable checks
  cover pack assembly, imported-file provenance, identities and, with a local
  mod-gm-tools checkout, overlap and symbol resolution against the pinned commit.
- Native compilation, CDF save/load round trips and multiplayer acceptance are
  tracked separately in docs/TESTING.md.
