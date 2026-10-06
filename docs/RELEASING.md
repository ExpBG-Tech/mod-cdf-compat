# Releasing and release housekeeping

Releases run locally on the Windows Workbench PC with PowerShell 7. Publication
needs the orchestrator's native-slot release (see [AGENTS.md](../AGENTS.md)).

- `./release.ps1` builds, stages and packs the current `VERSION` without
  uploading (local dry run).
- `./release.ps1 -Publish` tags, uploads with the Workbench command line,
  verifies the uploaded package, attaches the source zip and Workshop receipt
  to a GitHub release and publishes it.
- `./release.ps1 -Publish -Interactive` uses the Workbench Publish form instead,
  for listing text changes.

Every attempted upload leaves `artifacts/<run>/workshop-receipt.json`. Never
delete it: `Assert-NoWorkshopUploadAttempt` reads every receipt and refuses to
upload a version that was already attempted.

## What each run leaves behind

All of this lives in ignored folders (`build/`, `artifacts/`, `.local/`).
Run folders are named `<family>-yyyyMMdd-HHmmss-fff` (UTC).

| Created by | Folder | Contents | Read later by |
| --- | --- | --- | --- |
| `build.ps1` (`tools/Build-Addon.ps1`) | `build/local-<utc>/` | frozen pack `EXPBG_CDF_Compat/` with its `resourceDatabase.rdb`, `PC/` build output, `profile-data/` logs, `dependencies/` (frozen CDF Game Master Save and EXPBG GM Tools, 0.6-0.8 GB) + `dependencies.json`, `receipt.json`, `source-manifest.json` | `.local/last-build.json` names the build installed into the Workbench addons directory; `dependencies.json` records which installed CDF and GM Tools copies were frozen |
| `release.ps1` (`tools/Stage-Release.ps1`) | `artifacts/workshop-local-<utc>/` | `Assembled/`, `Stage/`, `EXPBG_CDF_Compat_<version>_source.zip`, `receipt.json`, `source-manifest.json` | the zip is attached to the GitHub release of that run |
| `tools/Invoke-WorkshopRelease.ps1` | same folder | `workshop-receipt.json` with the frozen dependency record (some older UI runs also hold a manual `listing-verification.json`) | `Assert-NoWorkshopUploadAttempt` (every version ever attempted), `release.ps1` (upload and package checks of the current run) |
| `tools/Invoke-WorkshopRelease.ps1` | `.local/workshop-local-<utc>/` | `built/`, `packed/` (`data.pak`, project, `resourceDatabase.rdb`, `previewImage.png`, `manifest.json`), `dependencies/` + `dependencies.json`, `profile/`, `ui-published/` and `previous-ui-bundle/` (interactive), `change-note.txt`, `*-output.log`, `*-engine-logs/` (0.6-0.8 GB, mostly `dependencies/`) | the interactive form of the current run uses `packed/previewImage.png` and `packed/manifest.json` |
| `release.ps1 -Publish` | `.local/workshop-local-<utc>-notes.md` | GitHub release notes | nothing |

`Get-LocalReleaseSource` reads Git (tags, `main`), not run folders.

## Release housekeeping

[`tools/Invoke-ReleaseHousekeeping.ps1`](../tools/Invoke-ReleaseHousekeeping.ps1)
(identical to the EXPBG GM Tools copy) moves superseded heavy payloads out of
the checkout into an archive. It never deletes anything (except the explicit
`-Purge` below) and never touches anything outside `build/<run>/`,
`artifacts/<run>/` and `.local/workshop-local-<utc>/`. `.local/config.json`,
`.local/last-build.json`, notes and all other `.local` content stay where they
are. Folder names that do not follow the run pattern are left alone.

### Configuration

Set the archive in `.local/config.json` (see
[local-config.example.json](../tools/local-config.example.json)):

```json
{ "ArchiveRoot": "G:/EXPBG-archive" }
```

Payloads go to `<ArchiveRoot>/mod-cdf-compat/<same relative path>`, for example
`G:/EXPBG-archive/mod-cdf-compat/artifacts/workshop-local-<utc>/Stage`. A value
that already ends in the repository folder name (`G:/EXPBG-archive/mod-cdf-compat`)
is used as is. Without `ArchiveRoot` (or with the example placeholder, a
relative path, a path inside the repository or inside any other Git checkout,
or a linked path) nothing is moved and one line says how much could be
archived. The Git check also catches aliases of this checkout (8.3 short
names, `\\localhost\C$` shares) that a plain path comparison misses.

### When it runs

- Automatically, only at the end of a successful `./release.ps1 -Publish`
  (after the receipt confirms `uploaded` and `packageVerified` and the GitHub
  release is published). It passes `-PublishedRun <run>` and re-checks that
  receipt. A failed or dry-run release never triggers it. A housekeeping problem
  only prints a warning; the release has already succeeded.
- By hand:

```powershell
./tools/Invoke-ReleaseHousekeeping.ps1 -WhatIf -Verbose   # plan only, per-run decisions and sizes
./tools/Invoke-ReleaseHousekeeping.ps1                    # move
./tools/Invoke-ReleaseHousekeeping.ps1 -KeepReleases 2 -KeepBuilds 1 -KeepRuns 5
./tools/Invoke-ReleaseHousekeeping.ps1 -ArchiveRoot G:/EXPBG-archive -WhatIf
```

It skips itself while another release or housekeeping run of this repository
holds the release mutex. While a native engine is running (game, dedicated
server or Workbench, retail or Diag), every `build/` run is kept.

### Keep rules

Kept in full (nothing moves):

1. The newest `-KeepReleases` (default 1) published releases per Workshop item
   (`workshop-receipt.json` with `uploaded` and `packageVerified`), both the
   `artifacts/` and `.local/` halves, plus the run that was just published.
2. The newest `-KeepBuilds` (default 1) `build/local-*` snapshots and the build
   named in `.local/last-build.json`.
3. The newest `-KeepRuns` (default 3) runs of every other run family, per area
   (this repository creates none today; the rule is shared with EXPBG GM Tools).
4. Anything modified within `-MinAgeHours` (default 24; the newest file write or
   the name stamp counts), unless it is a release run of an older version than
   the newest published release of its item. The version comes from
   `workshop-receipt.json`, `receipt.json` or the source zip name. An earlier
   attempt of the published version itself is not superseded and only follows
   the age rule.

Every other run is slimmed:

- Stays in place: all `*.json`, `*.jsonl`, `*.log`, `*.txt`, `*.md`, `*.csv`
  outside addon payloads (receipts, manifests, change notes, results, logs) and
  the Workshop `manifest.json` beside a packed or published bundle, plus other
  small files.
- Moves: addon payloads (any folder holding a `.gproj`: `Stage`, `Assembled`,
  `built`, `packed`, `ui-published`, `previous-ui-bundle`, `PC`, frozen
  snapshots, dependency and fixture copies), `*.zip`, `*.pak`, `*.7z` and any
  other file of 1 MB or more. A folder without evidence moves as a whole.

The slimmed run folder gets an `archived.json` pointer (`"role": "source"`)
listing what moved where; the archive copy gets the same record with
`"role": "archive"`.

### How items move

- Same volume: one atomic rename per item.
- Different volume (C: to G:): copy beside the destination, verify the file
  count and every file size (`-VerifyHash` adds SHA-256) and that the source did
  not change meanwhile (count, sizes, newest write time), rename the copy into
  place, rename the source within its own folder, then remove it. That rename
  fails while any file below the source is open, so a source in use is never
  half deleted: its archive copy is withdrawn and the source stays whole. A
  failed or locked copy is rolled back in the archive and the source stays.
- An item that already exists in the archive is left in place and reported.
- Junctions, symbolic links, mount points and other reparse points are never
  followed, moved or deleted; a folder containing one is slimmed around it. An
  item whose archive path runs through a link is skipped. Same-volume renames
  are used only when no link sits above the checkout.
- Paths longer than 260 characters are handled (PowerShell 7 / .NET).

The summary prints moved items and bytes, run folders kept in full (with their
size), the archive path and free space of the repository volume (C: on the
release PC) and the archive volume before and after (`-WhatIf`: now and the
expected value). To restore a payload, move it back to the same relative path.

### Purge (explicit only)

```powershell
./tools/Invoke-ReleaseHousekeeping.ps1 -Purge -OlderThanDays 90 -WhatIf
./tools/Invoke-ReleaseHousekeeping.ps1 -Purge -OlderThanDays 90
```

`-Purge` permanently deletes archive run folders whose last `archived.json`
event is older than the given number of days. Only records with
`"role": "archive"` count; the `"source"` pointers beside the kept receipts
never do, so a purge pointed at a checkout by mistake cannot delete evidence.
It works only inside
`<ArchiveRoot>/mod-cdf-compat`, skips folders without an `archived.json` record
and folders holding a link, lists what it would delete and asks for
confirmation (a non-interactive session fails unless `-Confirm:$false` is
given). `release.ps1` never purges. The evidence that stayed in the checkout is
not touched.

### Tests

`tests/Test-ReleaseHousekeeping.ps1` (run by `tests/Test-Tools.ps1`) builds fake
run folders in a temporary directory outside the checkout
(`expbg-housekeeping-test-<guid>`) and checks the keep/move decisions, that
evidence and the newest snapshot stay, that `-WhatIf` and a run without
`ArchiveRoot` change nothing, that archives inside a Git checkout are refused,
that nothing is lost or duplicated, link targets (also links inside the archive)
are untouched, failed, locked or in-use copies leave the source whole (also
beyond 260 characters), purge needs confirmation, stays inside the archive and
ignores source-side pointers, and that `release.ps1` calls housekeeping once,
only after the verified publish, and never with `-Purge`.
