// UI-03: uniform cards, Wi-Fi/SSID header and battery power state.
// Based on the supplied board routing/timings; LVGL integration targets 9.3.0.
// Source reviewed, not compiled or hardware-tested.
#if defined(HW_WAVESHARE_LCD_5B) && !defined(SMALL_FLASH_DEVICE)
#include "display.h"
#include "../hal/hal.h"
#include "../safety/safety.h"
#include "../../datalayer/datalayer.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lvgl.h>
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 3 || LVGL_VERSION_PATCH != 0
#error This driver targets LVGL 9.3.0
#endif

namespace {
constexpr int LCD_WIDTH = 1024;
constexpr int LCD_HEIGHT = 600;
constexpr uint32_t LCD_PCLK_HZ = 14000000;
constexpr int LCD_HSYNC_FRONT = 40, LCD_HSYNC_PULSE = 20, LCD_HSYNC_BACK = 40;
constexpr int LCD_VSYNC_FRONT = 40, LCD_VSYNC_PULSE = 20, LCD_VSYNC_BACK = 40;
class WaveshareLCD5BLGFX : public lgfx::LGFX_Device {

 public:
  lgfx::Bus_RGB bus;
  lgfx::Panel_RGB panel;

  WaveshareLCD5BLGFX() {

    {
      auto cfg = panel.config();

      cfg.memory_width  = LCD_WIDTH;
      cfg.memory_height = LCD_HEIGHT;
      cfg.panel_width   = LCD_WIDTH;
      cfg.panel_height  = LCD_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;

      panel.config(cfg);
    }

    {
      auto cfg = panel.config_detail();

      // Same strategy as the reference project:
      // framebuffer managed by LovyanGFX in PSRAM.
      cfg.use_psram = 1;

      panel.config_detail(cfg);
    }

    {
      auto cfg = bus.config();

      cfg.panel = &panel;

      // RGB565 data pins: exact Waveshare 28151 routing.
      cfg.pin_d0  = 14;  // B0
      cfg.pin_d1  = 38;  // B1
      cfg.pin_d2  = 18;  // B2
      cfg.pin_d3  = 17;  // B3
      cfg.pin_d4  = 10;  // B4

      cfg.pin_d5  = 39;  // G0
      cfg.pin_d6  = 0;   // G1
      cfg.pin_d7  = 45;  // G2
      cfg.pin_d8  = 48;  // G3
      cfg.pin_d9  = 47;  // G4
      cfg.pin_d10 = 21;  // G5

      cfg.pin_d11 = 1;   // R0
      cfg.pin_d12 = 2;   // R1
      cfg.pin_d13 = 42;  // R2
      cfg.pin_d14 = 41;  // R3
      cfg.pin_d15 = 40;  // R4

      cfg.pin_henable = 5;
      cfg.pin_vsync   = 3;
      cfg.pin_hsync   = 46;
      cfg.pin_pclk    = 7;

      // Exact known-good timing from VaAndCob's 1024x600 example.
      cfg.freq_write = LCD_PCLK_HZ;

      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = LCD_HSYNC_FRONT;
      cfg.hsync_pulse_width = LCD_HSYNC_PULSE;
      cfg.hsync_back_porch  = LCD_HSYNC_BACK;

      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = LCD_VSYNC_FRONT;
      cfg.vsync_pulse_width = LCD_VSYNC_PULSE;
      cfg.vsync_back_porch  = LCD_VSYNC_BACK;

      cfg.pclk_active_neg = 0;
      cfg.de_idle_high    = 0;
      cfg.pclk_idle_high  = 0;

      bus.config(cfg);
    }

    panel.setBus(&bus);
    setPanel(&panel);
  }
};

static WaveshareLCD5BLGFX lcd;



i2c_master_bus_handle_t bus_handle = nullptr;
i2c_master_dev_handle_t expander_mode = nullptr;
i2c_master_dev_handle_t expander_io = nullptr;
lv_display_t* display = nullptr;
void* draw_buffer = nullptr;
bool ready = false;
bool attempted = false;

bool checked(esp_err_t error, const char* stage) {
  if (error == ESP_OK) return true;
  Serial.printf("LCD5B: %s failed: %s\n", stage, esp_err_to_name(error));
  return false;
}

bool add_device(uint8_t address, i2c_master_dev_handle_t* device) {
  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = address;
  cfg.scl_speed_hz = 400000;
  return checked(i2c_master_bus_add_device(bus_handle, &cfg, device), "I2C device");
}

bool write_byte(i2c_master_dev_handle_t device, uint8_t value) {
  return checked(i2c_master_transmit(device, &value, 1, 100), "CH422G write");
}

uint32_t tick_ms() { return millis(); }

void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* pixels) {
  // LVGL v9 supplies a byte map, not an array of v8 lv_color_t.
  lcd.pushImage(area->x1, area->y1,
                area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
                reinterpret_cast<lgfx::rgb565_t*>(pixels));
  // Synchronous copy: buffer may only be reused after pushImage returns.
  lv_display_flush_ready(disp);
}
// UI-01: base dashboard + cell monitor. No writes to battery settings.
// Colours follow a test-instrument panel: navy, white and amber controls.
constexpr uint32_t UI_BG = 0x101E29, UI_PANEL = 0x1B3040;
constexpr uint32_t UI_TEXT = 0xEDF3F5, UI_MUTED = 0xAAC0CC;
constexpr uint32_t UI_ACCENT = 0xFFB34D;
lv_obj_t *page_main, *page_cells, *navigation, *status_label, *footer_label;
lv_obj_t *soc_label, *soh_label, *temp_label, *voltage_label, *current_label, *power_label;
lv_obj_t *charge_label, *discharge_label, *charge_effective, *discharge_effective;
lv_obj_t *pump_track, *pump_knob, *pump_label;
lv_obj_t *chart, *cell_stats, *cell_scale, *cell_count_label;
lv_obj_t* wifi_label;
lv_obj_t* wifi_icon;
lv_obj_t* wifi_bars[4];
lv_chart_series_t* cell_series;
constexpr unsigned PACK_CELLS = 98, CELLS_PER_PAGE = 14;
static_assert(MAX_AMOUNT_CELLS >= PACK_CELLS, "Cell array too small");
int32_t plotted_cells[CELLS_PER_PAGE];
lv_obj_t* cell_numbers[CELLS_PER_PAGE];
lv_obj_t* cell_page_label;
unsigned cell_page = 0;
bool refresh_requested = false;
void refresh_data();
bool cells_visible = false;
bool pump_local_request = false;
bool touch_ready = false;
i2c_master_dev_handle_t touch_device = nullptr;
uint16_t touch_width = LCD_WIDTH, touch_height = LCD_HEIGHT;

void text(lv_obj_t* obj, const char* value) {
  if (strcmp(lv_label_get_text(obj), value) != 0) lv_label_set_text(obj, value);
}

lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t colour) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(obj, lv_color_hex(colour), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, 12, 0);
  return obj;
}

lv_obj_t* label(lv_obj_t* parent, int x, int y, int w, const char* value,
                const lv_font_t* font, uint32_t colour = UI_TEXT) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_width(obj, w);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(colour), 0);
  lv_label_set_text(obj, value);
  return obj;
}

lv_obj_t* metric(int x, int y, int w, int h, const char* title) {
  lv_obj_t* card = panel(page_main, x, y, w, h, UI_PANEL);
  label(card, 18, 12, w - 36, title, &lv_font_montserrat_20, UI_MUTED);
  return label(card, 18, 49, w - 36, "--", &lv_font_montserrat_40);
}

void build_ui() {
  lv_obj_t* root = lv_display_get_screen_active(display);
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(root, lv_color_hex(UI_BG), 0);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
  wifi_icon = label(root, 24, 20, 32, LV_SYMBOL_WIFI, &lv_font_montserrat_24, UI_MUTED);
  for (unsigned i = 0; i < 4; ++i) {
    int h = 6 + int(i) * 6;
    wifi_bars[i] = panel(root, 60 + int(i) * 9, 46 - h, 6, h, 0x3A4B58);
    lv_obj_set_style_radius(wifi_bars[i], 1, 0);
  }
  wifi_label = label(root, 106, 22, 286, "| SEM REDE", &lv_font_montserrat_20, UI_MUTED);
  lv_label_set_long_mode(wifi_label, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_size(wifi_label, 286, 26);
  status_label = label(root, 420, 24, 355, "CAN: A AGUARDAR", &lv_font_montserrat_20, UI_ACCENT);
  lv_obj_t* nav = panel(root, 800, 12, 200, 52, UI_ACCENT);
  navigation = label(nav, 10, 14, 180, "Cell Monitor >", &lv_font_montserrat_20, UI_BG);
  lv_obj_set_style_text_align(navigation, LV_TEXT_ALIGN_CENTER, 0);

  page_main = panel(root, 0, 76, 1024, 474, UI_BG);
  page_cells = panel(root, 0, 76, 1024, 474, UI_BG);
  lv_obj_add_flag(page_cells, LV_OBJ_FLAG_HIDDEN);
  soc_label = metric(24, 8, 314, 142, "SOC / CARGA");
  lv_obj_set_style_text_color(soc_label, lv_color_hex(UI_ACCENT), 0);
  soh_label = metric(24, 166, 314, 142, "SOH / SAUDE");
  temp_label = metric(354, 8, 314, 142, "TEMPERATURA MAX.");
  voltage_label = metric(684, 8, 314, 142, "TENSAO");
  current_label = metric(354, 166, 314, 142, "CORRENTE");
  power_label = metric(684, 166, 314, 142, "POTENCIA");

  lv_obj_t* c = panel(page_main, 24, 324, 314, 142, UI_PANEL);
  label(c, 16, 10, 278, "LIMITE CARGA / UTILIZADOR", &lv_font_montserrat_14, UI_MUTED);
  charge_label = label(c, 16, 36, 200, "-- A", &lv_font_montserrat_36);
  lv_obj_t* up = panel(c, 230, 32, 64, 48, UI_ACCENT);
  lv_obj_t* arrow = label(up, 0, 10, 64, LV_SYMBOL_UP, &lv_font_montserrat_24, UI_BG);
  lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* dn = panel(c, 230, 86, 64, 48, UI_ACCENT);
  arrow = label(dn, 0, 10, 64, LV_SYMBOL_DOWN, &lv_font_montserrat_24, UI_BG);
  lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
  charge_effective = label(c, 16, 96, 210, "Disponivel: -- A", &lv_font_montserrat_20, UI_MUTED);
  c = panel(page_main, 354, 324, 314, 142, UI_PANEL);
  label(c, 16, 10, 278, "LIMITE DESCARGA / UTILIZADOR", &lv_font_montserrat_14, UI_MUTED);
  discharge_label = label(c, 16, 36, 200, "-- A", &lv_font_montserrat_36);
  up = panel(c, 230, 32, 64, 48, UI_ACCENT);
  arrow = label(up, 0, 10, 64, LV_SYMBOL_UP, &lv_font_montserrat_24, UI_BG);
  lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
  dn = panel(c, 230, 86, 64, 48, UI_ACCENT);
  arrow = label(dn, 0, 10, 64, LV_SYMBOL_DOWN, &lv_font_montserrat_24, UI_BG);
  lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);
  discharge_effective = label(c, 16, 96, 210, "Disponivel: -- A", &lv_font_montserrat_20, UI_MUTED);
  c = panel(page_main, 684, 324, 314, 142, UI_PANEL);
  label(c, 16, 10, 290, "BOMBA / PEDIDO LOCAL", &lv_font_montserrat_20, UI_MUTED);
  pump_track = panel(c, 16, 47, 112, 48, 0x3A4B58);
  lv_obj_set_style_radius(pump_track, 24, 0);
  pump_knob = panel(pump_track, 5, 5, 38, 38, UI_TEXT);
  lv_obj_set_style_radius(pump_knob, 19, 0);
  pump_label = label(c, 144, 55, 154, "OFF", &lv_font_montserrat_24);
  label(c, 16, 110, 282, "RS485 por implementar", &lv_font_montserrat_20, UI_ACCENT);

  cell_count_label = label(page_cells, 24, 6, 976, "CELULAS / SEM DADOS", &lv_font_montserrat_24);
  cell_stats = label(page_cells, 24, 50, 976, "MIN --     MAX --     DELTA --", &lv_font_montserrat_20, UI_MUTED);
  cell_scale = label(page_cells, 24, 86, 976, "Escala automatica / mV", &lv_font_montserrat_14, UI_MUTED);
  chart = lv_chart_create(page_cells);
  lv_obj_set_pos(chart, 24, 120);
  lv_obj_set_size(chart, 976, 250);
  lv_obj_set_style_bg_color(chart, lv_color_hex(UI_PANEL), 0);
  lv_obj_set_style_border_width(chart, 0, 0);
  lv_obj_set_style_pad_all(chart, 8, 0);
  lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(chart, CELLS_PER_PAGE);
  lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 2500, 4300);
  lv_chart_set_div_line_count(chart, 5, 0);
  for (auto& v : plotted_cells) v = LV_CHART_POINT_NONE;
  cell_series = lv_chart_add_series(chart, lv_color_hex(UI_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_series_ext_y_array(chart, cell_series, plotted_cells);
  for (unsigned i = 0; i < CELLS_PER_PAGE; ++i) {
    int x = 32 + int((2 * i + 1) * 960 / (2 * CELLS_PER_PAGE)) - 24;
    cell_numbers[i] = label(page_cells, x, 376, 48, "", &lv_font_montserrat_20, UI_MUTED);
    lv_obj_set_style_text_align(cell_numbers[i], LV_TEXT_ALIGN_CENTER, 0);
  }
  lv_obj_t* prev = panel(page_cells, 24, 418, 164, 48, UI_ACCENT);
  label(prev, 14, 12, 140, "< Anteriores", &lv_font_montserrat_20, UI_BG);
  lv_obj_t* next = panel(page_cells, 836, 418, 164, 48, UI_ACCENT);
  label(next, 14, 12, 140, "Seguintes >", &lv_font_montserrat_20, UI_BG);
  cell_page_label = label(page_cells, 212, 430, 600, "", &lv_font_montserrat_20, UI_MUTED);
  lv_obj_set_style_text_align(cell_page_label, LV_TEXT_ALIGN_CENTER, 0);
  footer_label = label(root, 24, 566, 976, "ESTADO: SEM DADOS", &lv_font_montserrat_20, UI_MUTED);
  lv_obj_set_style_text_align(footer_label, LV_TEXT_ALIGN_CENTER, 0);
}

void toggle_pump_local() {
  pump_local_request = !pump_local_request;
  lv_obj_set_x(pump_knob, pump_local_request ? 69 : 5);
  lv_obj_set_style_bg_color(pump_track, lv_color_hex(pump_local_request ? UI_ACCENT : 0x3A4B58), 0);
  text(pump_label, pump_local_request ? "ON (local)" : "OFF");
  // Deliberately no RS485, CAN, NVM or datalayer write.
  Serial.printf("LCD5B: pump LOCAL request=%u; no command transmitted\n", pump_local_request);
}

void change_page() {
  cells_visible = !cells_visible;
  if (cells_visible) {
    lv_obj_add_flag(page_main, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page_cells, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(page_cells, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page_main, LV_OBJ_FLAG_HIDDEN);
  }
  text(navigation, cells_visible ? "< Principal" : "Cell Monitor >");
  refresh_requested = true;
}

void adjust_limit(bool charge, bool increase) {
  auto& settings = datalayer.battery.settings;
  uint16_t& target = charge ? settings.max_user_set_charge_dA : settings.max_user_set_discharge_dA;
  int old = target;
  // Values from other interfaces may be off-grid: preserve exact +/-5 A,
  // clamp the result, never wrap uint16_t. Do not alter anything on boot.
  int next = old + (increase ? 50 : -50);
  if (next < 0) next = 0;
  if (next > 400) next = 400;
  if (next == old) return;
  target = static_cast<uint16_t>(next);
  refresh_requested = true;
  Serial.printf("LCD5B: user %s limit=%d dA; no NVM save\n", charge ? "charge" : "discharge", next);
}

int hit_target(int x, int y) {
  if (x >= 800 && x < 1000 && y >= 12 && y < 64) return 1;
  if (cells_visible) {
    if (y >= 494 && y < 542) {
      if (x >= 24 && x < 188) return 7;
      if (x >= 836 && x < 1000) return 8;
    }
    return 0;
  }
  if (x >= 700 && x < 982 && y >= 447 && y < 498) return 2;
  bool charge = x >= 254 && x < 318;
  bool discharge = x >= 584 && x < 648;
  if (charge || discharge) {
    if (y >= 432 && y < 480) return charge ? 3 : 5;
    if (y >= 486 && y < 534) return charge ? 4 : 6;
  }
  return 0;
}

bool touch_read(uint16_t reg, uint8_t* bytes, size_t len) {
  uint8_t a[2] = {uint8_t(reg >> 8), uint8_t(reg)};
  return i2c_master_transmit_receive(touch_device, a, 2, bytes, len, 20) == ESP_OK;
}
bool touch_ack() {
  uint8_t a[3] = {0x81, 0x4E, 0};
  return i2c_master_transmit(touch_device, a, 3, 20) == ESP_OK;
}

bool init_touch() {
  if (!esp32hal->alloc_pins("GT911", GPIO_NUM_4)) return false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    const uint8_t address = attempt ? 0x14 : 0x5D;
    pinMode(4, OUTPUT);
    digitalWrite(4, attempt ? HIGH : LOW);
    if (!write_byte(expander_io, 0xFD)) return false;
    delay(100);
    if (!write_byte(expander_io, 0xFF)) return false;
    delay(10);
    pinMode(4, INPUT);
    delay(50);
    esp_task_wdt_reset();
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = address;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus_handle, &cfg, &touch_device) != ESP_OK) continue;
    uint8_t id[4] = {}, range[4] = {};
    if (touch_read(0x8140, id, 4) && id[0] == '9' && id[1] == '1' && id[2] == '1' &&
        touch_read(0x8048, range, 4)) {
      touch_width = uint16_t(range[0]) | (uint16_t(range[1]) << 8);
      touch_height = uint16_t(range[2]) | (uint16_t(range[3]) << 8);
      if (touch_width && touch_height && touch_ack()) {
        Serial.printf("LCD5B: GT911 at 0x%02X; range=%ux%u\n", address, touch_width, touch_height);
        return true;
      }
    }
    i2c_master_bus_rm_device(touch_device);
    touch_device = nullptr;
  }
  Serial.println("LCD5B: GT911 unavailable; display remains read-only");
  return false;
}

// Manual tap handling keeps LVGL in one task, without automatic pressed-style
// redraws. No long-press or repeated commands. Errors cancel the gesture.
void poll_touch(uint32_t now) {
  static uint32_t last_poll = 0, start_ms = 0;
  static bool down = false, valid = false;
  static int target = 0, sx = 0, sy = 0;
  if (!touch_ready || uint32_t(now - last_poll) < 30) return;
  last_poll = now;
  uint8_t status = 0;
  if (!touch_read(0x814E, &status, 1)) { down = valid = false; return; }
  if (down && uint32_t(now - start_ms) > 2000) valid = false;
  if (!(status & 0x80)) return;
  uint8_t count = status & 15;
  if (!count) {
    bool released = touch_ack();
    if (released && down && valid) {
      if (target == 1) change_page();
      else if (target == 2) toggle_pump_local();
      else if (target >= 3 && target <= 6) adjust_limit(target <= 4, target == 3 || target == 5);
      else if (target == 7 && cell_page > 0) { --cell_page; refresh_requested = true; }
      else if (target == 8 && cell_page < 6) { ++cell_page; refresh_requested = true; }
    }
    down = valid = false;
    return;
  }
  uint8_t point[8] = {};
  bool ok = count == 1 && touch_read(0x814F, point, sizeof(point));
  ok = touch_ack() && ok;
  if (!ok) { down = valid = false; return; }
  uint16_t rx = uint16_t(point[1]) | (uint16_t(point[2]) << 8);
  uint16_t ry = uint16_t(point[3]) | (uint16_t(point[4]) << 8);
  if (rx >= touch_width || ry >= touch_height) { down = valid = false; return; }
  int x = uint32_t(rx) * LCD_WIDTH / touch_width;
  int y = uint32_t(ry) * LCD_HEIGHT / touch_height;
  if (!down) {
    down = valid = true;
    start_ms = now; sx = x; sy = y;
    target = hit_target(x, y);
    Serial.printf("LCD5B: touch x=%d y=%d target=%d\n", x, y, target);
  } else if (abs(x - sx) > 30 || abs(y - sy) > 30) valid = false;
}

void refresh_wifi() {
  // Only query existing Wi-Fi state: no scans, reconnects or credential writes.
  bool connected = WiFi.status() == WL_CONNECTED;
  int level = 0;
  String name;
  if (connected) {
    int rssi = WiFi.RSSI();
    level = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
    name = WiFi.SSID();
  } else {
    auto mode = WiFi.getMode();
    name = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) ? "AP / sem ligacao a rede" : "SEM REDE";
  }
  String caption = String("| ") + name;
  text(wifi_label, caption.c_str());
  static int previous = -1;
  if (level != previous) {
    for (unsigned i = 0; i < 4; ++i)
      lv_obj_set_style_bg_color(wifi_bars[i], lv_color_hex(int(i) < level ? UI_ACCENT : 0x3A4B58), 0);
    lv_obj_set_style_text_color(wifi_icon, lv_color_hex(connected ? UI_ACCENT : UI_MUTED), 0);
    previous = level;
  }
}

void refresh_data() {
  refresh_wifi();
  // Read-only best-effort snapshot, NOT an atomic/validated BMS measurement.
  const auto& b = datalayer.battery;
  bool live = battery_detected && b.status.CAN_battery_still_alive > 0;
  text(status_label, live ? "CAN: PRESENTE*" : "CAN: SEM DADOS");
  char s[200];
  if (!cells_visible) {
    if (live) {
      snprintf(s, sizeof(s), "%.1f %%", b.status.real_soc / 100.0); text(soc_label, s);
      if (b.status.soh_available) {
        snprintf(s, sizeof(s), "%.1f %%", b.status.soh_pptt / 100.0); text(soh_label, s);
      } else text(soh_label, "-- %");
      snprintf(s, sizeof(s), "%.1f C", b.status.temperature_max_dC / 10.0); text(temp_label, s);
      snprintf(s, sizeof(s), "%.1f V", b.status.voltage_dV / 10.0); text(voltage_label, s);
      snprintf(s, sizeof(s), "%+.1f A", b.status.current_dA / 10.0); text(current_label, s);
      snprintf(s, sizeof(s), "%+.2f kW", b.status.active_power_W / 1000.0); text(power_label, s);
      snprintf(s, sizeof(s), "Disponivel: %.1f A", b.status.max_charge_current_dA / 10.0); text(charge_effective, s);
      snprintf(s, sizeof(s), "Disponivel: %.1f A", b.status.max_discharge_current_dA / 10.0); text(discharge_effective, s);
    } else {
      text(soc_label, "-- %"); text(soh_label, "-- %"); text(temp_label, "-- C");
      text(voltage_label, "-- V"); text(current_label, "-- A"); text(power_label, "-- kW");
      text(charge_effective, "Disponivel: -- A"); text(discharge_effective, "Disponivel: -- A");
    }
    snprintf(s, sizeof(s), "%.1f A", b.settings.max_user_set_charge_dA / 10.0); text(charge_label, s);
    snprintf(s, sizeof(s), "%.1f A", b.settings.max_user_set_discharge_dA / 10.0); text(discharge_label, s);
  } else {
    unsigned n = b.info.number_of_cells;
    if (n > MAX_AMOUNT_CELLS) n = MAX_AMOUNT_CELLS;
    unsigned valid_count = 0, mini = 0, maxi = 0;
    int32_t lo = INT32_MAX, hi = 0;
    bool changed = refresh_requested;
    for (unsigned i = 0; i < CELLS_PER_PAGE; ++i) {
      unsigned index = cell_page * CELLS_PER_PAGE + i;
      snprintf(s, sizeof(s), "%u", index + 1); text(cell_numbers[i], s);
      uint16_t mv = (live && index < n) ? b.status.cell_voltages_mV[index] : 0;
      int32_t v = mv ? mv : LV_CHART_POINT_NONE;
      if (plotted_cells[i] != v) { plotted_cells[i] = v; changed = true; }
    }
    // Pack-wide statistics/scale (not only the 14 visible cells).
    for (unsigned i = 0; i < n; ++i) {
      uint16_t mv = live ? b.status.cell_voltages_mV[i] : 0;
      if (mv) {
        ++valid_count;
        if (mv < lo) { lo = mv; mini = i + 1; }
        if (mv > hi) { hi = mv; maxi = i + 1; }
      }
    }
    snprintf(s, sizeof(s), "CELL MONITOR / %u celulas / %u leituras", n, valid_count);
    text(cell_count_label, s);
    snprintf(s, sizeof(s), "C%u a C%u / 98 posicoes / %u de 7",
             cell_page * 14 + 1, cell_page * 14 + 14, cell_page + 1);
    text(cell_page_label, s);
    if (n && n != PACK_CELLS)
      text(cell_count_label, "ATENCAO: contagem BMS diferente de 98; rever configuracao");
    if (valid_count) {
      snprintf(s, sizeof(s), "MIN %ld mV (C%u)     MAX %ld mV (C%u)     DELTA %ld mV",
               long(lo), mini, long(hi), maxi, long(hi - lo)); text(cell_stats, s);
      int32_t bottom = (lo / 50) * 50 - 50; if (bottom < 0) bottom = 0;
      int32_t top = ((hi + 49) / 50) * 50 + 50;
      static int32_t old_bottom = -1, old_top = -1;
      if (bottom != old_bottom || top != old_top) {
        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, bottom, top);
        old_bottom = bottom; old_top = top; changed = true;
      }
      snprintf(s, sizeof(s), "Base %ld mV | topo %ld mV | escala automatica, nao representa limites de seguranca",
               long(bottom), long(top)); text(cell_scale, s);
    } else {
      text(cell_stats, "MIN --     MAX --     DELTA --");
      text(cell_scale, "SEM DADOS / nao ha valores simulados");
    }
    if (changed) lv_chart_refresh(chart);
  }
  // Dala datalayer: positive W = charging, negative W = discharging.
  // CAN timeout is shared with the base: not per-sample freshness.
  const int32_t watts = b.status.active_power_W;
  const char* state = !live ? "ESTADO: SEM DADOS" :
                      watts > 0 ? "ESTADO: A CARREGAR" :
                      watts < 0 ? "ESTADO: A DESCARREGAR" : "ESTADO: EM REPOUSO";
  text(footer_label, state);
}
}  // namespace

void init_display() {
  if (attempted) return;
  attempted = true;
  Serial.println("LCD5B: UI-03 / equal cards / WiFi SSID / power state / pump LOCAL");

  // Register all LCD/I2C pins with the base's conflict detector.
  if (!esp32hal->alloc_pins("LCD5B", 14,38,18,17,10,39,0,45,48,47,21,
                           1,2,42,41,40,5,3,46,7,8,9)) return;
  if (!psramFound()) {
    Serial.println("LCD5B: PSRAM not detected; display disabled");
    return;
  }

  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = GPIO_NUM_8;
  cfg.scl_io_num = GPIO_NUM_9;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  if (!checked(i2c_new_master_bus(&cfg, &bus_handle), "I2C bus")) return;
  if (!add_device(0x24, &expander_mode) || !add_device(0x38, &expander_io)) return;

  // CH422G WR_IO: EXIO1 touch reset, EXIO2 backlight, EXIO3 LCD reset.
  // No writes to the separate open-drain DO output register.
  if (!write_byte(expander_mode, 0x01)) return;
  if (!write_byte(expander_io, 0xF1)) return; // resets low, backlight off
  delay(10);
  if (!write_byte(expander_io, 0xF9)) return; // release LCD reset only
  delay(100);
  esp_task_wdt_reset();
  if (!lcd.init()) {
    Serial.println("LCD5B: RGB initialization failed");
    return;
  }
  lcd.setRotation(0);
  lcd.fillScreen(0x0000);
  esp_task_wdt_reset();

  lv_init();
  lv_tick_set_cb(tick_ms);
  constexpr size_t bytes = LCD_WIDTH * 10 * sizeof(uint16_t);
  draw_buffer = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!draw_buffer) {
    Serial.println("LCD5B: cannot allocate 20480-byte draw buffer");
    return;
  }
  display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  if (!display) {
    heap_caps_free(draw_buffer);
    draw_buffer = nullptr;
    Serial.println("LCD5B: cannot create LVGL display");
    return;
  }
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, draw_buffer, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);

  build_ui();
  // First enable backlight, then release/select the GT911 address.
  if (!write_byte(expander_io, 0xFD)) return;
  touch_ready = init_touch();
  refresh_data();
  ready = true;
  Serial.printf("LCD5B: ready; buffer=%u, free internal=%u, free PSRAM=%u\n",
                unsigned(bytes), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void update_display() {
  // Existing connectivity_loop owns both init_display and update_display.
  // No other task may call LVGL in this test driver.
  if (!ready) return;
  uint32_t now = millis();
  poll_touch(now);
  static uint32_t last_update = 0;
  static bool last_page = false;
  if (uint32_t(now - last_update) >= 1000 || last_page != cells_visible || refresh_requested) {
    last_update = now;
    last_page = cells_visible;
    refresh_data();
    refresh_requested = false;
  }
  lv_timer_handler();
}
#endif
