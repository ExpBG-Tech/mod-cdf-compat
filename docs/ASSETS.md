# EXPBG CDF Compat artwork

No new artwork was created. The Workshop preview reuses approved companion art.

Preview (`tools/project.json` `preview`):
`addon/intel-items-cdf/UI/Textures/EXPII/EII_CDF_Workshop.png`, 1672 x 941,
the published preview of EXPBG Intel Items - CDF 0.0.4 (byte-identical to the
EXPBG Intel Items Workshop banner; provenance in `mod-intel-items/docs/artwork.md`).

Candidates considered (every companion reused its main mod's banner):

| Companion | Banner | Title shown | Size |
|---|---|---|---|
| GM Optimizer CDF 0.1.3 | `addon/unit-caching-cdf/UI/Textures/EXPBG/EXPBG_CDF_Workshop_Banner.png` | GM OPTIMIZER | 1024 x 576 |
| Intel Items - CDF 0.0.4 | `addon/intel-items-cdf/UI/Textures/EXPII/EII_CDF_Workshop.png` | INTEL ITEMS | 1672 x 941 |
| Ambient Destruction CDF 0.0.1 | `mod-ambient-destruction/tools/workshop-preview.png` (not in this repository) | AMBIENT DESTRUCTION | 1672 x 941 |

The Intel banner was chosen: it is already in a module folder at the
companion's own preview path, matches the 1672 x 941 EXPBG GM Tools banner, and
its maps, notebook and laptop on a desk suit a save/records pack. The Optimizer
banner shows the retired "GM Optimizer" name, which the pack renames to Unit
Caching, at lower resolution.

Known limit: the banner title reads INTEL ITEMS, not CDF COMPAT. A dedicated
banner in the EXPBG black and antique-gold style would need new approved art.

The Intel EDDS (`E110000000000041`) is the companion's native Workbench import
and is packed as before; nothing references it at runtime. The Optimizer PNG has
no metadata and was not packed by the standalone companion either.
