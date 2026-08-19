#pragma once

/* M0 probe screen -- the same LVGL code runs on the Tab5 and in the simulator.
 *
 * Its job is to answer the M0 questions: does the panel come up at 1280x720 in
 * landscape, does touch land where we think it does, and what frame rate do we
 * get out of the box.  It is deliberately plain LVGL; the framework starts in M1.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* `platform` is shown on screen, e.g. "SIM (SDL2)" or "M5 Tab5 / ESP32-P4". */
void m0_probe_build(const char *platform);

#ifdef __cplusplus
}
#endif
