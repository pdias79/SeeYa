#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// LD2410C basic target data frames. No engineering configuration in v1.
class RadarPresence {
public:
  void begin(HardwareSerial &port, int rxPin=4, int txPin=5);
  void update();
  bool present() const { return healthy() && targetState_ != 0; }
  bool healthy() const { return everReceived_ && (uint32_t)(millis() - lastFrameMs_) < 2500; }
  uint8_t targetState() const { return healthy() ? targetState_ : 0; }
  uint16_t movingDistanceCm() const { return movingCm_; }
  uint16_t stationaryDistanceCm() const { return stationaryCm_; }
  uint8_t movingEnergy() const { return movingEnergy_; }
  uint8_t stationaryEnergy() const { return stationaryEnergy_; }
  uint32_t frames() const { return frames_; }
private:
  HardwareSerial *port_=nullptr;
  uint8_t data_[64]={};
  size_t received_=0;
  bool everReceived_=false;
  uint32_t lastFrameMs_=0;
  uint32_t frames_=0;
  uint8_t targetState_=0, movingEnergy_=0, stationaryEnergy_=0;
  uint16_t movingCm_=0, stationaryCm_=0;
  void consume(uint8_t byte);
  bool decodeFrame(const uint8_t *p, size_t len);
};
