#pragma once

#include "fmsui/fmsui.h"

/* Builds the M1 demo tree.  Passed to fmsui::runApp(), so it is called on every
 * rebuild and must construct a fresh tree each time. */
fmsui::Widget *m1_demo_build();
