/***************************************************************************
 * ClickGotchi animated menu scene (core side)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "cg_scene.h"

#ifdef CG_SCENE_ENABLED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "system.h"
#include "kernel.h"
#include "tick.h"
#include "button.h"
#include "string-extra.h"
#include "backdrop.h"

/* Speed: 0 Smooth ~30 fps, 1 Simulation 10 fps, 2 Efficient 1 fps
 * (same three values as the plugin's render_speed). */
int cg_scene_speed = 0;

/* The scene lives in one static buffer the size of a theme backdrop. */
#define CG_PIXELS (LCD_BACKDROP_BYTES / sizeof(fb_data))
static fb_data cg_fb[CG_PIXELS] __attribute__((aligned(4)));
_Static_assert(sizeof(cg_fb) == LCD_BACKDROP_BYTES, "scene buffer != backdrop size");

#define W LCD_WIDTH
#define H LCD_HEIGHT

static bool cg_valid;
static long cg_last_frame;               /* tick of the last scene redraw */
static volatile long cg_last_use;        /* tick of the last cg_scene_attach() */
static volatile bool cg_in_use;
static bool cg_task_added;
static long cg_next_wake;                /* tick task only */

/* ---- drawing primitives (straight into cg_fb) ---- */

/* RGB565 constant of the plugin art as the target's native pixel */
static fb_data px(uint16_t c)
{
    return LCD_RGBPACK(((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63,
                       (c & 31) * 255 / 31);
}

static void fr(uint16_t c, int x, int y, int w, int h)
{
    int x1 = x + w, y1 = y + h;
    fb_data v;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > W) x1 = W;
    if (y1 > H) y1 = H;
    if (x1 <= x || y1 <= y) return;
    v = px(c);
    for (; y < y1; y++)
        memset16(&cg_fb[y * W + x], v, x1 - x);
}

static void ln(uint16_t c, int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;   /* -|dy| */
    int err = dx + dy;
    fb_data v = px(c);
    for (;;)
    {
        int e2;
        if (x0 >= 0 && x0 < W && y0 >= 0 && y0 < H)
            cg_fb[y0 * W + x0] = v;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void frame_rect(uint16_t c, int x, int y, int w, int h)
{
    fr(c, x, y, w, 1);
    fr(c, x, y + h - 1, w, 1);
    fr(c, x, y, 1, h);
    fr(c, x + w - 1, y, 1, h);
}

/* sine in thousandths for a 64-step circle (integer only) */
static int sin64(int phase)
{
    static const short q[17] = {0, 98, 195, 290, 383, 471, 556, 634, 707,
                                773, 831, 882, 924, 957, 981, 995, 1000};
    int p = phase & 63, quad = p >> 4, i = p & 15;
    switch (quad)
    {
    case 0:  return q[i];
    case 1:  return q[16 - i];
    case 2:  return -q[i];
    default: return -q[16 - i];
    }
}

/* ---- the water scene (port of the ClickGotchi plugin's draw_water and
 * draw_bubbles; same colours, same animation phases from "now" in ms) ---- */

static const uint16_t water_band[8] = {0x010B, 0x012D, 0x014F, 0x0170, 0x0190, 0x01B0, 0x01D0, 0x01EF};
#define C_SHAFT 0x2B75
#define C_REEF 0x0147
#define C_SAND_DEEP 0x6328
#define C_SAND_MID 0x9C4B
#define C_SAND_LIGHT 0xDDAF
#define C_WEED_A 0x05A8
#define C_WEED_B 0x03C8
#define C_PARTICLE 0x6DBB
#define C_BUBBLE 0x8410
#define HEADER_ROWS 17
#define FOOTER_ROWS 15

static void draw_water(int w, int h, uint32_t now)
{
    int i, top = 17, bottom = h - 17, span = bottom - top;
    fr(water_band[0], 0, 0, w, HEADER_ROWS);
    for (i = 0; i < 8; i++)
    {
        int y0 = top + (span * i) / 8, y1 = top + (span * (i + 1)) / 8;
        fr(water_band[i], 0, y0, w, y1 - y0);
    }
    for (i = 0; i < 5 || 18 + i * 48 - 13 < w; i++)             /* surface shafts of light */
    {
        int x = 18 + i * 48;
        ln(C_SHAFT, x, 18, x - 13, 39);
        ln(C_SHAFT, x + 3, 18, x - 10, 39);
    }
    for (i = 0; i < 7 || i * 43 - 12 < w; i++)                  /* far-off reef silhouettes */
    {
        int x = i * 43 - 12, y = h - 31 + (i % 3) * 3;
        fr(C_REEF, x, y, 30, h - 18 - y);
        fr(C_REEF, x + 8, y - 5, 12, 6);
    }
    fr(C_SAND_DEEP, 0, h - 23, w, 6);
    fr(C_SAND_MID, 0, h - 18, w, 3);
    fr(C_SAND_LIGHT, 0, h - 15, w, 1);
    fr(C_SAND_MID, 0, h - 14, w, FOOTER_ROWS - 1);
    for (i = 0; i < 7 || 8 + i * 37 < w; i++)                   /* seaweed, swaying */
    {
        int x = 8 + i * 37, base = h - 24, tall = 13 + (i % 3) * 5;
        int sway = (int)((now / 480UL + (uint32_t)(i * 3)) % 5U) - 2;
        uint16_t c = (i % 2) ? C_WEED_B : C_WEED_A;
        ln(c, x, base, x + sway, base - tall);
        ln(c, x + 2, base, x + 5 - sway, base - tall + 5);
    }
    for (i = 0; i < 8 || 9 + i * 29 < w; i++)                   /* rising particles */
    {
        int x = 9 + i * 29;
        int y = 20 + (int)(((uint32_t)(i * 17) + now / 190UL) % (uint32_t)(span > 1 ? span : 1));
        fr(C_PARTICLE, x, y, 1, 1);
    }
}

/* the three bubbles with a small sideways wobble */
static void draw_bubbles(int w, int h, uint32_t now)
{
    int top = 35, bottom = h - 19;
    unsigned span = (unsigned)((bottom - top + 1) > 1 ? (bottom - top + 1) : 1);
    int y0 = bottom - (int)((now / 180UL) % span);
    int y1 = bottom - (int)((now / 260UL + 13) % span);
    int y2 = bottom - (int)((now / 340UL + 25) % span);
    int x0 = 15 + (sin64((int)((now / 110U) & 63U)) * 2) / 1000;
    int x1 = 29 + (sin64((int)((now / 140U) & 63U)) * 2) / 1000;
    int x2 = w - 18 + (sin64((int)((now / 125U) & 63U)) * 2) / 1000;
    frame_rect(C_BUBBLE, x0, y0, 2, 2);
    frame_rect(C_BUBBLE, x1, y1, 3, 3);
    frame_rect(C_BUBBLE, x2, y2, 2, 2);
}

/* ---- animation clock and wake-up ---- */

static long frame_interval(void)
{
    long iv = cg_scene_speed == 0 ? HZ / 30 : cg_scene_speed == 1 ? HZ / 10 : HZ;
    return iv < 1 ? 1 : iv;
}

/* Tick task (interrupt context): while a list has drawn recently and the
 * scene is still the active backdrop, ask the UI loop for a redraw once per
 * frame interval. BUTTON_REDRAW makes get_action() return ACTION_REDRAW,
 * which the list loops already turn into gui_synclist_draw(). */
static void cg_tick(void)
{
    long now = current_tick;
    if (!cg_in_use)
        return;
    if (TIME_AFTER(now, cg_last_use + frame_interval() + HZ / 4))
    {   /* no list has drawn for a while: stop poking the UI */
        cg_in_use = false;
        return;
    }
    if (TIME_BEFORE(now, cg_next_wake))
        return;
    cg_next_wake = now + frame_interval();
    if (lcd_get_backdrop() == cg_fb && button_queue_count() == 0)
        button_queue_post(BUTTON_REDRAW, 0);
}

bool cg_scene_attach(long tick)
{
    fb_data *bd = lcd_get_backdrop();
    if (bd != NULL && bd != cg_fb)
        return false;                       /* a theme backdrop is active */

    if (!cg_valid || (tick - cg_last_frame) >= frame_interval())
    {
        uint32_t now = (uint32_t)tick * (1000 / HZ);
        draw_water(W, H, now);
        draw_bubbles(W, H, now);
        cg_last_frame = tick;
        cg_valid = true;
    }
    if (bd != cg_fb)
        lcd_set_backdrop(cg_fb);

    cg_last_use = tick;
    cg_in_use = true;
    if (!cg_task_added)
    {
        cg_task_added = true;
        tick_add_task(cg_tick);
    }
    return true;
}

/* ---- text colour support ---- */

int cg_row_luma(int x, int y, int w, int h)
{
    enum { COLS = 8, ROWS = 3 };
    int c, r, sum = 0;
    if (w <= 0 || h <= 0)
        return 128;
    for (r = 0; r < ROWS; r++)
    {
        int py = y + (h * (2 * r + 1)) / (2 * ROWS);
        if (py < 0) py = 0;
        if (py >= H) py = H - 1;
        for (c = 0; c < COLS; c++)
        {
            int pxx = x + (w * (2 * c + 1)) / (2 * COLS);
            fb_data p;
            if (pxx < 0) pxx = 0;
            if (pxx >= W) pxx = W - 1;
            p = cg_fb[py * W + pxx];
            sum += (RGB_UNPACK_RED(p) * 77 + RGB_UNPACK_GREEN(p) * 150 +
                    RGB_UNPACK_BLUE(p) * 29) >> 8;
        }
    }
    return sum / (COLS * ROWS);
}

#endif /* CG_SCENE_ENABLED */
