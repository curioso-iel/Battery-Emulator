#pragma once
#include <stdint.h>
namespace pump_link {
struct State {
  bool enabled=false, online=false, pump_ready=false, automatic=false, temperature_valid=false;
  uint8_t manual=0, desired=0, sent=0, feedback_speed=0, feedback_current=0, alarm=0;
  uint32_t reply_at=0, rejected=0;
};
void begin();
State get();
// Raw 0..153, volatile settings only. 0 allows automatic temperature control.
bool set_manual(uint8_t value);
}
