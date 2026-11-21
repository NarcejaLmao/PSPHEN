/*
 * This file is part of PRO CFW.

 * PRO CFW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "main.h"
#include <functions.h>

char* running_ark = "Running PSP HEN in ?PS? mode";

ARKConfig default_config = {
    .magic = ARK_CONFIG_MAGIC,
    .arkpath = DEFAULT_ARK_PATH,
    .exploit_id = LIVE_EXPLOIT_ID,
    .launcher = {0},
    .exec_mode = DEV_UNK,
    .recovery = 0,
};
ARKConfig* ark_config = NULL;

extern void loadKernelArk();
extern void copyPSPVram(u32*);

// K.BIN entry point
void (* kEntryPoint)() = (void*)KXPLOIT_LOADADDR;
char* kbin_path = NULL;

int autoDetectDevice(ARKConfig* config);
int initKxploitFile(char*);
void kernelContentFunction(void);

// Entry Point
int exploitEntry(ARKConfig* arg0, UserFunctions* arg1, char* kxploit_file) __attribute__((section(".text.startup")));
int exploitEntry(ARKConfig* arg0, UserFunctions* arg1, char* kxploit_file){

    // Clear BSS Segment
    clearBSS();
    void (*KernelExitGame)() = (void*)RelocImport("LoadExecForUser", 0x05572A5F, 0);

    // init function table
    if (arg1 == NULL)
        scanUserFunctions(g_tbl);
    else
        memcpy(g_tbl, arg1, sizeof(UserFunctions));

    if (arg0 == NULL) arg0 = &default_config;

    scanArkFunctions(g_tbl);

    // copy the path of the save
    g_tbl->config = ark_config = arg0;

    // make PRTSTR available for payloads
    g_tbl->prtstr = (void *)&PRTSTR11;

    // init screen
    initScreen(g_tbl->DisplaySetFrameBuf);

    PRTSTR("PSPHEN: Stage 2 initialized");
    PRTSTR("PSPHEN: Function tables ready");
    PRTSTR("PSPHEN: Checking available memory");
    g_tbl->freeMem(g_tbl);

    // read kxploit file into memory and initialize it
    char* err = NULL;
    int res = 0;
    PRTSTR("PSPHEN: Preparing exploit payload");
    PRTSTR("Reading kxploit file...");
    if ((res=initKxploitFile(kxploit_file)) == 0){
        PRTSTR("PSPHEN: Exploit payload loaded");
        PRTSTR("Scanning stubs for kxploit...");
        if ((res=kxf->stubScanner(g_tbl)) == 0){
            PRTSTR("PSPHEN: Required exploit stubs found");
            PRTSTR("Doing kernel exploit...");
            if ((res=kxf->doExploit()) == 0){
                PRTSTR("PSPHEN: Exploit trigger completed");
                PRTSTR("Escalating privileges...");
                PRTSTR("PSPHEN: Preparing kernel transition");
                g_tbl->KernelDcacheWritebackAll();
                PRTSTR("PSPHEN: Invoking kernel callback");
                kxf->executeKernel(KERNELIFY(&kernelContentFunction));
                err = "Could not execute kernel function";
                res = -1;
            }
            else{
                err = "Exploit failed!";
            }
        }
        else{
            err = "Scan failed!";
        }
    }
    else{
        err = "Could not open kxploit file!";
    }

    PRTSTR2("PSPHEN: Execution stopped (code %p): %s", res, err);
    PRTSTR("PSPHEN: Returning to XMB...");
    g_tbl->KernelDelayThread(5000000);
    if (KernelExitGame) KernelExitGame();

    return res;
}

int autoDetectDevice(ARKConfig* config){
    // determine execution mode by scanning for certain modules
    PRTSTR("PSPHEN: Checking device environment");
    if (k_tbl->KernelFindModuleByName == NULL) return -1;
    SceModule2* kermit_peripheral = k_tbl->KernelFindModuleByName("sceKermitPeripheral_Driver");
    if (kermit_peripheral){
        SceModule2* pspvmc = k_tbl->KernelFindModuleByName("pspvmc_Library");
        if (pspvmc){
            config->exec_mode = PSV_POPS;
            PRTSTR("PSPHEN: Environment: Vita POPS");
            return 0;
        }
        else{
            SceModule2* sctrl = k_tbl->KernelFindModuleByName("SystemControl");
            if (sctrl){
                if (k_tbl->KernelFindModuleByName("ARKCompatLayer") != NULL){
                    return -1;
                }
                else{
                    config->exec_mode = PSV_ADR;
                    PRTSTR("PSPHEN: Environment: Adrenaline");
                    return 0;
                }
            }
            else{
                config->exec_mode = PS_VITA;
                PRTSTR("PSPHEN: Environment: Vita ePSP");
                return 0;
            }
        }
    }
    else{
        config->exec_mode = PSP_ORIG;
        PRTSTR("PSPHEN: Environment: PSP");
        return 0;
    }
}

// Fake K1 Kernel Setting
void setK1Kernel(void){
    __asm__ (
        "nop\n"
        "lui $k1,0x0\n"
    );
}

// Kernel Permission Function
void kernelContentFunction(void){

    PRTSTR("PSPHEN: Kernel callback reached");
    if (!isKernel()){
        PRTSTR("PSPHEN ERROR: Kernel privileges not available");
        return;
    }

    PRTSTR("PSPHEN: Kernel privileges confirmed");
    setK1Kernel();

    g_tbl->prtstr = KERNELIFY(g_tbl->prtstr);
    kxf->repairInstruction = KERNELIFY(kxf->repairInstruction);

    PRTSTR("Scanning kernel functions");
    scanKernelFunctions(k_tbl);
    PRTSTR("PSPHEN: Kernel function scan complete");

    PRTSTR("Repairing kernel");
    kxf->repairInstruction(k_tbl);
    PRTSTR("PSPHEN: Kernel exploit cleanup complete");

    if (ark_config->exec_mode == DEV_UNK){
        PRTSTR("Autodetecting device");
        if (autoDetectDevice(ark_config) < 0){
            PRTSTR("Could not detect device, aborting...");
            return;
        }
    }

    PRTSTR("PSPHEN: Kernel setup complete");
    /* Show the PS3-style activation toast only after kernel privileges and
       HEN setup have succeeded. It remains visible for 5 seconds, fading
       during the final second, before normal initialization continues. */
    showPspHenAchievement(g_tbl->KernelDelayThread);
    running_ark[20] = 'P';
    if (IS_PSP(ark_config)){
        running_ark[17] = ' ';
    }
    else{
        if (IS_VITA_ADR(ark_config)){
            running_ark[17] = 'v';
        }
        else{
            running_ark[17] = 'e';
            if (IS_VITA_POPS(ark_config)){
                running_ark[20] = 'X';
            }
        }
    }
    PRTSTR(running_ark);
    PRTSTR("PSPHEN: Starting ARK initialization");

    loadKernelArk();
}

// Clear BSS Segment of Payload
void clearBSS(void){
    extern char __bss_start, __bss_end;
    memset(&__bss_start, 0, &__bss_end - &__bss_start);
}

u32 sctrlHENFindFunction(char* mod, char* lib, u32 nid){
    return FindFunction(mod, lib, nid);
}
