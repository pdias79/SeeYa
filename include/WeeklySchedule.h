#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

// Two optional local-time intervals per day; each supports crossing midnight.
// tm_wday uses Sunday=0. Slots are stored Monday=0 through Sunday=6.
class WeeklySchedule {
public:
  struct Day {
    bool enabled = false;
    uint16_t startMin = 1080;  // 18:00
    uint16_t endMin = 1320;    // 22:00
  };
  void begin(Preferences &preferences);
  static constexpr size_t SlotsPerDay = 2;
  const Day &day(size_t mondayBasedIndex) const { return days_[mondayBasedIndex][0]; }
  const Day &slot(size_t mondayBasedIndex, size_t slotIndex) const { return days_[mondayBasedIndex][slotIndex]; }
  static bool valid(const Day &day);
  bool setDay(size_t mondayBasedIndex, const Day &day) { return setSlot(mondayBasedIndex, 0, day); }
  bool setSlot(size_t mondayBasedIndex, size_t slotIndex, const Day &day);
  bool active(const tm &localTime) const;
  void save();
private:
  Preferences *prefs_ = nullptr;
  Day days_[7][SlotsPerDay];
  static bool inRange(const Day &day, uint16_t minute);
};
