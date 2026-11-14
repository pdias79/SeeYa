#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>

// STA first; temporary WPA2 access point + captive DNS while offline.
class WifiFallback {
public:
  void begin(Preferences &prefs, const char *defaultSsid, const char *defaultPassword);
  void update();
  void configure(const String &ssid, const String &password, const String &apName);
  bool accessPointActive() const { return apActive_; }
  const String &ssid() const { return ssid_; }
  const String &apName() const { return apName_; }
  bool configured() const { return ssid_.length()>0; }
  static bool validApName(const String &name);
  static bool validStaCredentials(const String &ssid, const String &password);
private:
  Preferences *prefs_=nullptr;
  DNSServer dns_;
  String ssid_, password_, apName_;
  bool apActive_=false;
  uint32_t lastAttempt_=0, offlineSince_=0;
  void connectSta();
  void startAp();
  void stopAp();
};
