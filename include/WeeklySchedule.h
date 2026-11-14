#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

// Ten weekly ON/OFF commands, Monday bit 0 through Sunday bit 6.
// Disabled actions are ignored; most recent scheduled command wins.
class WeeklySchedule {
public:
  enum Action : uint8_t { Disabled=0, On=1, Off=2 };
  static constexpr size_t SlotCount=10;
  struct Slot {
    Action action=Disabled;
    uint16_t minute=1080; // 18:00
    uint8_t days=0x7f;  // Monday..Sunday
  };
  void begin(Preferences &prefs);
  const Slot &slot(size_t i) const { return slots_[i]; }
  bool setSlot(size_t i,const Slot &s);
  bool requestedOn(const tm &local) const;
  void save();
  static bool valid(const Slot &s);
private:
  Preferences *prefs_=nullptr;
  Slot slots_[SlotCount];
};
