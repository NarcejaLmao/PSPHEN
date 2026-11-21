/*
 * This file is part of PRO CFW.

 * PRO CFW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * PRO CFW is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PRO CFW. If not, see <http://www.gnu.org/licenses/ .
 */

#include "graphics.h"
#include "lib.h"
#include <pspdisplay.h>
#include <string.h>

#define IS_ALPHA(color) (((color)&0xff000000)==0xff000000?0:1)
#define FRAMEBUFFER_SIZE (PSP_LINE_SIZE*SCREEN_HEIGHT*4)
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define ABGR(a, b, g, r)    ((((a)<<24) & 0xff000000) | (((b)<<16) & 0x00ff0000) | (((g)<<8) & 0x0000ff00) | ((r) & 0x000000ff))

typedef union 
{
    int rgba;
    struct 
    {
        char r;
        char g;
        char b;
        char a;
    } c;
} color_t;

extern u8 msx[];

extern void (*screen_handler)(u32 vram);

int gY = 0;

void cls()
{
    colorDebug(0);
    gY = 0;
}

void initScreen(int (*DisplaySetFrameBuf)(void*, int, int, int))
{
    initColorDebug();
    if(DisplaySetFrameBuf != NULL){
        DisplaySetFrameBuf((void *)0x04000000, 512, PSP_DISPLAY_PIXEL_FORMAT_8888, 1);
    }
    cls();
}

void printTextScreen(int x, int y, const char * text, u32 color)
{
    int c, i, j, l;
    u8 *font;
    u32* vram_ptr;
    u32* vram;

    for (c = 0; text[c]; c++) {
        if (x < 0 || x + 8 > SCREEN_WIDTH || y < 0 || y + 8 > SCREEN_HEIGHT) break;
        char ch = text[c];
        vram = g_vram_base + x + y * PSP_LINE_SIZE;
        
        font = &msx[ (int)ch * 8];
        for (i = l = 0; i < 8; i++, l += 8, font++) {
            vram_ptr  = vram;
            for (j = 0; j < 8; j++) {
                if ((*font & (128 >> j))) *vram_ptr = color;
                vram_ptr++;
            }
            vram += PSP_LINE_SIZE;
        }
        x += 8;
    }
}


/* PS3-style PSP HEN activation toast. The saved pixels let the banner fade
 * back to the exact console contents that were underneath it. */
#define ACHIEVEMENT_X 12
#define ACHIEVEMENT_Y 12
#define ACHIEVEMENT_W 260
#define ACHIEVEMENT_H 52
#define ACHIEVEMENT_FRAMES 300
#define ACHIEVEMENT_FADE_START 240

static u32 achievementBackground[ACHIEVEMENT_W * ACHIEVEMENT_H];

static u32 blendColor(u32 from, u32 to, unsigned int amount)
{
    unsigned int inv = 255 - amount;
    unsigned int r = (((from & 0xFF) * inv) + ((to & 0xFF) * amount)) / 255;
    unsigned int g = ((((from >> 8) & 0xFF) * inv) + (((to >> 8) & 0xFF) * amount)) / 255;
    unsigned int b = ((((from >> 16) & 0xFF) * inv) + (((to >> 16) & 0xFF) * amount)) / 255;
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

static void fillRect(int x, int y, int width, int height, u32 color)
{
    int row, col;
    for (row = 0; row < height; ++row)
    {
        u32 *pixel = g_vram_base + (y + row) * PSP_LINE_SIZE + x;
        for (col = 0; col < width; ++col) pixel[col] = color;
    }
}

static void drawAchievement(unsigned int opacity)
{
    int x, y;
    const u32 panel = 0xFF171A20;
    const u32 blue = 0xFF3B9DEB;
    const u32 white = 0xFFFFFFFF;
    const u32 muted = 0xFFB9C5D2;

    /* Restore the original console pixels, then composite the toast. */
    for (y = 0; y < ACHIEVEMENT_H; ++y)
    {
        u32 *dst = g_vram_base + (ACHIEVEMENT_Y + y) * PSP_LINE_SIZE + ACHIEVEMENT_X;
        for (x = 0; x < ACHIEVEMENT_W; ++x)
        {
            u32 toast = panel;
            if (x < 4) toast = blue;
            else if (y == 0 || y == ACHIEVEMENT_H - 1) toast = 0xFF39424D;
            dst[x] = toast;
        }
    }

    /* Small trophy mark and the two-line achievement label. */
    fillRect(ACHIEVEMENT_X + 17, ACHIEVEMENT_Y + 16, 5, 13, blue);
    fillRect(ACHIEVEMENT_X + 13, ACHIEVEMENT_Y + 16, 4, 4, blue);
    fillRect(ACHIEVEMENT_X + 22, ACHIEVEMENT_Y + 16, 4, 4, blue);
    fillRect(ACHIEVEMENT_X + 16, ACHIEVEMENT_Y + 29, 10, 3, blue);
    fillRect(ACHIEVEMENT_X + 13, ACHIEVEMENT_Y + 32, 16, 3, blue);

    /* Text is drawn into a temporary overlay and blended with the saved
       framebuffer below, so its edges fade along with the panel. */
    printTextScreen(ACHIEVEMENT_X + 39, ACHIEVEMENT_Y + 10,
                    "PSP HEN ACTIVATED", white);
    printTextScreen(ACHIEVEMENT_X + 39, ACHIEVEMENT_Y + 27,
                    "Achievement unlocked", muted);

    /* Composite the complete panel, icon, and text over the saved console
       pixels so every part of the notification fades together. */
    for (y = 0; y < ACHIEVEMENT_H; ++y)
    {
        u32 *dst = g_vram_base + (ACHIEVEMENT_Y + y) * PSP_LINE_SIZE + ACHIEVEMENT_X;
        for (x = 0; x < ACHIEVEMENT_W; ++x)
        {
            u32 original = achievementBackground[y * ACHIEVEMENT_W + x];
            dst[x] = blendColor(original, dst[x], opacity);
        }
    }
}

void showPspHenAchievement(void (*delayThread)(unsigned int))
{
    int x, y, frame;
    if (delayThread == NULL) return;

    for (y = 0; y < ACHIEVEMENT_H; ++y)
    {
        const u32 *src = g_vram_base + (ACHIEVEMENT_Y + y) * PSP_LINE_SIZE + ACHIEVEMENT_X;
        for (x = 0; x < ACHIEVEMENT_W; ++x)
            achievementBackground[y * ACHIEVEMENT_W + x] = src[x];
    }

    for (frame = 0; frame < ACHIEVEMENT_FRAMES; ++frame)
    {
        unsigned int opacity = 255;
        if (frame >= ACHIEVEMENT_FADE_START)
            opacity = (unsigned int)(255 * (ACHIEVEMENT_FRAMES - 1 - frame) /
                                     (ACHIEVEMENT_FRAMES - ACHIEVEMENT_FADE_START));
        drawAchievement(opacity);
        if (screen_handler) screen_handler((u32)g_vram_base);
        delayThread(16667); /* ~60 Hz: 5 seconds total, last second fades out. */
    }

    /* Restore the console exactly after the toast has disappeared. */
    for (y = 0; y < ACHIEVEMENT_H; ++y)
    {
        u32 *dst = g_vram_base + (ACHIEVEMENT_Y + y) * PSP_LINE_SIZE + ACHIEVEMENT_X;
        for (x = 0; x < ACHIEVEMENT_W; ++x)
            dst[x] = achievementBackground[y * ACHIEVEMENT_W + x];
    }
    if (screen_handler) screen_handler((u32)g_vram_base);
}

/*
 * Scroll the active PSP framebuffer up by one text row.  Copy complete
 * framebuffer rows (including the unused stride pixels) so the display
 * contents and the text cursor stay in sync.
 */
#define CONSOLE_LINE_HEIGHT 12
#define CONSOLE_GLYPH_HEIGHT 8

static void scrollConsole(void)
{
    int y;
    const int rowsToMove = SCREEN_HEIGHT - CONSOLE_LINE_HEIGHT;

    /* Move rows upward in-place.  Destination is always below source. */
    for (y = 0; y < rowsToMove; ++y)
    {
        u32 *dst = g_vram_base + (y * PSP_LINE_SIZE);
        const u32 *src = g_vram_base + ((y + CONSOLE_LINE_HEIGHT) * PSP_LINE_SIZE);
        memcpy(dst, src, PSP_LINE_SIZE * sizeof(u32));
    }

    /* Clear only the newly exposed rows at the bottom of the display. */
    memset(g_vram_base + (rowsToMove * PSP_LINE_SIZE), 0,
           CONSOLE_LINE_HEIGHT * PSP_LINE_SIZE * sizeof(u32));

    /* The next message belongs on the final available text row. */
    gY = SCREEN_HEIGHT - CONSOLE_GLYPH_HEIGHT;
}

void print_to_screen_color(const char * text, u32 color)
{
    if (text == NULL) return;

    /* Scroll before drawing whenever the next glyph would leave the screen. */
    if (gY < 0) gY = 0;
    if (gY + CONSOLE_GLYPH_HEIGHT > SCREEN_HEIGHT)
        scrollConsole();

    printTextScreen(0, gY, text, color);
    gY += CONSOLE_LINE_HEIGHT;
}

unsigned int gPrintColor = 0xFFFFFFFF;

void setPrintColorRGB(char r, char g, char b)
{
    gPrintColor = ABGR((unsigned int)255, (unsigned int)b, (unsigned int)g, (unsigned int)r);
}


void print_to_screen(const char * text) 
{
      print_to_screen_color(text, gPrintColor);
}

void PRTSTR11(const char* A, unsigned long B, unsigned long C, unsigned long D, unsigned long E, unsigned long F, unsigned long G, unsigned long H, unsigned long I, unsigned long J, unsigned long K, unsigned long L)
{
  char buff[512];
  for (int i=0; i<sizeof(buff); i++) buff[i] = 0;
  mysprintf11(buff, A, (unsigned long)B, (unsigned long)C, (unsigned long)D,  (unsigned long)E, (unsigned long)F, (unsigned long)G, (unsigned long)H,  (unsigned long)I, (unsigned long) J, (unsigned long) K, (unsigned long) L);
  print_to_screen(buff);
  if (screen_handler) screen_handler((u32)g_vram_base);
}
