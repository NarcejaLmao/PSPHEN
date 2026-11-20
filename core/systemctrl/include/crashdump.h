#ifndef _CRASHDUMP_H_
#define _CRASHDUMP_H_

/*
 * Crash dump to Memory Stick (debug setting "Crash Dump").
 *
 * While enabled, a CPU exception (bad address, reserved instruction, bus error, ...)
 * in the XMB, a game or a homebrew writes a text report to
 *
 *     <ms0:|ef0:>/CRASHDUMP/CRASH_nn.TXT
 *
 * with the exception details, thread and module information, registers, the
 * code around the faulting instruction and a trace of code addresses found
 * on the stack (module + offset). Then, if the crash was in a game/homebrew,
 * it returns to the XMB. A crash inside the XMB itself is only reported and
 * then halts, so a crashing XMB can't get stuck in a reboot loop.
 *
 * Call crashDumpApply() whenever se_config.crash_dump may have changed: it
 * installs the exception handler the first time the setting is found enabled.
 * Switching the setting off later stops the dump from being written (the
 * handler itself stays installed until the next reboot).
 *
 * Not available in DEBUG builds, which have their own on-screen handler.
 */
void crashDumpApply(void);

#endif
