#pragma once

/**
 * lvgl_v9compat.h - Engine-only LVGL v9 API aliases.
 *
 * Mx5UI.cpp was written against an LVGL v9.x where the chart accessor names
 * still used the pre-cleanup form (lv_chart_set_axis_range / _all_values /
 * _series_value_by_id). LVGL 9.2.0 (the pinned version) renamed these to
 * lv_chart_set_range / _all_value / _value_by_id with identical signatures.
 *
 * This header is force-included ONLY when compiling the untouched engine
 * (Mx5UI.cpp + fonts) and maps the legacy names onto the modern names so the
 * engine source stays unmodified and compiles against LVGL 9.2.0.
 *
 * Do not include this from jni_mx5.cpp / RfcommObd.cpp - they use modern names.
 */

#define lv_chart_set_axis_range(a, b, c, d)       lv_chart_set_range(a, b, c, d)
#define lv_chart_set_all_values(a, b, c)          lv_chart_set_all_value(a, b, c)
#define lv_chart_set_series_value_by_id(a, b, c, d) lv_chart_set_value_by_id(a, b, c, d)
