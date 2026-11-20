The compatibility layer is a new core CFW module introduced in PSP HEN that allows the CFW to be booted under different devices and environments.

It serves as a device driver and is loaded right after SystemControl, allowing it to fix or extend the CFW for the device or configuration it's currently running on.

The compat layer works completely transparent and can interact with the rest of the CFW, but the rest of the CFW is unaware of the compat layer's inner workings.

While the idea of a compatibility layer is new to the PSP HEN CFW, other well known homebrew already made use of the idea,
special mentions to `InfinityControl`, `TMControl` and `PopsCore` from `Infinity 2`, `Time Machine` and `PopsLoader` respectively.

The main compat layers for PSP HEN are the following:

- `PSP`: allows PSP HEN to work on retail `PSP` hardware as well as devkits and testkits. Based on `PRO` with some enhancements from `ME`.
- `Vita`: allows PSP HEN to work on the retail `PS Vita`'s PSP Emulator (aka. `ePSP`). Based on earlier `PSP HEN` versions.
- `VitaPops`: allows PSP HEN to work on retail `PS Vita`'s PS1 Emulator (aka. `ePSX`), essentially a heavily limited and restricted pspemu. Based on `TN-X` with enhancements from `ARK-3`.
- `Pentazemin`: allows PSP HEN to work on the modified `PS Vita`'s PSP Emulator running `Adrenaline` (aka. `vPSP`). Based on `Adrenaline`.

Compatibility layers are also responsible for defining the Rebootex binary to be loaded by the CFW on reboot.
Unlike classic CFW, PSP HEN's SystemControl does not have a rebootex binary built-into it.
Rebootex is heavily tied to the device and environment running, thus it's also part of the Compat Layer.
