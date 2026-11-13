#include "RadarPresence.h"
#include <cstring>
namespace {
constexpr uint8_t HEAD[4]={0xF4,0xF3,0xF2,0xF1};
constexpr uint8_t TAIL[4]={0xF8,0xF7,0xF6,0xF5};
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
}
void RadarPresence::begin(HardwareSerial &port, int rxPin, int txPin) {
  port_ = &port;
  received_ = 0;
  everReceived_ = false;
  targetState_ = 0;
  frames_ = 0;
  port_->setRxBufferSize(1024);
  port_->begin(256000, SERIAL_8N1, rxPin, txPin);
}
bool RadarPresence::decodeFrame(const uint8_t *p, size_t len) {
  // Basic report: header(4), length(2)=0x000d, data(13), footer(4).
  // Data: 02 AA state moveDist(2) moveEnergy stillDist(2)
  // stillEnergy detectionDist(2) 55 00
  if (len != 23 || u16(p+4) != 13) return false;
  if (memcmp(p,HEAD,4) != 0 || memcmp(p+19,TAIL,4) != 0) return false;
  const uint8_t *d = p+6;
  if (d[0] != 0x02 || d[1] != 0xAA ||
      d[11] != 0x55 || d[12] != 0 || d[2] > 3) return false;
  targetState_ = d[2];
  movingCm_ = u16(d+3);
  movingEnergy_ = d[5];
  stationaryCm_ = u16(d+6);
  stationaryEnergy_ = d[8];
  everReceived_ = true;
  lastFrameMs_ = millis();
  ++frames_;
  return true;
}
void RadarPresence::consume(uint8_t b) {
  if(received_ == 0) {
    if(b == HEAD[0]) { data_[0]=b; received_=1; }
    return;
  }
  if(received_ < 4) {
    if (b == HEAD[received_]) data_[received_++]=b;
    else { received_=0; if (b==HEAD[0]) {data_[0]=b;received_=1;} }
    return;
  }
  if(received_ >= sizeof(data_)) { received_=0; return; }
  data_[received_++] = b;
  if(received_ == 6) {
    size_t payload=uint16_t(data_[4]) | (uint16_t(data_[5])<<8);
    if (payload != 13) received_=0; // This first driver accepts basic reports only.
  } else if(received_ == 23) {
    decodeFrame(data_,received_);
    received_=0;
  }
}
void RadarPresence::update() {
  if(!port_) return;
  // Limit work per iteration to keep the web server responsive.
  size_t count=0;
  while(port_->available() && count++ < 128) consume(uint8_t(port_->read()));
}
