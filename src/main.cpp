#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include "LightOutput.h"
#include "RadarPresence.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#ifndef SEEYA_VERSION
#define SEEYA_VERSION "dev"
#endif

namespace {
WebServer server(80);
Preferences prefs;
LightOutput output;
HardwareSerial radarUart(1);
RadarPresence radar;
bool radarEnabled = false;
uint32_t offDelaySeconds = 90;
bool radarHold = false;
uint32_t lastPresenceMs = 0;
enum class Mode { Auto, ForceOn, ForceOff };
Mode mode = Mode::Auto;
bool otaOk = false;
bool otaStarted = false;
unsigned long lastWifiRetry = 0;

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
void jsonHeader() { server.sendHeader("Cache-Control", "no-store"); }
void status() {
  if (!authorize()) return;
  const LightOutput::Config &c = output.config();
  String json;
  json.reserve(320);
  json = "{\"version\":\"";
  json += SEEYA_VERSION;
  json += "\",\"mode\":\"";
  json += modeName();
  json += "\",\"requestedOn\":";
  json += output.requested() ? "true" : "false";
  json += ",\"radarEnabled\":";
  json += radarEnabled ? "true" : "false";
  json += ",\"radarHealthy\":";
  json += radarEnabled && radar.healthy() ? "true" : "false";
  json += ",\"presence\":";
  json += radarEnabled && radar.present() ? "true" : "false";
  json += ",\"radarHold\":";
  json += radarHold ? "true" : "false";
  json += ",\"offDelaySeconds\":";
  json += String(offDelaySeconds);
  json += ",\"movingDistanceCm\":";
  json += String(radar.movingDistanceCm());
  json += ",\"stationaryDistanceCm\":";
  json += String(radar.stationaryDistanceCm());
  json += ",\"radarFrames\":";
  json += String(radar.frames());
  json += ",\"outputType\":\"";
  json += LightOutput::typeName(c.type);
  json += "\",\"outputResult\":\"";
  json += LightOutput::resultName(output.result());
  json += "\",\"pending\":";
  json += output.pending() ? "true" : "false";
  json += ",\"gpio\":";
  json += String(c.gpio);
  json += ",\"activeHigh\":";
  json += c.activeHigh ? "true" : "false";
  json += ",\"ledEnabled\":";
  json += c.ledEnabled ? "true" : "false";
  json += ",\"ledActiveHigh\":";
  json += c.ledActiveHigh ? "true" : "false";
  json += ",\"httpMethod\":\"";
  json += c.httpMethod;
  json += "\",\"wifiConnected\":";
  json += WiFi.status() == WL_CONNECTED ? "true" : "false";
  json += ",\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\"}";
  jsonHeader();
  server.send(200, "application/json", json);
}
const char HOME[] PROGMEM = R"HTML(
<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SeeYa</title><style>
body{font:16px system-ui,sans-serif;max-width:680px;margin:30px auto;padding:0 16px}
button,input,select{font:inherit;padding:8px;margin:5px 0}
button{border:1px solid #aaa;border-radius:6px;cursor:pointer;margin-right:8px}
section{padding:16px;border:1px solid #ddd;border-radius:10px;margin:16px 0}
label{display:block;margin-top:12px}input[type=url]{width:95%}
small{color:#666}pre{white-space:pre-wrap;word-break:break-word}
</style></head><body><h1>SeeYa</h1>
<p>ESP32-C3 — firmware <span id="firmwareVersion">…</span></p>
<section><h2>Éclairage</h2><pre id="status">Chargement…</pre>
<button type="button" id="lightOn">Allumer</button>
<button type="button" id="lightOff">Éteindre</button>
<button type="button" id="lightAuto">Auto</button>
<p id="lightMessage" role="status" aria-live="polite"></p></section>
<section><h2>Sortie d'éclairage</h2>
<form onsubmit="saveOutput(event)">
<label>Mode de sortie <select id="type">
<option value="disabled">Désactivée</option>
<option value="gpio">Relais GPIO</option>
<option value="http">Prise distante HTTP</option></select></label>
<label>GPIO relais <select id="gpio">
<option value="3">3</option><option value="6">6</option>
<option value="7">7</option><option value="10">10</option></select></label>
<label>Polarité relais <select id="activeHigh">
<option value="false">Actif LOW</option>
<option value="true">Actif HIGH</option></select></label>
<label><input id="ledEnabled" type="checkbox" checked> LED d'état GPIO8 activée</label>
<label>Polarité LED <select id="ledActiveHigh">
<option value="false">Actif LOW</option><option value="true">Actif HIGH</option></select></label>
<label>URL ON <input id="urlOn" type="url" placeholder="http://192.168.1.50/on"></label>
<label>URL OFF <input id="urlOff" type="url" placeholder="http://192.168.1.50/off"></label>
<label>Méthode HTTP <select id="httpMethod">
<option value="POST">POST</option><option value="GET">GET</option></select></label>
<button type="submit">Enregistrer la configuration</button>
<p><small>Lors du changement de configuration, la sortie locale est remise à OFF.
Le protocole HTTP est limité à des URL http:// dans cette version expérimentale.</small></p>
</form></section>
<section><h2>LD2410C</h2>
<form onsubmit="saveRadar(event)">
<label><input type="checkbox" id="radarEnabled"> Activer le radar LD2410C</label>
<label>Délai extinction (secondes) <input id="offDelay" type="number" min="1" max="86400" value="90"></label>
<button type="submit">Enregistrer</button>
</form><small>UART 256000 bauds : TX radar vers GPIO4, RX radar vers GPIO5. Mode LD2450 non encore disponible.</small>
</section>
<section><h2>Firmware OTA</h2>
<form action="/update" method="post" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<button type="submit">Installer firmware.bin</button></form>
<p><small>Ne pas couper l'alimentation durant le transfert.</small></p></section>
<script>
async function refresh(){
 try {
  const r=await fetch('/api/status');const d=await r.json();
  document.getElementById('status').textContent=JSON.stringify(d,null,2);
  document.getElementById('firmwareVersion').textContent=d.version;
 document.getElementById('radarEnabled').checked=!!d.radarEnabled;
 if(document.activeElement.id!=='offDelay')document.getElementById('offDelay').value=d.offDelaySeconds;
 }catch(e){document.getElementById('status').textContent='ESP indisponible';}
}
async function sendLightCommand(action) {
 const message=document.getElementById('lightMessage');
 message.textContent='Envoi de la commande '+action+'…';
 try {
  const response=await fetch('/api/light/'+action,{method:'POST',cache:'no-store'});
  if(!response.ok)throw new Error('HTTP '+response.status+' : '+await response.text());
  const result=await response.json();
  message.textContent='Commande acceptée — '+result.mode;
  await refresh();
 } catch(error) {
  message.textContent='Commande impossible : '+error.message;
  console.error('SeeYa light command failed',error);
 }
}
document.getElementById('lightOn').addEventListener('click',()=>sendLightCommand('on'));
document.getElementById('lightOff').addEventListener('click',()=>sendLightCommand('off'));
document.getElementById('lightAuto').addEventListener('click',()=>sendLightCommand('auto'));
async function loadConfig(){
 const r=await fetch('/api/output');const d=await r.json();
 for(const key of ['type','gpio','activeHigh','ledActiveHigh','urlOn','urlOff','httpMethod']){
  if(d[key]!==undefined)document.getElementById(key).value=String(d[key]);
 }
 document.getElementById('ledEnabled').checked=!!d.ledEnabled;
}
async function saveRadar(e) {
 e.preventDefault();
 const d=new URLSearchParams();
 d.set('enabled',document.getElementById('radarEnabled').checked?'true':'false');
 d.set('offDelay',document.getElementById('offDelay').value);
 const r=await fetch('/api/radar',{method:'POST',body:d});
 if(!r.ok)alert(await r.text());
 refresh();
}
async function saveOutput(e){
 e.preventDefault();
 const keys=['type','gpio','activeHigh','ledActiveHigh','urlOn','urlOff','httpMethod'];
 const data=new URLSearchParams();
 for(const k of keys)data.set(k,document.getElementById(k).value);
 data.set('ledEnabled',document.getElementById('ledEnabled').checked?'true':'false');
 const r=await fetch('/api/output',{method:'POST',body:data});
 if(!r.ok)alert('Erreur : '+await r.text());
 await loadConfig();await refresh();
}
loadConfig();refresh();setInterval(refresh,3000);
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
    mode = Mode::ForceOn; output.request(true); status();
  });
  server.on("/api/light/off", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::ForceOff; output.request(false); status();
  });
  server.on("/api/light/auto", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::Auto; output.request(radarHold); status(); // schedules pending
  });
  server.on("/api/radar", HTTP_POST, []() {
    if (!authorize()) return;
    String seconds = server.arg("offDelay");
    if (seconds.isEmpty() || seconds.length() > 5) {
      server.send(400, "text/plain", "Invalid delay"); return;
    }
    for (size_t i=0;i<seconds.length();++i) {
      if (!isDigit(seconds[i])) { server.send(400,"text/plain","Expected integer");return; }
    }
    uint32_t value=seconds.toInt();
    if(value<1 || value>86400) {
      server.send(400,"text/plain","Delay out of range");return;
    }
    bool enable=server.arg("enabled")=="true";
    offDelaySeconds=value;
    prefs.putUInt("offDelay",offDelaySeconds);
    prefs.putBool("radarOn",enable);
    if(enable && !radarEnabled) radar.begin(radarUart,4,5);
    if(!enable && radarEnabled) radarUart.end();
    radarEnabled=enable;
    radarHold=false;
    if(mode==Mode::Auto) output.request(false);
    status();
  });
  server.on("/api/output", HTTP_GET, []() {
    if (!authorize()) return;
    const auto &c = output.config();
    // Configuration excluding HTTP URLs: they can contain tokens in query strings.
    String json = "{\"type\":\"";
    json += LightOutput::typeName(c.type);
    json += "\",\"gpio\":" + String(c.gpio);
    json += ",\"activeHigh\":" + String(c.activeHigh ? "true" : "false");
    json += ",\"ledEnabled\":" + String(c.ledEnabled ? "true" : "false");
    json += ",\"ledActiveHigh\":" + String(c.ledActiveHigh ? "true" : "false");
    json += ",\"httpMethod\":\"" + c.httpMethod + "\"";
    json += ",\"hasUrlOn\":" + String(c.urlOn.length() ? "true" : "false");
    json += ",\"hasUrlOff\":" + String(c.urlOff.length() ? "true" : "false") + "}";
    jsonHeader(); server.send(200, "application/json", json);
  });
  server.on("/api/output", HTTP_POST, []() {
    if (!authorize()) return;
    LightOutput::Config c = output.config();
    String type = server.arg("type");
    if (type == "gpio") c.type = LightOutput::Type::Gpio;
    else if (type == "http") c.type = LightOutput::Type::Http;
    else if (type == "disabled") c.type = LightOutput::Type::Disabled;
    else { server.send(400, "text/plain", "Invalid output type"); return; }
    int pin = server.arg("gpio").toInt();
    if (pin < 0 || pin > 255) { server.send(400, "text/plain", "Invalid pin"); return; }
    c.gpio = static_cast<uint8_t>(pin);
    c.activeHigh = server.arg("activeHigh") == "true";
    c.ledEnabled = server.arg("ledEnabled") == "true";
    c.ledActiveHigh = server.arg("ledActiveHigh") == "true";
    String method = server.arg("httpMethod");
    if (method != "GET" && method != "POST") {
      server.send(400, "text/plain", "Invalid HTTP method"); return;
    }
    c.httpMethod = method;
    // Blank fields mean "keep existing URL"; explicit clearing requires a future command.
    if (server.arg("urlOn").length()) c.urlOn = server.arg("urlOn");
    if (server.arg("urlOff").length()) c.urlOff = server.arg("urlOff");
    String error;
    if (!output.configure(c, error)) {
      server.send(400, "text/plain", error); return;
    }
    mode = Mode::ForceOff;
    server.send(200, "application/json", "{\"saved\":true,\"requestedOn\":false}");
  });
  server.on("/update", HTTP_POST,
    []() {
      if (!authorize()) return;
      const bool success = otaStarted && otaOk && !Update.hasError();
      jsonHeader();
      server.send(success ? 200 : 500, "text/plain",
        success ? "Mise a jour installee. Redemarrage." : "Echec mise a jour");
      if (success) { server.client().flush(); delay(250); ESP.restart(); }
      otaStarted = false;
    }, []() {
      if (!server.authenticate("admin", SEEYA_ADMIN_PASSWORD)) return;
      HTTPUpload &upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        otaStarted = true;
        otaOk = Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (otaOk && Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          otaOk = false;
      } else if (upload.status == UPLOAD_FILE_END) {
        if (otaOk) otaOk = Update.end(true);
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); otaOk = false;
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
  prefs.begin("seeya", false);
  output.begin(prefs);
  offDelaySeconds=prefs.getUInt("offDelay",90);
  if(offDelaySeconds<1 || offDelaySeconds>86400)offDelaySeconds=90;
  radarEnabled=prefs.getBool("radarOn",false);
  if(radarEnabled)radar.begin(radarUart,4,5);
  if (String(SEEYA_WIFI_SSID) == "CHANGE_ME_WIFI" ||
      String(SEEYA_ADMIN_PASSWORD) == "CHANGE_ME_ADMIN_PASSWORD" ||
      strlen(SEEYA_ADMIN_PASSWORD) < 12) {
    Serial.println("ERROR: set WiFi and unique admin password in include/secrets.h");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SEEYA_WIFI_SSID, SEEYA_WIFI_PASSWORD);
  setupRoutes();
}
void loop() {
  if(radarEnabled) {
    radar.update();
    uint32_t now=millis();
    if(radar.present()) {
      lastPresenceMs=now;
      radarHold=true;
    } else if(radarHold && (uint32_t)(now-lastPresenceMs)>=offDelaySeconds*1000UL) {
      radarHold=false;
    }
    // The holding timer still expires after loss of radar communication.
  } else radarHold=false;
  if(mode==Mode::Auto)output.request(radarHold);
  server.handleClient();
  output.update();
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry > 15000) {
    lastWifiRetry = millis();
    WiFi.reconnect();
  }
  delay(2);
}
