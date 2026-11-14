#include "LD2450Presence.h"

namespace {
constexpr uint8_t kHeader[4] = {0xAA,0xFF,0x03,0x00};
uint16_t word(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
// Hi-Link's sign-magnitude-like format: bit 15 SET => positive,
// bit 15 CLEAR => negative; magnitude is in the low 15 bits.
int16_t signedTarget(uint16_t raw) {
  int16_t magnitude = int16_t(raw & 0x7FFF);
  return (raw & 0x8000) ? magnitude : -magnitude;
}
}
bool LD2450Presence::decode(const uint8_t *p, size_t len, Target out[3]) {
  if (!p || !out || len != 30 || p[28] != 0x55 || p[29] != 0xCC) return false;
  for (int i=0;i<4;++i) if(p[i] != kHeader[i]) return false;
  for (size_t i=0;i<3;++i) {
    const uint8_t *d=p+4+8*i;
    bool empty=true;
    for(size_t j=0;j<8;++j) if(d[j]!=0) {empty=false;break;}
    out[i] = Target{};
    if(empty) continue;
    out[i].valid = true;
    out[i].xMm = signedTarget(word(d));
    out[i].yMm = signedTarget(word(d+2));
    out[i].speedCmS = signedTarget(word(d+4));
    out[i].resolutionMm = word(d+6);
  }
  return true;
}
bool LD2450Presence::validZone(const Zone &z) {
  if (!z.enabled) return true;
  return z.minX >= -6000 && z.maxX <= 6000 && z.minX < z.maxX &&
         z.minY >= 0 && z.maxY <= 6000 && z.minY < z.maxY;
}
bool LD2450Presence::inside(const Target &t) const {
  if (!t.valid) return false;
  if (!zone_.enabled) return true;
  return t.xMm >= zone_.minX && t.xMm <= zone_.maxX &&
         t.yMm >= zone_.minY && t.yMm <= zone_.maxY;
}
bool LD2450Presence::present() const {
  if(!healthy()) return false;
  for(const auto &t:targets_) if(inside(t)) return true;
  return false;
}
uint8_t LD2450Presence::count() const {
  if(!healthy()) return 0;
  uint8_t n=0;
  for(const auto &t:targets_) if(t.valid) ++n;
  return n;
}
void LD2450Presence::begin(HardwareSerial &port, int rxPin, int txPin) {
  port_=&port;
  received_=0; everReceived_=false; frames_=0;
  for(auto &t:targets_) t=Target{};
  port_->setRxBufferSize(1024);
  port_->begin(256000, SERIAL_8N1, rxPin, txPin);
}
void LD2450Presence::consume(uint8_t b) {
  if(received_<4) {
    if(b==kHeader[received_]) data_[received_++]=b;
    else {
      received_=0;
      if(b==kHeader[0]) data_[received_++]=b;
    }
    return;
  }
  data_[received_++]=b;
  if(received_==30) {
    Target decoded[3];
    if(decode(data_,30,decoded)) {
      for(size_t i=0;i<3;++i) targets_[i]=decoded[i];
      lastFrameMs_=millis(); everReceived_=true; ++frames_;
    }
    received_=0;
  }
}
void LD2450Presence::update() {
  if(!port_)return;
  size_t count=0;
  while(port_->available() && count++<128)consume(uint8_t(port_->read()));
}
