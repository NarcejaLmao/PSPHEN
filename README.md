# PSP HEN

A minimal debug-only custom firmware for PSP / PS Vita (ePSP), forked from ARK-4,
in the spirit of PS3HEN / GoldHEN: it enables the hardware/firmware for homebrew and
debugging, and exposes **only debug settings**.

## Settings (XMB: Settings > ★Debug Settings (last entry in the Settings column), or the Recovery menu)

| Setting     | Values                                              | Notes |
|-------------|------------------------------------------------------|-------|
| QA Flags    | Disabled / Enabled                                   | Unlocks Sony's QA/debug flags (extra hidden options in System Settings). |
| VSH Region  | Default, Japan, America, ... Debug I, Debug II       | Spoofs the VSH region, including the dev/debug regions. (XMB menu only) |

Everything else from ARK-4's settings UI (clocks, Inferno cache, UMD options, LEDs,
Plugins Manager, Custom Launcher, etc.) has been removed from the menus.
Plugin *loading* still works from the existing config files; only the UI was removed.


## ISO and EBOOT launching

The ARK-4 game manager and launch path are retained in PSP HEN. Put PSP game
images in `ms0:/ISO/` (or `ef0:/ISO/`) and homebrew/game packages in
`ms0:/PSP/GAME/<TITLE>/EBOOT.PBP` (or the corresponding `ef0:` path).
The ISO backend supports plain ISO plus the compressed formats handled by
ARK-4's Inferno reader (CSO, ZSO, DAX and JSO). EBOOT entries are classified
from their PBP metadata and dispatched to the homebrew, PSN, POPS or updater
launch path as appropriate.

PBP headers are now validated before their section offsets are used, so
truncated, malformed or non-PBP files are rejected instead of being treated
as executable entries.

## Building
Same as ARK-4: install the PSPDEV toolchain, then run `./build.sh` / `make`.
Not yet built or tested on hardware.

## Credits / License
Based on ARK-4 (PSP-Archive), PRO CFW, ME CFW, XmbControl. GPLv3 - see the license headers in the source.
