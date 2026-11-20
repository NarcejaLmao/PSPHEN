/*
 * Crash dump to Memory Stick for PSP HEN.
 *
 * Installs a default exception handler (when the "Crash Dump" debug setting is
 * enabled) that writes a text report to <ms0:|ef0:>/CRASHDUMP/CRASH_nn.TXT.
 * See crashdump.h for details.
 *
 * Design notes:
 *  - The entry stub (crashdump_asm.S) saves the registers and switches to a private
 *    stack before calling crashDumpHandler(), so stack overflows can be reported too.
 *  - Everything the handler needs is static, nothing is allocated while crashing.
 *  - Memory is only read after checking that the address is inside RAM, and the
 *    report is written in pieces. If the handler itself faults, the part written so
 *    far is flushed and closed instead of being lost.
 */

#include <pspsdk.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspdebug.h>
#include <pspinit.h>
#include <stdio.h>
#include <string.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <ark.h>
#include "imports.h"
#include "rebootex.h"
#include "crashdump.h"

extern SEConfig se_config;
extern ARKConfig* ark_config;

#define DUMP_DIR_NAME   "CRASHDUMP"
#define MAX_DUMPS       99      // CRASH_01.TXT ... CRASH_99.TXT, the last one is overwritten after that

#define STACK_SIZE      8192    // private stack used while writing the report
#define IOBUF_SIZE      2048
#define STACK_WORDS     64      // raw stack words listed in the report
#define STACK_SCAN      512     // stack words searched for code addresses (2 KB)
#define MAX_TRACE       32      // max code addresses listed from the stack scan

// entry stub (crashdump_asm.S)
extern void _crashDumpExceptionHandler(void);
static void crashDumpHandler(PspDebugRegBlock* regs);

// variables used by the entry stub
static PspDebugRegBlock crash_regs_block;
static u8 crash_stack[STACK_SIZE] __attribute__((aligned(16)));
PspDebugRegBlock* crashdump_regs = &crash_regs_block;
void (* crashdump_handler)(PspDebugRegBlock*) = crashDumpHandler;
u32 crashdump_stack_top = 0;

static int installed = 0;
static volatile int in_handler = 0;

// output state
static SceUID dump_fd = -1;
static char iobuf[IOBUF_SIZE];
static int iolen = 0;
static char tmp[160];

static const char* exc_names[32] = {
    "Interrupt", "TLB modification", "TLB load/inst fetch", "TLB store",
    "Address load/inst fetch", "Address store", "Bus error (instr)",
    "Bus error (data)", "Syscall", "Breakpoint", "Reserved instruction",
    "Coprocessor unusable", "Arithmetic overflow", "Unknown 13",
    "Unknown 14", "Unknown 15", "Unknown 16", "Unknown 17", "Unknown 18",
    "Unknown 19", "Unknown 20", "Unknown 21", "Unknown 22", "Unknown 23",
    "Unknown 24", "Unknown 25", "Unknown 26", "Unknown 27", "Unknown 28",
    "Unknown 29", "Unknown 30", "Unknown 31"
};

static const char* reg_names[32] = {
    "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

/* ------------------------------ output helpers ------------------------------ */

static void flushOut(void){
    if (dump_fd >= 0 && iolen > 0)
        sceIoWrite(dump_fd, iobuf, iolen);
    iolen = 0;
}

static void putStr(const char* s){
    int n = strlen(s);
    if (n > IOBUF_SIZE) n = IOBUF_SIZE;
    if (iolen + n > IOBUF_SIZE) flushOut();
    memcpy(iobuf + iolen, s, n);
    iolen += n;
}

#define OUT(...) do { snprintf(tmp, sizeof(tmp), __VA_ARGS__); putStr(tmp); } while (0)

/* ------------------------------ memory helpers ------------------------------ */

// only RAM present on every model (first 32 MB) is ever read by the report
static int isValidRam(u32 addr, u32 len){
    u32 a = addr & 0x1FFFFFFF;
    return (a >= 0x08000000) && (a < 0x0A000000) && ((0x0A000000 - a) >= len);
}

static SceModule2* findModule(u32 addr){
    if (!isValidRam(addr, 4)) return NULL;
    return (SceModule2*)sceKernelFindModuleByAddress((unsigned int)addr);
}

// module whose code (text) segment contains addr, or NULL
static SceModule2* findCodeModule(u32 addr){
    SceModule2* mod = findModule(addr);
    if (mod != NULL && addr >= mod->text_addr && addr < mod->text_addr + mod->text_size)
        return mod;
    return NULL;
}

static void putAddr(const char* label, u32 addr){
    SceModule2* mod = findModule(addr);
    if (mod != NULL)
        OUT("%s%08X  %s+0x%X\n", label, addr, mod->modname, addr - mod->text_addr);
    else
        OUT("%s%08X\n", label, addr);
}

/* ------------------------------ report sections ------------------------------ */

static void writeHeader(void){
    OUT("PSP HEN crash dump\n");
    OUT("===================\n");
    OUT("Version   : %s\n", PSPHEN_VERSION_STRING);
    OUT("Firmware  : 0x%08X\n", sceKernelDevkitVersion());
    if (rebootex_config.game_id[0] != 0)
        OUT("Game ID   : %.9s\n", rebootex_config.game_id);
    OUT("Apitype   : 0x%03X\n", sceKernelInitApitype());

    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    int thid = sceKernelGetThreadId();
    if (sceKernelReferThreadStatus(thid, &ti) == 0){
        OUT("Thread    : \"%.31s\" (uid 0x%08X, priority %d)\n", ti.name, thid, ti.currentPriority);
        putAddr("Entry     : ", (u32)ti.entry);
        OUT("Stack     : 0x%08X, size 0x%X\n", (u32)ti.stack, (u32)ti.stackSize);
    }
    else {
        OUT("Thread    : uid 0x%08X\n", thid);
    }
    OUT("\n");
}

static void writeException(PspDebugRegBlock* regs){
    OUT("Exception : %s (code %d)%s\n", exc_names[(regs->cause >> 2) & 31], (int)((regs->cause >> 2) & 31),
        (regs->cause & 0x80000000) ? ", in a branch delay slot" : "");
    putAddr("EPC       : ", regs->epc);
    putAddr("RA        : ", regs->r[31]);
    OUT("BadVAddr  : %08X\n", regs->badvaddr);
    OUT("Cause     : %08X\n", regs->cause);
    OUT("Status    : %08X\n", regs->status);
    OUT("\n");
}

static void writeRegisters(PspDebugRegBlock* regs){
    OUT("Registers\n");
    for (int i = 0; i < 32; i += 4){
        OUT(" %s:%08X %s:%08X %s:%08X %s:%08X\n",
            reg_names[i], regs->r[i], reg_names[i+1], regs->r[i+1],
            reg_names[i+2], regs->r[i+2], reg_names[i+3], regs->r[i+3]);
    }
    OUT(" hi:%08X lo:%08X\n\n", regs->hi, regs->lo);
}

static void writeCodeAroundEPC(PspDebugRegBlock* regs){
    u32 epc = regs->epc;
    OUT("Code around EPC\n");
    if (!isValidRam(epc - 16, 36) || (epc & 3)){
        OUT(" (EPC is not a valid code address)\n\n");
        return;
    }
    for (int i = -4; i <= 4; i++){
        u32 addr = epc + i * 4;
        OUT(" %08X: %08X%s\n", addr, _lw(addr), (i == 0) ? "   <-- EPC" : "");
    }
    OUT("\n");
}

static void writeStack(PspDebugRegBlock* regs){
    u32 sp = regs->r[29];

    OUT("Stack (sp = %08X)\n", sp);
    if ((sp & 3) || !isValidRam(sp, 4)){
        OUT(" (sp is not a valid stack address)\n\n");
        return;
    }

    // raw words
    for (int i = 0; i < STACK_WORDS; i += 4){
        if (!isValidRam(sp + i * 4, 16)) break;
        OUT(" %08X: %08X %08X %08X %08X\n", sp + i * 4,
            _lw(sp + i * 4), _lw(sp + i * 4 + 4), _lw(sp + i * 4 + 8), _lw(sp + i * 4 + 12));
    }
    OUT("\n");

    // code addresses found on the stack, a rough call trace (may contain stale entries)
    OUT("Code addresses found on the stack (module+offset)\n");
    int found = 0;
    for (int i = 0; i < STACK_SCAN && found < MAX_TRACE; i++){
        u32 addr = sp + i * 4;
        if (!isValidRam(addr, 4)) break;
        u32 value = _lw(addr);
        if (value & 3) continue;
        SceModule2* mod = findCodeModule(value);
        if (mod != NULL){
            OUT(" sp+%03X: %08X  %s+0x%X\n", i * 4, value, mod->modname, value - mod->text_addr);
            found++;
        }
    }
    if (found == 0) OUT(" (none)\n");
    OUT("\n");
}

/* ------------------------------ file handling ------------------------------ */

// opens the next free CRASH_nn.TXT, returns the file descriptor
static SceUID openDumpFile(void){
    char dev[5] = "ms0:";
    char dir[32];
    char path[48];

    // use the same storage as the PSP HEN folder (ms0: or ef0:)
    const char* arkpath = ark_config->arkpath;
    if (arkpath[0] != 0 && arkpath[1] != 0 && arkpath[2] != 0 && arkpath[3] == ':'){
        memcpy(dev, arkpath, 4);
    }

    snprintf(dir, sizeof(dir), "%s/" DUMP_DIR_NAME, dev);
    sceIoMkdir(dir, 0777);

    int n;
    for (n = 1; n < MAX_DUMPS; n++){
        snprintf(path, sizeof(path), "%s/CRASH_%02d.TXT", dir, n);
        SceUID test = sceIoOpen(path, PSP_O_RDONLY, 0777);
        if (test < 0) break; // doesn't exist, use this one
        sceIoClose(test);
    }
    snprintf(path, sizeof(path), "%s/CRASH_%02d.TXT", dir, n);
    return sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
}

static void closeDump(void){
    flushOut();
    if (dump_fd >= 0){
        sceIoClose(dump_fd);
        dump_fd = -1;
    }
}

/* ------------------------------ handler ------------------------------ */

// leave the crashed program: back to the XMB, or halt if the XMB itself crashed
static void finish(void){
    if (sceKernelInitApitype() < 0x200){
        sctrlKernelExitVSH(NULL);
    }
    // crash inside the XMB: stay here, so a crashing XMB doesn't reboot endlessly
    while (1) sceKernelDelayThread(1000000);
}

static void crashDumpHandler(PspDebugRegBlock* regs){
    pspSdkSetK1(0); // we never return, no need to restore it

    if (in_handler){
        // the handler itself crashed (e.g. unreadable memory): keep what was written so far
        OUT("\n*** Exception while writing this report: %s at %08X (BadVAddr %08X) ***\n",
            exc_names[(regs->cause >> 2) & 31], regs->epc, regs->badvaddr);
        closeDump();
        finish();
    }
    in_handler = 1;

    if (se_config.crash_dump){
        dump_fd = openDumpFile();
        if (dump_fd >= 0){
            writeHeader();
            writeException(regs);
            writeRegisters(regs);
            writeCodeAroundEPC(regs);
            writeStack(regs);
            OUT("End of report\n");
            closeDump();
        }
    }

    finish();
}

void crashDumpApply(void){
#ifdef DEBUG
    // DEBUG builds have their own on-screen exception handler
    return;
#else
    if (!se_config.crash_dump || installed)
        return;

    crashdump_stack_top = (((u32)crash_stack) + STACK_SIZE - 16) & ~0xF;
    if (sceKernelRegisterDefaultExceptionHandler((void*)_crashDumpExceptionHandler) >= 0)
        installed = 1;
#endif
}
