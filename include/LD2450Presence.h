#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// LD2450 periodic target reports (30 bytes, 3 targets, UART 256000 8N1).
class LD2450Presence {
public:
  struct Target {
    bool valid = false;
    int16_t xMm = 0;
    int16_t yMm = 0;
    int16_t speedCmS = 0;
    uint16_t resolutionMm = 0;
  };
  struct Zone {
    bool enabled = false;
    int16_t minX = -1500;
    int16_t maxX = 1500;
    int16_t minY = 0;
    int16_t maxY = 4000;
  };
  void begin(HardwareSerial &port, int rxPin=4, int txPin=5);
  void update();
  bool healthy() const { return everReceived_ && (uint32_t)(millis()-lastFrameMs_)<2500; }
  bool present() const;
  uint8_t count() const;
  uint32_t frames() const { return frames_; }
  const Target &target(size_t n) const { return targets_[n]; } // n=0..2
  void setZone(const Zone &zone) { zone_ = zone; }
  Zone zone() const { return zone_; }
  bool inside(const Target &t) const;
  static bool validZone(const Zone &z);
  // Testable pure decode helper: returns false for wrong framing.
  static bool decode(const uint8_t *frame, size_t len, Target out[3]);
private:
  HardwareSerial *port_ = nullptr;
  uint8_t data_[30] = {};
  uint8_t received_ = 0;
  Target targets_[3] = {};
  Zone zone_;
  bool everReceived_ = false;
  uint32_t lastFrameMs_ = 0, frames_ = 0;
  void consume(uint8_t byte);
};
