+#ifndef LV_CONF_H
+#define LV_CONF_H
+
+/* LVGL 9.3.0 - Waveshare ESP32-S3 Touch LCD 5B.
+ * Minimal explicit configuration; omitted options use LVGL defaults.
+ * This is not yet a minimum-flash or hardware-validated configuration.
+ */
+
+/* Display output: the display driver must also select RGB565. */
+#define LV_COLOR_DEPTH 16
+
+/* Retain a fixed 48 KiB LVGL pool for initial testing.
+ * This does NOT include display buffers and does NOT force PSRAM use.
+ */
+#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
+#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
+#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
+#define LV_MEM_SIZE (48U * 1024U)
+#define LV_MEM_POOL_EXPAND_SIZE 0
+#define LV_MEM_ADR 0
+
+/* One application task must own LVGL.
+ * The forthcoming driver must provide the millisecond tick source.
+ */
+#define LV_USE_OS LV_OS_NONE
+#define LV_DEF_REFR_PERIOD 30
+#define LV_DPI_DEF 130
+#define LV_USE_DRAW_SW 1
+#define LV_DRAW_SW_DRAW_UNIT_CNT 1
+#define LV_DRAW_SW_COMPLEX 1
+
+/* Fonts used by the reference UI. */
/*Montserrat fonts with ASCII range and some symbols using bpp = 4
 *https://fonts.google.com/specimen/Montserrat*/
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
+
+#define LV_USE_LABEL 1
+#define LV_USE_CHART 1
+#define LV_USE_ASSERT_NULL 1
+#define LV_USE_ASSERT_MALLOC 1
+#define LV_USE_LOG 0
+#define LV_BUILD_EXAMPLES 0
+
+#endif
