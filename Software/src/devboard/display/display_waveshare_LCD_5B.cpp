// Checkpoint LCD-01: image only, no touch or battery controls.
// Based on the supplied board routing/timings; LVGL integration targets 9.3.0.
#if defined(HW_WAVESHARE_LCD_5B) && !defined(SMALL_FLASH_DEVICE)
#include "display.h"
#include "../hal/hal.h"
#include <Arduino.h>
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

      // Same strategy as a test reference project:
      // framebuffer managed by LovyanGFX in PSRAM.
      cfg.use_psram = 1;

      panel.config_detail(cfg);
    }

    {
      auto cfg = bus.config();

      cfg.panel = &panel;

      // RGB565 data pins: Waveshare 28151 routing.
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

      // Known-good timing from VaAndCob's 1024x600 example.
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
}  // namespace

void init_display() {
  if (attempted) return;
  attempted = true;
  Serial.println("LCD5B: image-only LVGL 9.3.0 test");

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

  lv_obj_t* screen = lv_display_get_screen_active(display);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x102030), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_t* label = lv_label_create(screen);
  lv_label_set_text(label, "Waveshare LCD 5B\nLVGL 9.3.0\nTESTE DE IMAGEM\nSem toque nesta etapa");
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(label);

  // Leave touch in reset for this image-only checkpoint; enable backlight.
  if (!write_byte(expander_io, 0xFD)) return;
  ready = true;
  Serial.printf("LCD5B: ready; buffer=%u, free internal=%u, free PSRAM=%u\n",
                unsigned(bytes), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void update_display() {
  // Existing connectivity_loop owns both init_display and update_display.
  // No other task may call LVGL in this test driver.
  if (ready) lv_timer_handler();
}
#endif
