#ifndef HW_WAVESHARE_LCD_5B_H
#define HW_WAVESHARE_LCD_5B_H

#include "hal.h"

class WaveshareLCD5BHal : public Esp32Hal {
 public:
  const char* name() override {
    return "Waveshare ESP32-S3-Touch-LCD-5B";
  }

  // CAN
  gpio_num_t CAN_TX_PIN() override { return GPIO_NUM_15; }
  gpio_num_t CAN_RX_PIN() override { return GPIO_NUM_16; }

  // RS485
  gpio_num_t RS485_TX_PIN() override { return GPIO_NUM_44; }
  gpio_num_t RS485_RX_PIN() override { return GPIO_NUM_43; }

  // Remaining Pins
  std::vector<comm_interface> available_interfaces() override {
    return {
        comm_interface::Modbus,
        comm_interface::RS485,
        comm_interface::CanNative
    };
  }
};

#define HalClass WaveshareLCD5BHal

#ifndef HW_CONFIGURED
#define HW_CONFIGURED
#else
#error Multiple HW defined! Please select a single HW
#endif

#endif
