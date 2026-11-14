#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

// One optional local-time interval per day; supports crossing midnight.
// tm_wday uses Sunday=0. Slots are stored Monday=0 through Sunday=6.
class WeeklySchedule {
public:
  struct Day {
    bool enabled = false;
    uint16_t startMin = 1080;  // 18:00
    uint16_t endMin = 1320;    // 22:00
  };
  void begin(Preferences &preferences);
  const Day &day(size_t mondayBasedIndex) const { return days_[mondayBasedIndex]; }
  static bool valid(const Day &day);
  bool setDay(size_t mondayBasedIndex, const Day &day);
  bool active(const tm &localTime) const;
  void save();
private:
  Preferences *prefs_ = nullptr;
  Day days_[7];
  static bool inRange(const Day &day, uint16_t minute);
};
