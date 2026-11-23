/*
    UMD dumper for the XMB "Debug Settings" page.
    Dumps the inserted UMD to ms0:/ISO/<DISC_ID>.iso (same folder ArkMenu scans for ISOs).
*/

#include <pspsdk.h>
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <string.h>

#include "include/umddump.h"

extern int sce_paf_private_sprintf(char *buf, const char *fmt, ...);

#define SECTOR_SIZE   2048
#define CHUNK_SECTORS 128               // 256 KB per read
#define ISO_DIR       "ms0:/ISO"

enum { ST_IDLE = 0, ST_RUNNING, ST_DONE, ST_FAILED };

char umddump_status[48] = "";
static volatile int umd_state = ST_IDLE;
static SceUID umd_thread = -1;

static void set_status(const char *s)
{
    strncpy(umddump_status, s, sizeof(umddump_status) - 1);
    umddump_status[sizeof(umddump_status) - 1] = 0;
}

int umddump_get_value(void)
{
    return (umd_state == ST_IDLE) ? 0 : 2;
}

// Read `count` sectors starting at `lba` from the disc. Returns bytes read or <0.
static int read_sectors(u32 lba, u32 count, void *buf)
{
    char path[48];
    sce_paf_private_sprintf(path, "disc0:/sce_lbn0x%x_size0x%x", lba, count);

    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return fd;

    int res = sceIoRead(fd, buf, count * SECTOR_SIZE);
    sceIoClose(fd);
    return res;
}

static int read_sectors_retry(u32 lba, u32 count, void *buf)
{
    int res = -1;
    for (int i = 0; i < 3; i++) {
        res = read_sectors(lba, count, buf);
        if (res == (int)(count * SECTOR_SIZE)) return res;
        sceKernelDelayThread(200000);
    }
    return res;
}

static int write_all(SceUID fd, u8 *buf, int size)
{
    while (size > 0) {
        int w = sceIoWrite(fd, buf, size);
        if (w <= 0) return -1;
        buf += w;
        size -= w;
    }
    return 0;
}

// Free bytes on ms0:, or -1 if unknown.
static s64 get_free_space(void)
{
    struct {
        u32 maxClusters;
        u32 freeClusters;
        int maxSectors;
        int sectorSize;
        int sectorCount;
    } info;
    void *p = &info;

    memset(&info, 0, sizeof(info));
    if (sceIoDevctl("ms0:", 0x02425818, &p, sizeof(p), NULL, 0) < 0) return -1;
    return (s64)info.freeClusters * info.sectorCount * info.sectorSize;
}

// Disc ID from UMD_DATA.BIN (e.g. "ULUS-10041"), falls back to "UMD".
static void get_disc_id(char *id)
{
    char tmp[16];
    strcpy(id, "UMD");

    SceUID fd = sceIoOpen("disc0:/UMD_DATA.BIN", PSP_O_RDONLY, 0);
    if (fd < 0) return;
    memset(tmp, 0, sizeof(tmp));
    int n = sceIoRead(fd, tmp, 10);
    sceIoClose(fd);
    if (n < 10) return;

    for (int i = 0; i < 10; i++) {
        char c = tmp[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return;
    }
    tmp[10] = 0;
    strcpy(id, tmp);
}

static void fail(const char *msg)
{
    set_status(msg);
    umd_state = ST_FAILED;
}

static int dump_thread(SceSize args, void *argp)
{
    SceUID memid = -1;
    SceUID fd = -1;
    u8 *buf = NULL;
    u32 chunk = CHUNK_SECTORS;
    char path[96], id[16];

    set_status("Starting...");

    // Allocate the transfer buffer (64 byte aligned for the UMD driver)
    while (chunk >= 16) {
        memid = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER, "umddump", PSP_SMEM_Low, chunk * SECTOR_SIZE + 64, NULL);
        if (memid >= 0) break;
        chunk >>= 1;
    }
    if (memid < 0) { fail("Failed: no memory"); goto end; }
    buf = (u8 *)(((u32)sceKernelGetBlockHeadAddr(memid) + 63) & ~63);

    // Read the ISO9660 primary volume descriptor to get the disc size
    if (read_sectors_retry(16, 1, buf) != SECTOR_SIZE) { fail("Failed: no UMD"); goto end; }
    if (memcmp(buf + 1, "CD001", 5) != 0) { fail("Failed: not a data UMD"); goto end; }

    u32 total = buf[0x50] | (buf[0x51] << 8) | (buf[0x52] << 16) | ((u32)buf[0x53] << 24);
    if (total == 0 || total > 0x100000) { fail("Failed: bad disc size"); goto end; }

    // Free space check
    s64 need = (s64)total * SECTOR_SIZE;
    s64 freespace = get_free_space();
    if (freespace >= 0 && freespace < need + 1024 * 1024) { fail("Failed: not enough space"); goto end; }

    // Pick an unused file name
    sceIoMkdir(ISO_DIR, 0777);
    get_disc_id(id);
    sce_paf_private_sprintf(path, ISO_DIR "/%s.iso", id);
    for (int n = 1; n < 100; n++) {
        SceIoStat st;
        if (sceIoGetstat(path, &st) < 0) break;
        sce_paf_private_sprintf(path, ISO_DIR "/%s_%d.iso", id, n);
    }

    fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) { fail("Failed: can't create ISO"); goto end; }

    int last_pct = -1;
    for (u32 lba = 0; lba < total; ) {
        u32 count = total - lba;
        if (count > chunk) count = chunk;

        if (read_sectors_retry(lba, count, buf) != (int)(count * SECTOR_SIZE)) {
            sceIoClose(fd); fd = -1;
            sceIoRemove(path);
            fail("Failed: UMD read error");
            goto end;
        }
        if (write_all(fd, buf, count * SECTOR_SIZE) < 0) {
            sceIoClose(fd); fd = -1;
            sceIoRemove(path);
            fail("Failed: write error");
            goto end;
        }
        lba += count;

        int pct = (int)(((u64)lba * 100) / total);
        if (pct != last_pct) {
            last_pct = pct;
            sce_paf_private_sprintf(umddump_status, "Dumping %d%%", pct);
        }
    }

    sceIoClose(fd); fd = -1;
    sce_paf_private_sprintf(umddump_status, "Done: %s.iso", id);
    umd_state = ST_DONE;

end:
    if (fd >= 0) sceIoClose(fd);
    if (memid >= 0) sceKernelFreePartitionMemory(memid);
    umd_thread = -1;
    return sceKernelExitDeleteThread(0);
}

void umddump_start(void)
{
    if (umd_state == ST_RUNNING) return;

    umd_state = ST_RUNNING;
    set_status("Starting...");

    umd_thread = sceKernelCreateThread("umddump", dump_thread, 0x30, 0x4000, 0, NULL);
    if (umd_thread < 0) { fail("Failed: thread"); return; }
    sceKernelStartThread(umd_thread, 0, NULL);
}
