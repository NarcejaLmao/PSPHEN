# PSPHEN changes

## System-wide FPS counter
- Added a user-mode `fpscounter.prx` overlay plugin that runs in ARK's VSH, game, homebrew, and POPS runlevels.
- The plugin reads the saved `show_fps` setting from `ARKMENU.BIN`, checks it once per second, and draws a compact FPS readout in the top-left of the active display framebuffer.
- Registered the plugin in the default ARK `PLUGINS.TXT` and added it to the main build and distribution targets.
- Removed ArkMenu's separate menu-only FPS rendering so the same overlay is used in the menu and while playing.
- The FPS option remains disabled by default and persists using the existing menu settings file.
- The XMB **Debug Settings** page (xmbctrl) now also lists an **FPS Counter** (Disabled/Enabled) item. It reads and writes the same `show_fps` byte in `ARKMENU.BIN` as ArkMenu, so both menus and the overlay plugin stay in sync.

Note: the reported rate is based on framebuffer flips. Games that render in-place or use nonstandard buffering may report a lower estimate than their internal render rate. Build and test on PSP hardware with the project's PSPSDK toolchain.

## Build timestamp robustness
- The build script now detects source files whose modification times are ahead of the local system clock and normalizes only those files before invoking GNU make.
- This avoids the WSL/ZIP-extraction clock-skew warnings seen in the supplied build log without touching normal source timestamps or forcing a full rebuild.
- The supplied `build(1).log` reaches `Build Done`; its remaining make output is timestamp-skew and compiler warnings, not a fatal build error.

## FPS counter overlay fix
- `fpscounter.prx` never ran: its `exports.exp` had no `syslib` section (no `module_start`/`module_info` exports) and `imports.S` declared `SystemCtrlForUser` with kernel-library flags. Both now match the project's other user modules.
- Removed `-lpspdisplay_driver` (kernel stubs) from the plugin's link line.
- The plugin no longer uses stdio/snprintf (it is built with `-mno-crt0`, so libc is not initialised); it reads `ARKMENU.BIN` with `sceIo*` and formats the label by hand. Thread stack raised to 16 KB.
- Pixels are written through the uncached VRAM mirror, and the old even-frame check that made the readout flicker was removed.

## Games not launching: FPS plugin boot safety
- Now that `fpscounter.prx` really loads, it runs in every runlevel, started very early at boot. Its thread ran at priority 0x18 and called `sceDisplayWaitVblankStart()` in a loop with no fallback, so if the display wasn't ready yet the call returned immediately and the thread spun at higher priority than the game/boot threads, hanging game launch.
- The thread now runs at priority 0x40, waits 4 s after start before doing anything, always sleeps each iteration (falls back to a 16 ms delay if the vblank wait fails), and while the option is Disabled only wakes every 250 ms to re-read one byte of `ARKMENU.BIN`.
- `module_stop` now waits for / terminates the overlay thread and deletes it, so the module is never unloaded under a running thread (e.g. when launching a game from ArkMenu).
- ArkMenu: the PBP header validator treats an unknown file size (`ftell` failing on files >= 2 GB, such as large POPS images) as "size unknown" instead of rejecting the EBOOT.
- To confirm the plugin is the cause on a given build: set `always, fpscounter.prx, off` in `ARK_01234/PLUGINS.TXT` (or delete the line); games should launch again immediately.

## Build fix: `undefined reference to __bss_start / __bss_end` (kernel_loader)
- The top-level Makefile runs sub-makes with `-j$(nproc)`. In `loader/live/kernel/{kernel_loader,psxloader,kxploit}` the `main.elf` link had no dependency on the generated linker script `link.l`, so under `-j` it could link against an empty or half-written script (no `__bss_start`/`__bss_end`). A stale empty `link.l` from an earlier raced build was also never regenerated.
- All eight kernel loader Makefiles that use `genlink.py` now: write `link.l` atomically (temp file + `mv`), always regenerate it (`.PHONY: link.l`), and make `main.elf` wait for it with an order-only prerequisite (`main.elf: $(C_OBJS) | link.l`).
- The implicit-declaration / incompatible-pointer warnings in the log are legacy ARK-4 code warnings, not errors.

## Changes from ARK-4
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
