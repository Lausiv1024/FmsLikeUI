#pragma once

/* Umbrella header. Include this and you have the framework's public API.
 *
 * The arena and the element tree are not here, and cannot be included from
 * outside the library: their headers are in src/internal/, which only fmsui
 * itself is built with.  Which of these headers are for every application and
 * which are for diagnostics and extension is in docs/USING.md. */

#include "fmsui/app.h"
#include "fmsui/fms.h"
#include "fmsui/foundation.h"
#include "fmsui/refresh.h"
#include "fmsui/render.h"
#include "fmsui/str.h"
#include "fmsui/theme.h"
#include "fmsui/widget.h"
#include "fmsui/widgets.h"
