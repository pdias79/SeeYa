#include "WeeklySchedule.h"

bool WeeklySchedule::valid(const Day &d) {
  return d.startMin < 1440 && d.endMin < 1440 && d.startMin != d.endMin;
}
bool WeeklySchedule::setSlot(size_t day, size_t slotIndex, const Day &d) {
  if(day>=7 || slotIndex>=SlotsPerDay || !valid(d))return false;
  days_[day][slotIndex]=d;
  return true;
}
void WeeklySchedule::begin(Preferences &prefs) {
  prefs_=&prefs;
  for(size_t day=0;day<7;++day) {
    for(size_t slotIndex=0;slotIndex<SlotsPerDay;++slotIndex) {
      // Keep legacy keys for the first slot; slot 2 gets new, distinct keys.
      const String stem="sch"+String(day);
      const String suffix=slotIndex==0?"":"2";
      Day d;
      d.enabled=prefs.getBool((stem+"e"+suffix).c_str(),false);
      d.startMin=prefs.getUShort((stem+"s"+suffix).c_str(),slotIndex==0?1080:720);
      d.endMin=prefs.getUShort((stem+"f"+suffix).c_str(),slotIndex==0?1320:780);
      if(valid(d))days_[day][slotIndex]=d;
    }
  }
}
void WeeklySchedule::save() {
  if(!prefs_)return;
  for(size_t day=0;day<7;++day) {
    for(size_t slotIndex=0;slotIndex<SlotsPerDay;++slotIndex) {
      const String stem="sch"+String(day);
      const String suffix=slotIndex==0?"":"2";
      const Day &d=days_[day][slotIndex];
      prefs_->putBool((stem+"e"+suffix).c_str(),d.enabled);
      prefs_->putUShort((stem+"s"+suffix).c_str(),d.startMin);
      prefs_->putUShort((stem+"f"+suffix).c_str(),d.endMin);
    }
  }
}
bool WeeklySchedule::inRange(const Day &day, uint16_t minute) {
  return day.enabled && minute>=day.startMin && minute<day.endMin;
}
bool WeeklySchedule::active(const tm &localTime) const {
  const size_t today=(localTime.tm_wday+6)%7;
  const size_t yesterday=(today+6)%7;
  const uint16_t minute=localTime.tm_hour*60+localTime.tm_min;
  for(size_t slotIndex=0;slotIndex<SlotsPerDay;++slotIndex) {
    const Day &now=days_[today][slotIndex];
    const Day &prev=days_[yesterday][slotIndex];
    if(now.enabled) {
      if(now.startMin<now.endMin && inRange(now,minute))return true;
      if(now.startMin>now.endMin && minute>=now.startMin)return true;
    }
    if(prev.enabled && prev.startMin>prev.endMin && minute<prev.endMin)return true;
  }
  return false;
}
