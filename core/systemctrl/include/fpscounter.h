#ifndef _FPSCOUNTER_H_
#define _FPSCOUNTER_H_

/*
 * FPS counter overlay (debug setting "FPS Counter").
 *
 * Draws the current frame rate on the screen, in the XMB and in games.
 * The corner/edge it is drawn at is set by se_config.fps_counter_pos
 * (debug setting "FPS Counter Position", see enum FpsCounterPosition; top left
 * by default) and is read every frame, so changing it takes effect immediately.
 * Call fpsCounterApply() whenever se_config.fps_counter
 * may have changed: it installs the display hook the first time the setting
 * is found enabled. Turning the setting off later hides the overlay immediately.
 */
void fpsCounterApply(void);

#endif
