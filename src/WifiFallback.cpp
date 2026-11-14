#include "WifiFallback.h"

namespace {
constexpr uint32_t retryMs=15000;
constexpr uint32_t fallbackDelayMs=12000;
const IPAddress apIp(192,168,4,1);
}
bool WifiFallback::validApName(const String &name) {
  return name.length()>0 && name.length()<=32 &&
    name.indexOf('\n')<0 && name.indexOf('\r')<0;
}
bool WifiFallback::validStaCredentials(const String &ssid, const String &password) {
  return ssid.length()>0 && ssid.length()<=32 && password.length()<=63;
}
void WifiFallback::connectSta() {
  lastAttempt_=millis();
  if(!configured())return;
  WiFi.begin(ssid_.c_str(), password_.c_str());
  Serial.printf("[WiFi] Connecting to SSID: %s\n",ssid_.c_str());
}
void WifiFallback::startAp() {
  if(apActive_)return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp,apIp,IPAddress(255,255,255,0));
  if(!WiFi.softAP(apName_.c_str())) {
    Serial.println("[WiFi] Fallback AP start failed");return;
  }
  dns_.start(53,"*",apIp);
  apActive_=true;
  Serial.printf("[WiFi] Open captive AP SSID=%s IP=%s\n",apName_.c_str(),apIp.toString().c_str());
}
void WifiFallback::stopAp() {
  if(!apActive_)return;
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive_=false;
  Serial.println("[WiFi] Fallback AP stopped; home Wi-Fi connected");
}
void WifiFallback::begin(Preferences &prefs, const char *defaultSsid, const char *defaultPassword) {
  prefs_=&prefs;
  ssid_=prefs.getString("wifiSsid",defaultSsid);
  password_=prefs.getString("wifiPass",defaultPassword);
  const uint32_t chipSuffix=(uint32_t)(ESP.getEfuseMac() & 0xFFFFFF);
  char identifier[16];
  snprintf(identifier,sizeof(identifier),"%06lX",(unsigned long)chipSuffix);
  apName_=prefs.getString("apName",String("SeeYa_")+identifier);
  if(!validApName(apName_))apName_=String("SeeYa_")+identifier;
  if(!validStaCredentials(ssid_,password_))ssid_="";
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  offlineSince_=millis();
  if(configured())connectSta();
  else startAp();
}
void WifiFallback::configure(const String &ssid, const String &password, const String &apName) {
  ssid_=ssid;password_=password;
  if(apName_!=apName) {
    apName_=apName;
    if(apActive_)stopAp();
  }
  prefs_->putString("wifiSsid",ssid_);
  prefs_->putString("wifiPass",password_);
  prefs_->putString("apName",apName_);
  WiFi.disconnect();
  offlineSince_=millis();
  lastAttempt_=millis()-retryMs;
  if(configured())connectSta();
  else startAp();
}
void WifiFallback::update() {
  if(apActive_)dns_.processNextRequest();
  if(WiFi.status()==WL_CONNECTED) {
    offlineSince_=0;
    if(apActive_)stopAp();
    return;
  }
  if(offlineSince_==0)offlineSince_=millis();
  if(!apActive_ && (!configured() || (uint32_t)(millis()-offlineSince_)>=fallbackDelayMs))
    startAp();
  if(configured() && (uint32_t)(millis()-lastAttempt_)>=retryMs)connectSta();
}
