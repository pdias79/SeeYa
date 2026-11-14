#include "WeeklySchedule.h"

bool WeeklySchedule::valid(const Slot &s) {
  return s.action<=Off && s.minute<1440 && (s.days & 0x80)==0;
}
bool WeeklySchedule::setSlot(size_t i,const Slot &s) {
  if(i>=SlotCount||!valid(s))return false;
  slots_[i]=s;return true;
}
void WeeklySchedule::begin(Preferences &prefs) {
  prefs_=&prefs;
  // New schema deliberately does not interpret the old daily interval keys:
  // ON/OFF events represent a different semantic model.
  for(size_t i=0;i<SlotCount;++i) {
    const String stem="evt"+String(i);
    Slot s;
    s.action=static_cast<Action>(prefs.getUChar((stem+"a").c_str(),0));
    s.minute=prefs.getUShort((stem+"m").c_str(),1080);
    s.days=prefs.getUChar((stem+"d").c_str(),0x7f);
    if(valid(s))slots_[i]=s;
  }
}
void WeeklySchedule::save() {
  if(!prefs_)return;
  for(size_t i=0;i<SlotCount;++i) {
    const String stem="evt"+String(i);
    const Slot &s=slots_[i];
    prefs_->putUChar((stem+"a").c_str(),s.action);
    prefs_->putUShort((stem+"m").c_str(),s.minute);
    prefs_->putUChar((stem+"d").c_str(),s.days);
  }
}
bool WeeklySchedule::requestedOn(const tm &local) const {
  // Search the past seven days and today's elapsed minutes. A command
  // persists until the next eligible command. At equal instants, the
  // higher-numbered slot wins for deterministic behavior.
  const int today=(local.tm_wday+6)%7;
  const int minute=local.tm_hour*60+local.tm_min;
  int bestAge=8*1440;
  int bestIndex=-1;
  Action selected=Off;
  for(size_t i=0;i<SlotCount;++i) {
    const Slot &s=slots_[i];
    if(s.action==Disabled||!s.days)continue;
    for(int back=0;back<7;++back) {
      const int weekday=(today-back+7)%7;
      if(!(s.days & (1<<weekday)))continue;
      int age=back*1440+minute-int(s.minute);
      if(age<0)continue;
      if(age<bestAge || (age==bestAge && int(i)>bestIndex)) {
        bestAge=age;
        bestIndex=int(i);
        selected=s.action;
      }
      break; // latest eligible instance of this slot
    }
  }
  return selected==On;
}
