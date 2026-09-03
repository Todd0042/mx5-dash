/**
 * @file lv_conf.h
 * Configuration file for v9 of the LVGL library.
 *
 * Minimal configuration tuned for the mx5-dash project on an ESP32-S3
 * with 8MB PSRAM. Enable only what the UI uses to keep the binary lean.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

// Swipe navigation between the dash screens (Mx5UI::onGesture) requires
// LVGL's gesture recognizer - disabled by default in v9 (see lv_conf_internal.h)
// and needs LV_USE_FLOAT (lv_indev_gesture.h).
#define LV_USE_FLOAT 1
#define LV_USE_GESTURE_RECOGNITION 1

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 1

#if 1
#define LV_USE_LOG 0
#else
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#endif

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

/*========= Monitor =========*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/*========= Drawing =========*/
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SHADOW 1
#define LV_DRAW_SW_GRADIENT 1

/*========= Widgets =========*/
#define LV_USE_WIDGETS 1

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 1
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 1
#define LV_USE_KEYBOARD 0
#define LV_USE_LABEL 1
#define LV_USE_LINE 1
#define LV_USE_LIST 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/*========= Themes =========*/
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_BASIC 1
#define LV_USE_THEME_MONO 0
#define LV_USE_THEME_SIMPLE 1

/*========= Layouts =========*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*========= Fonts =========*/
#define LV_FONT_MONTSERRAT_8 1
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*========= Others =========*/
#define LV_USE_ANIMIMG 0
#define LV_USE_QRCODE 0
#define LV_USE_SNAPSHOT 0

/*========= Features =========*/
#define LV_USE_OBSERVER 1
#define LV_USE_SNAPSHOT 0

/*========= 3rd party libraries =========*/
#define LV_USE_FS_STDIO 0

/*========= Demos =========*/
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS 0

#endif /*LV_CONF_H*/
