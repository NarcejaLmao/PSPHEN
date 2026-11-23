# PSPHEN 1.0.0 - Initial Release

- Prevent the sceUID exploit from using the Vita 3.60 kernel offset when the detected kernel signature is unknown.
- Validate exploit memory allocations and fake-UID placement before attempting the kernel trigger.
- Clean up temporary allocations on early failure.
- Prevent the sceUID exploit from using the Vita 3.60 kernel offset when the detected kernel signature is unknown.
- Validate exploit memory allocations and fake-UID placement before attempting the kernel trigger.
- Clean up temporary allocations on early failure.
- extras/modules/xmbctrl/main.c: items table reduced to QA Flags + VSH Region; XMB top-level
  entries reduced to a single "★Debug Settings" (shown at the bottom of the Settings column); Plugins Manager / Custom Launcher /
  Custom App / Reboot 1.50 entries not added; per-model skip logic removed.
- extras/menus/recovery/submenu.c, main.c: every model's settings list reduced to QA Flags; relabelled.
- contrib/SETTINGS.TXT: default config reduced to qaflags=off.
- common/include/ark.h: added PSPHEN_VERSION_STRING.
- Internal file names (ARK_01234, ARK4.BIN, ...) intentionally unchanged so installers/loaders keep working.
- Hardened ARK-4 EBOOT.PBP header/section handling: validate PBP magic, file bounds and ordered offsets before parsing or reading sections; reject malformed/truncated PBP files.
- Added "FPS Counter Position" debug setting (Top Left / Top Right / Top Middle / Bottom Left / Bottom Right / Bottom Middle; default Top Left). SETTINGS.TXT key: `fpspos:<topleft|topright|topmiddle|bottomleft|bottomright|bottommiddle>`. Available in the XMB Debug Settings, the recovery menu and the launcher; takes effect immediately without a reboot.
- Added "Crash Dump" debug setting (off by default). On a CPU exception in the XMB, a game or a homebrew, a report (exception, thread, registers, code around EPC, stack and a module+offset trace) is written to `<ms0:|ef0:>/CRASHDUMP/CRASH_nn.TXT`, then the XMB is restored (a crash in the XMB itself only halts, to avoid reboot loops). SETTINGS.TXT key: `crashdump`. New files: core/systemctrl/src/crashdump.c, crashdump_asm.S, include/crashdump.h. Not available in DEBUG builds, which keep the on-screen handler.
- Added "UMD Dumper" to the XMB Debug Settings. With a UMD inserted, choose **Start Dump** to copy the disc to `ms0:/ISO/<DISC_ID>.iso` (the folder ArkMenu scans for ISOs; `_1`, `_2`... appended if the name exists). It runs in a background thread; the item's value shows live status (`Dumping 42%`, `Done: ULUS-10041.iso`, or a `Failed: ...` reason). Checks free space first and deletes the partial file on any read/write error. New files: extras/modules/xmbctrl/umddump.c, include/umddump.h.
- Hardened the `sceUID` live kernel exploit used by the PSP 6.60 HEN path: unknown kernel-layout signatures now abort before writing the libc clock pointer instead of silently assuming the Vita 3.60 offset; exploit-memory allocations and the planted UID block are checked before continuing, and temporary allocations are released on pre-trigger failures.
- Fix ARK initialization null checks and reboot validation.
- 