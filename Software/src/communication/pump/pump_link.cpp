#include "pump_link.h"
#if defined(HW_WAVESHARE_LCD_5B)
#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../rs485/comm_rs485.h"
#include "../../datalayer/datalayer.h"
#include "../../devboard/safety/safety.h"

namespace pump_link {
namespace {
constexpr uint32_t POLL_MS=250, LINK_MS=1000, REPLY_MS=200;
constexpr uint8_t MAX_DEMAND=153;
std::atomic<uint8_t> manual{0};
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
State published;
uint16_t crc16(const uint8_t* b, unsigned n) {
  uint16_t crc=0xFFFF;
  while(n--) {crc^=*b++;for(int i=0;i<8;++i)crc=(crc&1)?(crc>>1)^0xA001:crc>>1;}
  return crc;
}
void put16(uint8_t* p,uint16_t n){p[0]=uint8_t(n);p[1]=uint8_t(n>>8);}
uint16_t get16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
void put32(uint8_t* p,uint32_t n){for(int i=0;i<4;++i)p[i]=uint8_t(n>>(i*8));}
uint32_t get32(const uint8_t* p){uint32_t n=0;for(int i=0;i<4;++i)n|=uint32_t(p[i])<<(i*8);return n;}

// PDS194: passive receive diagnostics. No change to pins, parser acceptance,
// command ceiling, temperature rules, reply deadline or watchdog.
struct RxDiag {
  uint32_t bytes=0,headers=0,crcBad=0,valid=0,echo=0,gaps=0,accepted=0,tx=0;
  uint32_t workerGap=0;
  uint8_t rejectMask=0,lastFlags=0;
  uint8_t sample[16]={};
  unsigned sampleSize=0;
};

void report_rx(const RxDiag& d,unsigned line) {
  // One short complete line per idle iteration; never wait for USB space.
  char b[64];
  int n=0;
  if(line==0)n=snprintf(b,sizeof(b),"P194 rx=%lu hdr=%lu\n",(unsigned long)d.bytes,(unsigned long)d.headers);
  else if(line==1)n=snprintf(b,sizeof(b),"P194 valid=%lu crc_bad=%lu\n",(unsigned long)d.valid,(unsigned long)d.crcBad);
  else if(line==2)n=snprintf(b,sizeof(b),"P194 echo=%lu gaps=%lu\n",(unsigned long)d.echo,(unsigned long)d.gaps);
  else if(line==3)n=snprintf(b,sizeof(b),"P194 accepted=%lu tx=%lu\n",(unsigned long)d.accepted,(unsigned long)d.tx);
  else if(line==4)n=snprintf(b,sizeof(b),"P194 gap_ms=%lu reject=%02X flags=%02X\n",
                           (unsigned long)d.workerGap,d.rejectMask,d.lastFlags);
  else {
    unsigned start=(line-5)*8;
    n=snprintf(b,sizeof(b),"P194 raw%u=",start);
    for(unsigned i=start;i<start+8 && i<d.sampleSize;++i)
      n+=snprintf(b+n,sizeof(b)-unsigned(n),"%02X ",d.sample[i]);
    n+=snprintf(b+n,sizeof(b)-unsigned(n),"\n");
  }
  if(Serial && n>0 && size_t(n)<sizeof(b) && Serial.availableForWrite()>=n)
    Serial.write(reinterpret_cast<const uint8_t*>(b),size_t(n));
}

void worker(void*) {
  State s; s.enabled=true;
  uint32_t session=esp_random(); if(!session)session=1;
  uint16_t seq=0;
  uint8_t sentDemand=0,rx[20]={}; unsigned used=0;
  uint32_t lastPoll=millis()-POLL_MS, lastByte=0, requestAt=0, lastLog=0;
  bool awaiting=false,seen=false,autoLatch=false,previousOnline=false;
  RxDiag diag,snapshot;
  unsigned reportLine=7;
  uint32_t previousLoop=millis(),lastReportLine=0;
  for(;;) {
    uint32_t now=millis();
    uint32_t gap=uint32_t(now-previousLoop);previousLoop=now;
    if(gap>diag.workerGap)diag.workerGap=gap;
    if(used && uint32_t(now-lastByte)>50){used=0;++diag.gaps;}
    for(unsigned budget=0;budget<80 && Serial2.available();++budget) {
      int v=Serial2.read();if(v<0)break;lastByte=millis();
      ++diag.bytes;
      if(diag.sampleSize<sizeof(diag.sample))diag.sample[diag.sampleSize++]=uint8_t(v);
      rx[used++]=uint8_t(v);
      if(used<20)continue;
      bool header=rx[0]==0xA5 && rx[1]==0x5A && rx[2]==1;
      if(header){
        ++diag.headers;
        bool crcOK=get16(rx+18)==crc16(rx,18);
        if(!crcOK)++diag.crcBad;
        else if(rx[3]==1)++diag.echo; // Request-shaped traffic, not proof of source.
      }
      bool valid=rx[0]==0xA5 && rx[1]==0x5A && rx[2]==1 && rx[3]==2 &&
                 get16(rx+18)==crc16(rx,18);
      if(valid) {
        ++diag.valid;diag.lastFlags=rx[11];
        bool match=awaiting && get32(rx+4)==session && get16(rx+8)==seq && rx[10]==sentDemand &&
                   uint32_t(millis()-requestAt)<=REPLY_MS && (rx[11]&0xF8)==0;
        if(match) {
          ++diag.accepted;
          awaiting=false;seen=true;s.reply_at=millis();
          bool fresh=(rx[11]&2) && get16(rx+12)<=500;
          s.alarm=rx[17];
          s.pump_ready=(rx[11]&1) && fresh && !(rx[11]&4) && !(rx[14]&2) && rx[17]==0;
          s.feedback_speed=rx[15];s.feedback_current=rx[16];
        } else {
          ++s.rejected;
          diag.rejectMask=(!awaiting?1:0)|(get32(rx+4)!=session?2:0)|
                          (get16(rx+8)!=seq?4:0)|(rx[10]!=sentDemand?8:0)|
                          (uint32_t(millis()-requestAt)>REPLY_MS?16:0)|
                          ((rx[11]&0xF8)?32:0);
        }
        used=0;
      } else {
        memmove(rx,rx+1,19);used=19; // bounded sliding resynchronization
      }
    }
    now=millis();
    s.online=seen && uint32_t(now-s.reply_at)<=LINK_MS;
    if(!s.online) {s.pump_ready=false;autoLatch=false;}
    if(previousOnline && (!s.online || !s.pump_ready))manual.store(0);
    previousOnline=s.online;
    // Presentation-grade BMS validity uses the base's existing CAN timeout.
    // No independent timestamp is available for the temperature field here.
    int16_t temp=datalayer.battery.status.temperature_max_dC;
    s.temperature_valid=battery_detected && datalayer.battery.status.CAN_battery_still_alive>0 &&
                        temp>=-400 && temp<=1000;
    if(!s.temperature_valid || !s.online || !s.pump_ready) autoLatch=false;
    else if(temp>300)autoLatch=true;
    else if(temp<=250)autoLatch=false;
    s.automatic=autoLatch;s.manual=manual.load();
    s.desired=autoLatch?MAX_DEMAND:s.manual;
    uint8_t effective=(s.online && s.pump_ready)?s.desired:0;
    if(uint32_t(now-lastPoll)>=POLL_MS) {
      lastPoll=now;
      if(++seq==0) {session=esp_random();if(!session)session=1;seq=1;seen=false;s.online=false;s.pump_ready=false;effective=0;}
      uint8_t tx[20]={0xA5,0x5A,1,1};put32(tx+4,session);put16(tx+8,seq);
      tx[10]=effective;
      tx[11]=(s.temperature_valid?1:0)|(autoLatch?2:0)|(s.manual?4:0);
      put16(tx+12,uint16_t(s.temperature_valid?temp:INT16_MIN));
      put16(tx+18,crc16(tx,18));
      awaiting=false;
      if(Serial2.availableForWrite()>=20) {
        size_t n=Serial2.write(tx,sizeof(tx));
        if(n==sizeof(tx)){++diag.tx;s.sent=effective;sentDemand=effective;requestAt=millis();awaiting=true;}
        else {seen=false;s.online=false;s.pump_ready=false;manual.store(0);}
      } else {seen=false;s.online=false;s.pump_ready=false;manual.store(0);}
    }
    portENTER_CRITICAL(&mux);published=s;portEXIT_CRITICAL(&mux);
    if(uint32_t(now-lastLog)>=5000) {
      lastLog=now;
      snapshot=diag;reportLine=0;
      diag.sampleSize=0;diag.workerGap=0;
      Serial.printf("PUMP485 online=%u ready=%u manual=%u auto=%u sent=%u temp_valid=%u replies_rejected=%lu\n",
                    s.online,s.pump_ready,s.manual,s.automatic,s.sent,s.temperature_valid,(unsigned long)s.rejected);
    }
    // Keep extra diagnostics away from the request and response turnaround.
    if(reportLine<7 && uint32_t(now-lastPoll)>80 &&
       uint32_t(now-lastPoll)<180 && uint32_t(now-lastReportLine)>=10) {
      lastReportLine=now;report_rx(snapshot,reportLine++);
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}
State get(){portENTER_CRITICAL(&mux);State s=published;portEXIT_CRITICAL(&mux);return s;}
bool set_manual(uint8_t value) {
  if(value>MAX_DEMAND)value=MAX_DEMAND;
  State s=get();
  if(value && (!s.enabled || !s.online || !s.pump_ready || uint32_t(millis()-s.reply_at)>LINK_MS))return false;
  manual.store(value);return true;
}
void begin() {
  static bool attempted=false;if(attempted)return;attempted=true;
  Serial.println("PDS199 PUMP485: applying bench-tested RX configuration");
  // Run after all protocol setup functions so existing UART pin owners win.
  // rs485_begin uses HAL allocation and refuses a second owner.
  if(!rs485_begin("PUMP485",Serial2,19200,SERIAL_8N1)) {
    Serial.println("PUMP485 disabled: RS485 pins unavailable; existing protocol preserved");return;
  }
  // LCD-5B only: same sequence as the successful PDS198 phase 2.
  // Run AFTER Arduino begin and HAL ownership checks, BEFORE starting worker.
  // Do not apply to the generic RS485 layer or change the board pin map.
  if(!uart_is_driver_installed(UART_NUM_2)) {
    Serial2.end();
    Serial.println("PDS199 RX setup failed: UART2 driver missing; pump link disabled");
    return;
  }
  const esp_err_t direction=gpio_set_direction(GPIO_NUM_43,GPIO_MODE_INPUT);
  const esp_err_t pins=uart_set_pin(UART_NUM_2,44,43,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
  const bool fifo=Serial2.setRxFIFOFull(1);
  const bool timeout=Serial2.setRxTimeout(2);
  Serial.printf("PDS199 RX setup direction=%d pins=%d fifo=%u timeout=%u\n",
                int(direction),int(pins),unsigned(fifo),unsigned(timeout));
  if(direction!=ESP_OK || pins!=ESP_OK || !fifo || !timeout) {
    Serial2.end();
    Serial.println("PDS199 RX setup failed; pump link disabled");
    return;
  }
  if(xTaskCreatePinnedToCore(worker,"pump485",4096,nullptr,2,nullptr,0)!=pdPASS) {
    Serial2.end();Serial.println("PUMP485 task failed; receiver watchdog must request zero");
  }
}
}
#else
namespace pump_link {
void begin(){}
State get(){return State{};}
bool set_manual(uint8_t){return false;}
}
#endif
