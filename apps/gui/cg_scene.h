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
#ifndef CG_SCENE_H
#define CG_SCENE_H

#include "config.h"
#include "lcd.h"

/* Only for colour 16 bpp targets whose framebuffer is the plain
 * LCD_WIDTH x LCD_HEIGHT array (the backdrop has the same layout). */
#if defined(HAVE_LCD_COLOR) && defined(HAVE_BACKDROP_IMAGE) && \
    (LCD_DEPTH == 16) && (LCD_FBWIDTH == LCD_WIDTH) && (LCD_FBHEIGHT == LCD_HEIGHT)
#define CG_SCENE_ENABLED

/* Menu animation speed, mirroring the ClickGotchi plugin's Speed setting:
 * 0 Smooth (~30 fps), 1 Simulation (10 fps), 2 Efficient (1 fps).
 * The plugin's own setting is not reachable from core, so this is a
 * separate int in the scene module. */
extern int cg_scene_speed;

/* Called by the list code before it clears its viewport. If no theme
 * backdrop is active, redraws the scene when a frame is due (rate set by
 * cg_scene_speed), points the LCD backdrop at the scene buffer and returns
 * true. Returns false when a theme backdrop is in use. */
bool cg_scene_attach(long tick);

/* Average luma (0..255) of the scene behind the given screen rectangle,
 * from a coarse sample grid. */
int cg_row_luma(int x, int y, int w, int h);

#endif /* CG_SCENE_ENABLED conditions */
#endif /* CG_SCENE_H */
