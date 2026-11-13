#include "LightOutput.h"

bool LightOutput::validGpio(uint8_t gpio) {
  // GPIO4/5: radar UART, 8: onboard LED + strapping, 2/9: strapping,
  // 18/19: USB. Allow 3,6,7,10 only for the initial SuperMini board.
  return gpio == 3 || gpio == 6 || gpio == 7 || gpio == 10;
}
const char *LightOutput::typeName(Type t) {
  switch (t) {
    case Type::Gpio: return "gpio";
    case Type::Http: return "http";
    default: return "disabled";
  }
}
const char *LightOutput::resultName(Result r) {
  switch (r) {
    case Result::AppliedLocal: return "applied_local";
    case Result::HttpAccepted: return "http_accepted_unconfirmed";
    case Result::HttpFailed: return "http_failed";
    case Result::WifiOffline: return "wifi_offline";
    default: return "unknown";
  }
}
void LightOutput::driveLed(bool on) {
  // The blue LED on many, but not all, SuperMini boards is active LOW on GPIO8.
  // GPIO8 is a strapping pin: never attach an external load to it.
  if (!cfg_.ledEnabled) {
    pinMode(LED_GPIO, INPUT);
    return;
  }
  digitalWrite(LED_GPIO, (on == cfg_.ledActiveHigh) ? HIGH : LOW);
  pinMode(LED_GPIO, OUTPUT);
}
void LightOutput::releaseGpio() {
  if (cfg_.type == Type::Gpio) {
    digitalWrite(cfg_.gpio, cfg_.activeHigh ? LOW : HIGH);
    pinMode(cfg_.gpio, OUTPUT);
  }
}
void LightOutput::driveGpio(bool on) {
  if (cfg_.type != Type::Gpio) return;
  // Set latch BEFORE switching into output mode.
  digitalWrite(cfg_.gpio, (on == cfg_.activeHigh) ? HIGH : LOW);
  pinMode(cfg_.gpio, OUTPUT);
}
void LightOutput::begin(Preferences &prefs) {
  prefs_ = &prefs;
  cfg_.type = static_cast<Type>(prefs.getUChar("outType", 0));
  if (cfg_.type != Type::Disabled && cfg_.type != Type::Gpio && cfg_.type != Type::Http)
    cfg_.type = Type::Disabled;
  cfg_.gpio = prefs.getUChar("outPin", 7);
  cfg_.activeHigh = prefs.getBool("outHigh", false);
  cfg_.ledEnabled = prefs.getBool("ledEnable", true);
  cfg_.ledActiveHigh = prefs.getBool("ledHigh", false);
  cfg_.urlOn = prefs.getString("urlOn", "");
  cfg_.urlOff = prefs.getString("urlOff", "");
  cfg_.httpMethod = prefs.getString("httpMethod", "POST");
  if (cfg_.type == Type::Gpio && !validGpio(cfg_.gpio))
    cfg_.type = Type::Disabled;
  driveGpio(false);
  driveLed(false);
}
void LightOutput::save() {
  if (!prefs_) return;
  prefs_->putUChar("outType", static_cast<uint8_t>(cfg_.type));
  prefs_->putUChar("outPin", cfg_.gpio);
  prefs_->putBool("outHigh", cfg_.activeHigh);
  prefs_->putBool("ledEnable", cfg_.ledEnabled);
  prefs_->putBool("ledHigh", cfg_.ledActiveHigh);
  prefs_->putString("urlOn", cfg_.urlOn);
  prefs_->putString("urlOff", cfg_.urlOff);
  prefs_->putString("httpMethod", cfg_.httpMethod);
}
bool LightOutput::configure(const Config &candidate, String &error) {
  if (candidate.type == Type::Gpio && !validGpio(candidate.gpio)) {
    error = "GPIO not permitted"; return false;
  }
  if (candidate.type == Type::Http) {
    if (!candidate.urlOn.startsWith("http://") || !candidate.urlOff.startsWith("http://") ||
        candidate.urlOn.length() > 240 || candidate.urlOff.length() > 240) {
      error = "HTTP mode needs two http:// URLs (max 240 chars)"; return false;
    }
    if (candidate.httpMethod != "GET" && candidate.httpMethod != "POST") {
      error = "HTTP method must be GET or POST"; return false;
    }
  }
  // An existing relay is turned OFF before swapping its pin or polarity.
  releaseGpio();
  cfg_ = candidate;
  requested_ = false;
  pending_ = false;
  retries_ = 0;
  result_ = Result::Unknown;
  driveGpio(false);
  driveLed(false);
  save();
  return true;
}
void LightOutput::request(bool on) {
  if (requested_ == on) return;
  requested_ = on;
  driveLed(on);
  if (cfg_.type == Type::Gpio) {
    driveGpio(on);
    result_ = Result::AppliedLocal;
    pending_ = false;
  } else if (cfg_.type == Type::Http) {
    pending_ = true;
    retries_ = 0;
    nextTry_ = 0;
    result_ = Result::Unknown;
  } else {
    result_ = Result::Unknown;
  }
}
bool LightOutput::sendHttp(bool on) {
  const String &url = on ? cfg_.urlOn : cfg_.urlOff;
  HTTPClient http;
  if (!http.begin(url)) return false;
  http.setConnectTimeout(700);
  http.setTimeout(700);
  int code = cfg_.httpMethod == "GET" ? http.GET() : http.POST("");
  http.end();
  return code >= 200 && code < 300;
}
void LightOutput::update() {
  if (cfg_.type != Type::Http || !pending_) return;
  if (static_cast<int32_t>(millis() - nextTry_) < 0) return;
  if (WiFi.status() != WL_CONNECTED) {
    result_ = Result::WifiOffline;
    nextTry_ = millis() + 5000;
    return;
  }
  // A single HTTP attempt can block for up to the configured timeout.
  // This is a prototype: replace with an asynchronous transport before
  // enabling radar tracking and high-frequency status updates.
  bool ok = sendHttp(requested_);
  if (ok) {
    result_ = Result::HttpAccepted;
    pending_ = false;
  } else {
    result_ = Result::HttpFailed;
    if (++retries_ >= 3) {
      pending_ = false; // Avoid uncontrolled retry traffic.
    } else {
      nextTry_ = millis() + 2000UL * retries_;
    }
  }
}
