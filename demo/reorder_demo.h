#pragma once

#include "fmsui/fmsui.h"

/* A keyed list you can reorder, with the keys switchable off so the difference
 * is visible on screen.
 *
 * `rows` is how many legs the plan has.  It is a parameter because the cost of
 * a reorder is a question about scale: every lv_obj that changes position in the
 * flat LVGL tree has to be told, and whether that stays linear is not something
 * to assume.  ~40 rows puts the screen at the same lv_obj count as the real
 * two-up FMS page. */
fmsui::Widget *reorder_demo_build(int rows);
