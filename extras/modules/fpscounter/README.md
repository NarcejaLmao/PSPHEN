# ARK FPS Counter overlay

This user-mode ARK plugin draws a compact FPS readout directly into the active PSP display framebuffer. It is loaded in all ARK runlevels through `contrib/PSP/SAVEDATA/ARK_01234/PLUGINS.TXT` and reads the persisted `show_fps` option from `ARKMENU.BIN`.

The counter is disabled by default. Enable **Settings → Debug: FPS Counter → Enabled** in ArkMenu. The overlay checks the setting once per second, so changes take effect without restarting the game. It samples display-buffer flips over one-second windows and draws at the top-left of the screen.

The value is an estimate based on display framebuffer flips. Some titles render in-place or use unusual buffering, so their reported rate may be lower than the game's internal render rate. The plugin supports the common 32-bit, RGB565, and RGBA5551 PSP framebuffer formats.
