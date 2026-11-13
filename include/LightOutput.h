#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

// v0.2: one output at a time. The state reported for HTTP is a command result,
// not proof that the physical plug has switched.
class LightOutput {
public:
  enum class Type { Disabled, Gpio, Http };
  enum class Result { Unknown, AppliedLocal, HttpAccepted, HttpFailed, WifiOffline };
  struct Config {
    Type type = Type::Disabled;
    uint8_t gpio = 7;
    bool activeHigh = false;
    bool ledEnabled = true;
    bool ledActiveHigh = false;
    String urlOn;
    String urlOff;
    String httpMethod = "POST";
  };
  void begin(Preferences &prefs);
  bool configure(const Config &candidate, String &error);
  const Config &config() const { return cfg_; }
  void request(bool on);
  void update();
  bool requested() const { return requested_; }
  Result result() const { return result_; }
  bool pending() const { return pending_; }
  static const char *typeName(Type t);
  static const char *resultName(Result r);
private:
  static constexpr uint8_t LED_GPIO = 8;
  Preferences *prefs_ = nullptr;
  Config cfg_;
  bool requested_ = false;
  bool pending_ = false;
  uint8_t retries_ = 0;
  unsigned long nextTry_ = 0;
  Result result_ = Result::Unknown;
  void releaseGpio();
  void driveGpio(bool on);
  void driveLed(bool on);
  void save();
  bool sendHttp(bool on);
  static bool validGpio(uint8_t gpio);
};
