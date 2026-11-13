#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

#ifndef SEEYA_VERSION
#define SEEYA_VERSION "dev"
#endif

namespace {
constexpr uint8_t RELAY_PIN = 7;
constexpr uint8_t LED_PIN = 8; // Common SuperMini: active LOW; verify actual board
WebServer server(80);
Preferences preferences;
bool relayActiveHigh = true;
bool ledActiveHigh = false;
bool lightRequested = false;
enum class Mode { Auto, ForceOn, ForceOff };
Mode mode = Mode::Auto;
bool otaOk = false;
bool otaStarted = false;
uint32_t lastWifiRetry = 0;

void writeLight(bool on) {
  lightRequested = on;
  digitalWrite(RELAY_PIN, (on == relayActiveHigh) ? HIGH : LOW);
  digitalWrite(LED_PIN, (on == ledActiveHigh) ? HIGH : LOW);
}
void initOutput() {
  // Put intended OFF level in the output latch before enabling the pin.
  digitalWrite(RELAY_PIN, relayActiveHigh ? LOW : HIGH);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(LED_PIN, ledActiveHigh ? LOW : HIGH);
  pinMode(LED_PIN, OUTPUT);
  writeLight(false);
}
const char *modeName() {
  switch (mode) {
    case Mode::ForceOn: return "FORCE_ON";
    case Mode::ForceOff: return "FORCE_OFF";
    default: return "AUTO";
  }
}
bool authorize() {
  if (server.authenticate("admin", SEEYA_ADMIN_PASSWORD)) return true;
  server.requestAuthentication(BASIC_AUTH, "SeeYa");
  return false;
}
void addNoCache() { server.sendHeader("Cache-Control", "no-store"); }
void status() {
  if (!authorize()) return;
  String json = "{\"version\":\"";
  json += SEEYA_VERSION;
  json += "\",\"mode\":\"";
  json += modeName();
  json += "\",\"requestedOn\":";
  json += lightRequested ? "true" : "false";
  json += ",\"relayActiveHigh\":";
  json += relayActiveHigh ? "true" : "false";
  json += ",\"wifiConnected\":";
  json += WiFi.status() == WL_CONNECTED ? "true" : "false";
  json += ",\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\"}";
  addNoCache();
  server.send(200, "application/json", json);
}
const char HOME[] PROGMEM = R"HTML(
<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SeeYa</title><style>
body{font:16px system-ui,sans-serif;max-width:650px;margin:40px auto;padding:0 16px}
button{padding:10px 16px;margin:5px;border:1px solid #aaa;border-radius:8px;cursor:pointer}
section{padding:16px;border:1px solid #ddd;border-radius:12px;margin:16px 0}
</style></head><body>
<h1>SeeYa</h1><p>Premier firmware : relais local et mise à jour Wi-Fi.</p>
<section><h2>Éclairage</h2><pre id="status">Chargement…</pre>
<button onclick="command('on')">Allumer</button>
<button onclick="command('off')">Éteindre</button>
<button onclick="command('auto')">Auto</button></section>
<section><h2>Configuration du relais</h2>
<form onsubmit="polarity(event)"><label>Polarité :
<select id="pol"><option value="high">Actif HIGH</option><option value="low">Actif LOW</option></select>
</label><button type="submit">Enregistrer</button></form></section>
<section><h2>Mise à jour OTA</h2>
<form action="/update" method="post" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<button type="submit">Installer firmware.bin</button></form>
<p>Ne couper ni l'alimentation ni le Wi-Fi durant le transfert.</p></section>
<script>
async function refresh(){
 try {const r=await fetch('/api/status');const d=await r.json();
 document.getElementById('status').textContent=JSON.stringify(d,null,2);
 document.getElementById('pol').value=d.relayActiveHigh?'high':'low';
 }catch(e){document.getElementById('status').textContent='ESP indisponible';}
}
async function command(action){await fetch('/api/light/'+action,{method:'POST'});refresh();}
async function polarity(e){
 e.preventDefault();
 const v=document.getElementById('pol').value;
 const r=await fetch('/api/relay/polarity',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'value='+v});
 if(!r.ok)alert('Erreur de configuration');
 refresh();
}
refresh();setInterval(refresh,3000);
</script></body></html>
)HTML";
void setupRoutes() {
  server.on("/", HTTP_GET, []() {
    if (!authorize()) return;
    server.send_P(200, "text/html; charset=utf-8", HOME);
  });
  server.on("/api/status", HTTP_GET, status);
  server.on("/api/light/on", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::ForceOn; writeLight(true); status();
  });
  server.on("/api/light/off", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::ForceOff; writeLight(false); status();
  });
  server.on("/api/light/auto", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::Auto; writeLight(false); status(); // Radar/schedule not implemented yet
  });
  server.on("/api/relay/polarity", HTTP_POST, []() {
    if (!authorize()) return;
    String value = server.arg("value");
    if (value != "high" && value != "low") {
      server.send(400, "text/plain", "Expected high or low");
      return;
    }
    relayActiveHigh = (value == "high");
    // Force output OFF before changing polarities; do not surprise connected load.
    mode = Mode::ForceOff;
    writeLight(false);
    preferences.putBool("relayHigh", relayActiveHigh);
    status();
  });
  server.on("/update", HTTP_POST,
    []() {
      if (!authorize()) return;
      bool success = otaStarted && otaOk && !Update.hasError();
      addNoCache();
      server.send(success ? 200 : 500, "text/plain",
                  success ? "Mise a jour terminee. Redemarrage..." : "Echec mise a jour");
      if (success) { server.client().flush(); delay(250); ESP.restart(); }
    },
    []() {
      // Check authentication before accepting or writing any uploaded bytes.
      if (!server.authenticate("admin", SEEYA_ADMIN_PASSWORD)) return;
      HTTPUpload &upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        otaStarted = true;
        otaOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (otaOk && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          otaOk = false;
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (otaOk) otaOk = Update.end(true);
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        otaOk = false;
      }
    });
  server.onNotFound([]() {
    if (!authorize()) return;
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
}
} // namespace

void setup() {
  Serial.begin(115200);
  preferences.begin("seeya", false);
  relayActiveHigh = preferences.getBool("relayHigh", true);
  initOutput();
  if (String(SEEYA_WIFI_SSID) == "CHANGE_ME_WIFI" ||
      String(SEEYA_ADMIN_PASSWORD) == "CHANGE_ME_ADMIN_PASSWORD" ||
      strlen(SEEYA_ADMIN_PASSWORD) < 12) {
    Serial.println("ERROR: configure include/secrets.h (Wi-Fi and admin password >= 12 chars)");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SEEYA_WIFI_SSID, SEEYA_WIFI_PASSWORD);
  setupRoutes();
}
void loop() {
  server.handleClient();
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry > 15000) {
    lastWifiRetry = millis();
    WiFi.reconnect();
  }
  delay(2);
}
