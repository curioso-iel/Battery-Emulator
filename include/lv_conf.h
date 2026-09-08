+#ifndef LV_CONF_H
+#define LV_CONF_H
+
+/* LVGL 8.4.0: configuration for the supplied Waveshare LCD-5B UI.
+ * Unspecified settings use LVGL defaults. Not hardware-tested.
+ */
+#include <stdint.h>
+
+/* RGB565; retain the existing allocation strategy for this checkpoint. */
+#define LV_COLOR_DEPTH 16
+#define LV_COLOR_16_SWAP 0
+#define LV_MEM_CUSTOM 0
+#define LV_MEM_SIZE (48U * 1024U)
+#define LV_TICK_CUSTOM 0
+#define LV_DISP_DEF_REFR_PERIOD 30
+#define LV_INDEV_DEF_READ_PERIOD 30
+#define LV_DPI_DEF 130
+#define LV_DRAW_COMPLEX 1
+
+/* Fonts actually selected by the supplied display code. */
+#define LV_FONT_MONTSERRAT_14 1
+#define LV_FONT_MONTSERRAT_20 1
+#define LV_FONT_MONTSERRAT_24 1
+#define LV_FONT_MONTSERRAT_36 1
+#define LV_FONT_MONTSERRAT_40 1
+#define LV_FONT_DEFAULT &lv_font_montserrat_14
+
+/* The UI builds controls from base objects, labels and a cell chart. */
+#define LV_USE_LABEL 1
+#define LV_USE_CHART 1
+
+/* Explicitly disable unused widgets: omission can enable LVGL defaults. */
+#define LV_USE_ARC 0
+#define LV_USE_BAR 0
+#define LV_USE_BTN 0
+#define LV_USE_BTNMATRIX 0
+#define LV_USE_CANVAS 0
+#define LV_USE_CHECKBOX 0
+#define LV_USE_DROPDOWN 0
+#define LV_USE_IMG 0
+#define LV_USE_LINE 0
+#define LV_USE_ROLLER 0
+#define LV_USE_SLIDER 0
+#define LV_USE_SWITCH 0
+#define LV_USE_TEXTAREA 0
+#define LV_USE_TABLE 0
+#define LV_USE_ANIMIMG 0
+#define LV_USE_CALENDAR 0
+#define LV_USE_COLORWHEEL 0
+#define LV_USE_IMGBTN 0
+#define LV_USE_KEYBOARD 0
+#define LV_USE_LED 0
+#define LV_USE_LIST 0
+#define LV_USE_MENU 0
+#define LV_USE_METER 0
+#define LV_USE_MSGBOX 0
+#define LV_USE_SPAN 0
+#define LV_USE_SPINBOX 0
+#define LV_USE_SPINNER 0
+#define LV_USE_TABVIEW 0
+#define LV_USE_TILEVIEW 0
+#define LV_USE_WIN 0
+
+/* Retain the original default theme; no automatic layouts are used. */
+#define LV_USE_THEME_DEFAULT 1
+#define LV_USE_THEME_BASIC 0
+#define LV_USE_THEME_MONO 0
+#define LV_USE_FLEX 0
+#define LV_USE_GRID 0
+
+/* Keep fast allocation/null checks; omit examples and demos. */
+#define LV_USE_ASSERT_NULL 1
+#define LV_USE_ASSERT_MALLOC 1
+#define LV_BUILD_EXAMPLES 0
+#define LV_USE_DEMO_WIDGETS 0
+#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
+#define LV_USE_DEMO_BENCHMARK 0
+#define LV_USE_DEMO_STRESS 0
+#define LV_USE_DEMO_MUSIC 0
+
+#endif
