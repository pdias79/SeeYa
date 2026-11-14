#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include "LightOutput.h"
#include "RadarPresence.h"
#include "LD2450Presence.h"
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
LD2450Presence radar2450;
// 0 = disabled; 1 = LD2410C; 2 = LD2450
uint8_t sensorType = 0;
uint32_t offDelaySeconds = 90;
bool radarHold = false;
bool previousPresence = false;
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
  const bool is2410=sensorType==1;
  const bool is2450=sensorType==2;
  const bool health=is2410?radar.healthy():(is2450&&radar2450.healthy());
  const bool presence=is2410?radar.present():(is2450&&radar2450.present());
  json += ",\"sensorType\":\"";
  json += is2410?"ld2410c":(is2450?"ld2450":"disabled");
  json += "\",\"radarEnabled\":";
  json += sensorType ? "true" : "false";
  json += ",\"radarHealthy\":";
  json += health ? "true" : "false";
  json += ",\"presence\":";
  json += presence ? "true" : "false";
  json += ",\"radarHold\":";
  json += radarHold ? "true" : "false";
  json += ",\"offDelaySeconds\":";
  json += String(offDelaySeconds);
  json += ",\"movingDistanceCm\":";
  json += String(radar.movingDistanceCm());
  json += ",\"stationaryDistanceCm\":";
  json += String(radar.stationaryDistanceCm());
  json += ",\"radarFrames\":";
  json += String(is2410?radar.frames():radar2450.frames());
  json += ",\"targetCount\":";
  json += String(is2450?radar2450.count():0);
  const auto z=radar2450.zone();
  json += ",\"zoneEnabled\":";
  json += z.enabled ? "true":"false";
  json += ",\"zoneMinX\":" + String(z.minX);
  json += ",\"zoneMaxX\":" + String(z.maxX);
  json += ",\"zoneMinY\":" + String(z.minY);
  json += ",\"zoneMaxY\":" + String(z.maxY);
  json += ",\"targets\":[";
  for(size_t i=0;i<3;++i) {
    if(i)json+=",";
    const auto &t=radar2450.target(i);
    json += "{\"valid\":";
    json += (is2450&&radar2450.healthy()&&t.valid)?"true":"false";
    json += ",\"xMm\":" + String(t.xMm);
    json += ",\"yMm\":" + String(t.yMm);
    json += ",\"speedCmS\":" + String(t.speedCmS);
    json += ",\"insideZone\":";
    json += (is2450&&radar2450.healthy()&&radar2450.inside(t))?"true":"false";
    json += "}";
  }
  json += "]";
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
<label>Capteur <select id="sensorType">
<option value="disabled">Désactivé</option>
<option value="ld2410c">LD2410C</option>
<option value="ld2450">LD2450</option></select></label>
<label>Délai extinction (secondes) <input id="offDelay" type="number" min="1" max="86400" value="90"></label>
<button type="submit">Enregistrer</button>
</form><small>UART 256000 bauds : TX radar vers GPIO4, RX radar vers GPIO5.
La sélection d'un nouveau radar remplace l'ancien, sans modifier le câblage.</small>
</section>
<section id="zoneSection"><h2>LD2450 — Zone rectangulaire</h2>
<form onsubmit="saveZone(event)">
<label><input type="checkbox" id="zoneEnabled"> Activer le filtre de zone</label>
<p><small>Coordonnées en millimètres, origine au radar. X : latéral (±6000), Y : devant le radar (0 à 6000).</small></p>
<label>X min <input id="zoneMinX" type="number" min="-6000" max="6000" value="-1500"></label>
<label>X max <input id="zoneMaxX" type="number" min="-6000" max="6000" value="1500"></label>
<label>Y min <input id="zoneMinY" type="number" min="0" max="6000" value="0"></label>
<label>Y max <input id="zoneMaxY" type="number" min="0" max="6000" value="4000"></label>
<button type="submit">Enregistrer la zone</button>
</form><div id="targetView">Aucune cible</div>
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
 document.getElementById('sensorType').value=d.sensorType;
 document.getElementById('zoneSection').hidden=d.sensorType!=='ld2450';
 const zoneKeys=['zoneMinX','zoneMaxX','zoneMinY','zoneMaxY'];
 for(const key of zoneKeys) {
   if(document.activeElement.id!==key)document.getElementById(key).value=d[key];
 }
 document.getElementById('zoneEnabled').checked=d.zoneEnabled;
 document.getElementById('targetView').textContent=(d.targets||[])
   .filter(t=>t.valid).map(t=>'X='+t.xMm+' mm, Y='+t.yMm+
     ' mm, vitesse='+t.speedCmS+' cm/s, retenue='+t.insideZone).join(' | ')||'Aucune cible';
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
 d.set('type',document.getElementById('sensorType').value);
 d.set('offDelay',document.getElementById('offDelay').value);
 const r=await fetch('/api/radar',{method:'POST',body:d});
 if(!r.ok)alert(await r.text());
 refresh();
}
async function saveZone(e) {
 e.preventDefault();
 const d=new URLSearchParams();
 d.set('enabled',document.getElementById('zoneEnabled').checked?'true':'false');
 for(const key of ['zoneMinX','zoneMaxX','zoneMinY','zoneMaxY'])
   d.set(key,document.getElementById(key).value);
 const r=await fetch('/api/radar/zone',{method:'POST',body:d});
 if(!r.ok)alert('Zone non enregistrée : '+await r.text());
 await refresh();
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
    const String newType=server.arg("type");
    uint8_t next=0;
    if(newType=="ld2410c")next=1;
    else if(newType=="ld2450")next=2;
    else if(newType!="disabled"){server.send(400,"text/plain","Invalid radar");return;}
    offDelaySeconds=value;
    prefs.putUInt("offDelay",offDelaySeconds);
    if(sensorType!=next) {
      if(sensorType)radarUart.end();
      sensorType=next;
      if(sensorType==1)radar.begin(radarUart,4,5);
      else if(sensorType==2)radar2450.begin(radarUart,4,5);
      radarHold=false;
      previousPresence=false;
    }
    prefs.putUChar("sensorType",sensorType);
    prefs.remove("radarOn");
    if(mode==Mode::Auto) output.request(false);
    status();
  });
  server.on("/api/radar/zone", HTTP_POST, [](){
    if(!authorize())return;
    LD2450Presence::Zone z;
    z.enabled=server.arg("enabled")=="true";
    // Validate before integer conversion (toInt accepts partial strings).
    int values[4]={};
    const char *names[4]={"zoneMinX","zoneMaxX","zoneMinY","zoneMaxY"};
    for(int i=0;i<4;++i){
      String raw=server.arg(names[i]);
      if(raw.isEmpty()||raw.length()>6){server.send(400,"text/plain","Bad coordinate");return;}
      int start=raw[0]=='-'?1:0;
      if(start==int(raw.length())){server.send(400,"text/plain","Bad coordinate");return;}
      for(int j=start;j<int(raw.length());++j)if(!isDigit(raw[j])){
        server.send(400,"text/plain","Bad coordinate");return;
      }
      values[i]=raw.toInt();
      if(values[i]<-6000||values[i]>6000){server.send(400,"text/plain","Outside bounds");return;}
    }
    z.minX=values[0]; z.maxX=values[1];
    z.minY=values[2]; z.maxY=values[3];
    if(!LD2450Presence::validZone(z)){server.send(400,"text/plain","Invalid rectangle");return;}
    radar2450.setZone(z);
    prefs.putBool("zEnable",z.enabled);
    prefs.putShort("zMinX",z.minX); prefs.putShort("zMaxX",z.maxX);
    prefs.putShort("zMinY",z.minY); prefs.putShort("zMaxY",z.maxY);
    server.send(200,"application/json","{\"saved\":true}");
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
  sensorType=prefs.getUChar("sensorType",prefs.getBool("radarOn",false)?1:0);
  if(sensorType>2)sensorType=0;
  LD2450Presence::Zone z;
  z.enabled=prefs.getBool("zEnable",false);
  z.minX=prefs.getShort("zMinX",-1500);
  z.maxX=prefs.getShort("zMaxX",1500);
  z.minY=prefs.getShort("zMinY",0);
  z.maxY=prefs.getShort("zMaxY",4000);
  if(LD2450Presence::validZone(z))radar2450.setZone(z);
  if(sensorType==1)radar.begin(radarUart,4,5);
  else if(sensorType==2)radar2450.begin(radarUart,4,5);
  if (String(SEEYA_WIFI_SSID) == "CHANGE_ME_WIFI" ||
      String(SEEYA_ADMIN_PASSWORD) == "CHANGE_ME_ADMIN_PASSWORD") {
    Serial.println("ERROR: set WiFi and unique admin password in include/secrets.h");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SEEYA_WIFI_SSID, SEEYA_WIFI_PASSWORD);
  setupRoutes();
}
void loop() {
  if(sensorType) {
    if(sensorType==1)radar.update();
    else radar2450.update();
    uint32_t now=millis();
    const bool presence=sensorType==1?radar.present():radar2450.present();
    if(presence) {
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
