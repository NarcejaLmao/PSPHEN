/*
 * FPS counter overlay for PSP HEN.
 *
 * Hooks the display driver's SetFrameBuffer function and, while the
 * "FPS Counter" debug setting is enabled, counts the frames shown per
 * second and draws "FPS nn" at the corner / edge of the screen chosen by the
 * "FPS Counter Position" debug setting (top or bottom; left, middle or right).
 * Works in the XMB and in games/homebrew (not in POPS).
 */

#include <pspsdk.h>
#include <pspkernel.h>
#include <pspinit.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <macros.h>
#include "fpscounter.h"

extern SEConfig se_config;

// sceDisplay_Service / sceDisplay_driver: sceDisplaySetFrameBufferInternal
#define NID_DISPLAY_SETFRAMEBUF_INTERNAL 0x3E17FE8D
// sceThreadManager / ThreadManForUser: sceKernelGetSystemTimeLow
#define NID_GET_SYSTEM_TIME_LOW 0x369ED59D

// overlay geometry (in screen pixels)
#define SCREEN_W    480
#define SCREEN_H    272
#define OSD_MARGIN  4   // distance from the screen edges
#define OSD_PAD     2
#define OSD_SCALE   2
#define GLYPH_W     5
#define GLYPH_H     7
#define CELL_W      ((GLYPH_W + 1) * OSD_SCALE)
#define OSD_CHARS   7   // "FPS 999"
#define BOX_W       (OSD_CHARS * CELL_W + 2 * OSD_PAD)
#define BOX_H       (GLYPH_H * OSD_SCALE + 2 * OSD_PAD)

// glyph indexes
#define G_F     10
#define G_P     11
#define G_S     12
#define G_SPACE 13

// 5x7 glyphs, one byte per row, bit 4 is the leftmost column
static const u8 glyphs[14][GLYPH_H] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
};

// hooked function and helpers
static int (* _SetFrameBufInternal)(int pri, void *topaddr, int width, int format, int sync) = NULL;
static u32 (* _GetSystemTimeLow)(void) = NULL;
static int hook_installed = 0;

// counter state
static int was_active = 0;
static u32 frame_count = 0;
static u32 last_time = 0;
static u32 current_fps = 0;

// colors per pixel format: 0 = 565, 1 = 5551, 2 = 4444, 3 = 8888
static const u32 fg_colors[4] = { 0x07E0, 0x83E0, 0xF0F0, 0xFF00FF00 }; // green
static const u32 bg_colors[4] = { 0x0000, 0x8000, 0xF000, 0xFF000000 }; // black

static void setText(u8* text, u32 fps){
    int n = 0;
    if (fps > 999) fps = 999;
    text[n++] = G_F;
    text[n++] = G_P;
    text[n++] = G_S;
    text[n++] = G_SPACE;
    if (fps >= 100) text[n++] = (fps / 100) % 10;
    if (fps >= 10)  text[n++] = (fps / 10) % 10;
    text[n++] = fps % 10;
    while (n < OSD_CHARS) text[n++] = G_SPACE;
}

// top left corner of the overlay box for the configured position
static void getOverlayOrigin(int* ox, int* oy){
    int x, y;

    switch (se_config.fps_counter_pos){
    case FPS_POS_TOP_RIGHT:
    case FPS_POS_BOTTOM_RIGHT:
        x = SCREEN_W - BOX_W - OSD_MARGIN;
        break;
    case FPS_POS_TOP_MIDDLE:
    case FPS_POS_BOTTOM_MIDDLE:
        x = (SCREEN_W - BOX_W) / 2;
        break;
    default: // left
        x = OSD_MARGIN;
        break;
    }

    switch (se_config.fps_counter_pos){
    case FPS_POS_BOTTOM_LEFT:
    case FPS_POS_BOTTOM_RIGHT:
    case FPS_POS_BOTTOM_MIDDLE:
        y = SCREEN_H - BOX_H - OSD_MARGIN;
        break;
    default: // top
        y = OSD_MARGIN;
        break;
    }

    *ox = x;
    *oy = y;
}

static void drawOverlay(void* topaddr, int stride, int format, u32 fps){
    // translate to the uncached mirror so we don't need to flush the data cache
    u32 base = ((u32)topaddr & 0x1FFFFFFF) | 0x40000000;

    u8 text[OSD_CHARS];
    setText(text, fps);

    u32 fg = fg_colors[format];
    u32 bg = bg_colors[format];

    int osd_x, osd_y;
    getOverlayOrigin(&osd_x, &osd_y);

    for (int y = 0; y < BOX_H; y++){
        for (int x = 0; x < BOX_W; x++){
            u32 color = bg;

            int tx = x - OSD_PAD;
            int ty = y - OSD_PAD;
            if (tx >= 0 && ty >= 0 && tx < OSD_CHARS * CELL_W && ty < GLYPH_H * OSD_SCALE){
                int ch = tx / CELL_W;
                int gx = (tx % CELL_W) / OSD_SCALE;
                int gy = ty / OSD_SCALE;
                if (gx < GLYPH_W && (glyphs[text[ch]][gy] & (0x10 >> gx)))
                    color = fg;
            }

            int px = osd_x + x;
            int py = osd_y + y;
            if (format == 3){
                ((volatile u32*)base)[py * stride + px] = color;
            }
            else {
                ((volatile u16*)base)[py * stride + px] = (u16)color;
            }
        }
    }
}

static int isValidFrameBuffer(void* topaddr, int width, int format){
    if (topaddr == NULL) return 0; // display is off
    if (format < 0 || format > 3) return 0;
    if (width < 128 || width > 1024) return 0;
    u32 addr = (u32)topaddr & 0x1FFFFFFF;
    if (addr >= 0x04000000 && addr < 0x04800000) return 1; // VRAM (and its mirrors)
    if (addr >= 0x08800000 && addr < 0x0C000000) return 1; // user RAM
    return 0;
}

static int SetFrameBufInternalHook(int pri, void* topaddr, int width, int format, int sync){
    if (_SetFrameBufInternal == NULL || _GetSystemTimeLow == NULL)
        return 0; // not fully set up yet

    if (se_config.fps_counter && isValidFrameBuffer(topaddr, width, format)){
        u32 now = _GetSystemTimeLow();
        if (!was_active){
            // just enabled, start measuring from scratch
            was_active = 1;
            frame_count = 0;
            current_fps = 0;
            last_time = now;
        }
        frame_count++;
        u32 elapsed = now - last_time;
        if (elapsed >= 1000000){
            current_fps = (frame_count * 1000000 + elapsed / 2) / elapsed;
            frame_count = 0;
            last_time = now;
        }
        drawOverlay(topaddr, width, format, current_fps);
    }
    else if (!se_config.fps_counter){
        was_active = 0;
    }

    return _SetFrameBufInternal(pri, topaddr, width, format, sync);
}

void fpsCounterApply(void){
    if (!se_config.fps_counter || hook_installed)
        return;

    // POPS handles its own screen
    int apitype = sceKernelInitApitype();
    if (apitype == 0x144 || apitype == 0x155)
        return;

    u32 display_func = sctrlHENFindFunction("sceDisplay_Service", "sceDisplay_driver", NID_DISPLAY_SETFRAMEBUF_INTERNAL);
    _GetSystemTimeLow = (void*)sctrlHENFindFunction("sceThreadManager", "ThreadManForUser", NID_GET_SYSTEM_TIME_LOW);
    if (!display_func || !_GetSystemTimeLow)
        return;

    HIJACK_FUNCTION(display_func, SetFrameBufInternalHook, _SetFrameBufInternal);
    hook_installed = 1;

    sctrlFlushCache();
}
