#pragma once

#include "fmsui/fmsui.h"

/* ACTIVE/F-PLN: a flight plan too long for the panel, stepped rather than
 * scrolled.
 *
 * The real aircraft has no scrollbar and no flick gesture -- a fixed number of
 * lines, and a pair of arrows that walk the window along the plan one waypoint
 * at a time.  This screen is that, and it is also where the claim behind it gets
 * measured: stepping a window costs no lv_obj churn at all, and `lv_moved` in
 * the readout is what says so.
 *
 * The two toggles are the demonstration.  KEYS puts a key on each row, which is
 * the obvious thing to do and the wrong one here -- watch lv_moved leave zero.
 * SCOPE moves the arrows between the list's own row and the scaffold footer,
 * which is the placement rule: whatever region the arrows page, they have to sit
 * outside it.
 */
fmsui::Widget *fplan_demo_build();
