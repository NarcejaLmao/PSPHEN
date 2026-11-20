#include <pspkernel.h>
#include <pspdisplay.h>
#include <psptypes.h>
#include <string.h>
#include <stddef.h>
#include <ark.h>
#include <conf.h>

PSP_MODULE_INFO("ARK FPS Counter", PSP_MODULE_USER, 1, 0);

#define SAMPLE_USEC 1000000

static volatile int running = 1;
static unsigned int last_frame;
static unsigned int sample_start;
static unsigned int frame_count;
static unsigned int fps_value;
static unsigned int config_check;
static int overlay_enabled;

/* Compact 3x5 glyphs: digits, F, P, S, colon. */
static const unsigned char glyphs[14][5] = {
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7},
    {5,5,7,1,1}, {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1},
    {7,5,7,5,7}, {7,5,7,1,7}, {7,4,6,4,4}, {6,5,6,5,6},
    {7,4,7,4,4}, {0,2,0,2,0}
};

static int glyph_index(char c){
    if (c >= '0' && c <= '9') return c - '0';
    if (c == 'F') return 10;
    if (c == 'P') return 11;
    if (c == 'S') return 12;
    return 13;
}

static unsigned short rgb565(unsigned int c){
    return (unsigned short)((((c >> 19) & 0x1f) << 11) | (((c >> 10) & 0x3f) << 5) | ((c >> 3) & 0x1f));
}

static void put_pixel(void *fb, int stride, int format, int x, int y, unsigned int color){
    if (!fb || x < 0 || x >= 480 || y < 0 || y >= 272) return;
    if (format == PSP_DISPLAY_PIXEL_FORMAT_8888){
        ((unsigned int*)fb)[y * stride + x] = color;
    } else if (format == PSP_DISPLAY_PIXEL_FORMAT_565){
        ((unsigned short*)fb)[y * stride + x] = rgb565(color);
    } else if (format == PSP_DISPLAY_PIXEL_FORMAT_5551){
        unsigned short v = (unsigned short)((((color >> 19) & 0x1f) << 10) | (((color >> 11) & 0x1f) << 5) | (((color >> 3) & 0x1f) << 0) | 0x8000);
        ((unsigned short*)fb)[y * stride + x] = v;
    }
}

static void draw_text(void *fb, int stride, int format, int x, int y, const char *s, unsigned int color){
    int i, row, col;
    for (i = 0; s[i]; i++){
        const unsigned char *g = glyphs[glyph_index(s[i])];
        for (row = 0; row < 5; row++)
            for (col = 0; col < 3; col++)
                if (g[row] & (1 << (2 - col)))
                    put_pixel(fb, stride, format, x + i * 4 + col, y + row, color);
    }
}

static int fps_enabled(void){
    ARKConfig ark;
    char path[ARK_PATH_SIZE + 32];
    unsigned char value = 0;
    int i, n = 0;
    SceUID fd;
    memset(&ark, 0, sizeof(ark));
    sctrlArkGetConfig(&ark);
    if (!ark.arkpath[0]) return 0;
    for (i = 0; ark.arkpath[i] && n < ARK_PATH_SIZE - 1; i++) path[n++] = ark.arkpath[i];
    for (i = 0; MENU_SETTINGS[i]; i++) path[n++] = MENU_SETTINGS[i];
    path[n] = 0;
    fd = sceIoOpen(path, PSP_O_RDONLY, 0777);
    if (fd < 0) return 0;
    if (sceIoLseek32(fd, offsetof(t_conf, show_fps), PSP_SEEK_SET) < 0 || sceIoRead(fd, &value, 1) != 1) value = 0;
    sceIoClose(fd);
    return value != 0;
}

/* Builds "FPS:nnn" without libc formatting. */
static void make_label(char *out, unsigned int v){
    char tmp[12];
    int n = 0, i = 0;
    out[i++] = 'F'; out[i++] = 'P'; out[i++] = 'S'; out[i++] = ':';
    if (v > 999) v = 999;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (n) out[i++] = tmp[--n];
    out[i] = 0;
}

/* Safety notes (this runs inside every game/XMB session, loaded very early at boot):
 *  - low priority, so it can never starve the game or ARK's own boot threads;
 *  - waits a few seconds before touching anything;
 *  - every loop iteration sleeps (a failed vblank wait must not turn into a busy loop);
 *  - while the option is disabled it only wakes up to re-read one byte of ARKMENU.BIN;
 *  - module_stop terminates the thread before the module's code is unloaded. */
#define OVERLAY_PRIORITY   0x40
#define STARTUP_DELAY_USEC 4000000
#define IDLE_SLICE_USEC     250000
#define FRAME_FALLBACK_USEC  16667

static SceUID overlay_thid = -1;

static void overlay_draw(unsigned int fps){
    void *fb = NULL;
    int stride = 0, format = 0;
    if (sceDisplayGetFrameBuf(&fb, &stride, &format, PSP_DISPLAY_SETBUF_IMMEDIATE) < 0 || !fb || stride <= 0) return;
    if (fb != (void*)last_frame){
        last_frame = (unsigned int)fb;
        frame_count++;
    }
    if (format == PSP_DISPLAY_PIXEL_FORMAT_8888 || format == PSP_DISPLAY_PIXEL_FORMAT_565 || format == PSP_DISPLAY_PIXEL_FORMAT_5551){
        char label[16];
        int x, y;
        /* VRAM must be written through the uncached mirror or the pixels may never reach the screen. */
        fb = (void*)((unsigned int)fb | 0x40000000);
        /* Small opaque backing keeps the counter readable over bright games. */
        for (y = 4; y < 17; y++)
            for (x = 4; x < 47; x++)
                put_pixel(fb, stride, format, x, y, 0xD0000000);
        make_label(label, fps);
        draw_text(fb, stride, format, 8, 8, label, 0xFFFFFFFF);
    }
}

static int overlay_thread(SceSize args, void *argp){
    unsigned int waited;
    (void)args; (void)argp;

    /* let the system / game finish booting before doing anything */
    for (waited = 0; running && waited < STARTUP_DELAY_USEC; waited += IDLE_SLICE_USEC)
        sceKernelDelayThread(IDLE_SLICE_USEC);

    while (running){
        unsigned int now = sceKernelGetSystemTimeLow();

        if (!overlay_enabled || (unsigned int)(now - config_check) >= SAMPLE_USEC){
            int was_enabled = overlay_enabled;
            overlay_enabled = fps_enabled();
            config_check = now;
            if (overlay_enabled && !was_enabled){
                /* just switched on: start a fresh measurement window */
                frame_count = 0;
                fps_value = 0;
                last_frame = 0;
                sample_start = now;
            }
        }

        if (!overlay_enabled){
            sceKernelDelayThread(IDLE_SLICE_USEC);
            continue;
        }

        if (sceDisplayWaitVblankStart() < 0)
            sceKernelDelayThread(FRAME_FALLBACK_USEC); /* display not ready: never spin */

        now = sceKernelGetSystemTimeLow();
        if ((unsigned int)(now - sample_start) >= SAMPLE_USEC){
            fps_value = frame_count;
            frame_count = 0;
            sample_start = now;
        }
        overlay_draw(fps_value);
    }
    return 0;
}

int module_start(SceSize args, void *argp){
    (void)args; (void)argp;
    running = 1;
    overlay_thid = sceKernelCreateThread("ARK_FPS_Overlay", overlay_thread, OVERLAY_PRIORITY, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (overlay_thid >= 0) sceKernelStartThread(overlay_thid, 0, NULL);
    return 0;
}

int module_stop(SceSize args, void *argp){
    SceUInt timeout = 2000000;
    (void)args; (void)argp;
    running = 0;
    if (overlay_thid >= 0){
        if (sceKernelWaitThreadEnd(overlay_thid, &timeout) < 0)
            sceKernelTerminateThread(overlay_thid);
        sceKernelDeleteThread(overlay_thid);
        overlay_thid = -1;
    }
    return 0;
}
