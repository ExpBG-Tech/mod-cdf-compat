# Sources and attribution

**Structures For GM byHeine**, Heine, installed version 1.2.29, APL-SA.
Source: https://reforger.armaplatform.com/workshop/628EDA2ABC937159
License and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license-share-alike
Only the armored laptop and Tablet 2 model/material/texture closure is imported.
Resource GUIDs and paths have been remapped under `EIIArt/`; texture bytes are unchanged.
Compiled-model reference replacements preserve byte lengths. No endorsement is implied.
The exact 15 input/output paths, identities and SHA256 hashes are recorded in `docs/imported-assets.json`.

**Game Master Enhanced**, GME Mod Team, installed version 1.3.8, APL.
Source: https://reforger.armaplatform.com/workshop/5964E0B3BB7410CE
Upstream: https://github.com/zen-mod/GME_AR
License and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license
`EII_StringAttributeVar.c`, `EII_EditboxEditorAttributeUIComponent.c` and the two EXPII editbox layouts adapt GME's string-attribute serialization and widget integration. Names, references and private state are namespaced; byte limits and cleanup were added. GME's modded serializer super-chain is preserved. These adapted files retain APL. The addon does not require or redistribute the complete GME mod.

**Bohemia Interactive**: vanilla field manual/notebook models, materials, core inventory, editor and widget resources are referenced from the base game and are not copied into this repository.

**Microsoft Windows XP startup recording**: supplied by the user as `Microsoft Windows XP Startup Sound_0_5.wav`. Imported byte-for-byte as `Audio/EXPII/EII_Startup.wav`. This recording is not relicensed under APL-SA. File metadata and source SHA256 are recorded in `docs/audio-source.json`; this source build does not establish publication clearance.

**EXPBG shared build/install tooling**, ExpBG Tech, MIT. Adapted from `mod-ambient-sounds`; the installer additionally permits model `.xob` files. Full retained notice: `licenses/EXPBG-tools-MIT.txt`.

**EXPBG GM Optimizer release tooling**, ExpBG Tech (M.Pac and K.Edgar), APL-SA. `release.ps1`, release configuration/staging/publication helpers and their portable tests are adapted from revision `10429eb6341a34fd1015641f306a5427ad66bb07` of `mod-gamemaster-optimizer`. Changes select Intel identities/versions/previews, require explicit orchestrator slots, enforce Unlisted first-publication flow, preserve custom upstream notices, and include attribution in source stages. Same APL-SA license/warranty URL as original contributions above. Existing MIT tools retain their separate notice.

**Artwork**: user-supplied EXPBG emblem and source screenshots used as references with the built-in image-generation tool. Final cards and root-supplied master art are documented in `docs/artwork.md`. They are illustrative catalogue cards; the actual in-game meshes remain the referenced/imported objects.

**CDF GameMaster Save** is an optional dependency, not redistributed. The companion targets observed 1.4.1 hooks and stores upstream state without interpreting or replacing it.

Required runtime attribution is also included as a comment-only script so a Workshop resource package retains it.
