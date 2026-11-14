#include "WeeklySchedule.h"

bool WeeklySchedule::valid(const Day &d) {
  return d.startMin < 1440 && d.endMin < 1440 && d.startMin != d.endMin;
}
bool WeeklySchedule::setDay(size_t i, const Day &d) {
  if (i >= 7 || !valid(d)) return false;
  days_[i] = d;
  return true;
}
void WeeklySchedule::begin(Preferences &prefs) {
  prefs_ = &prefs;
  for (size_t i=0;i<7;++i) {
    const String stem="sch"+String(i);
    Day d;
    d.enabled=prefs.getBool((stem+"e").c_str(),false);
    d.startMin=prefs.getUShort((stem+"s").c_str(),1080);
    d.endMin=prefs.getUShort((stem+"f").c_str(),1320);
    if(valid(d))days_[i]=d;
  }
}
void WeeklySchedule::save() {
  if(!prefs_)return;
  for(size_t i=0;i<7;++i) {
    const String stem="sch"+String(i);
    prefs_->putBool((stem+"e").c_str(),days_[i].enabled);
    prefs_->putUShort((stem+"s").c_str(),days_[i].startMin);
    prefs_->putUShort((stem+"f").c_str(),days_[i].endMin);
  }
}
bool WeeklySchedule::inRange(const Day &day, uint16_t minute) {
  return day.enabled && minute>=day.startMin && minute<day.endMin;
}
bool WeeklySchedule::active(const tm &localTime) const {
  // Monday-based indexing. Check yesterday for intervals that span midnight.
  const size_t today=(localTime.tm_wday+6)%7;
  const size_t yesterday=(today+6)%7;
  const uint16_t minute=localTime.tm_hour*60+localTime.tm_min;
  const Day &now=days_[today];
  const Day &prev=days_[yesterday];
  if(now.enabled) {
    if(now.startMin<now.endMin && inRange(now,minute))return true;
    if(now.startMin>now.endMin && minute>=now.startMin)return true;
  }
  return prev.enabled && prev.startMin>prev.endMin && minute<prev.endMin;
}
