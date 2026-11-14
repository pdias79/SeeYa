#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Preferences.h>
#include "LightOutput.h"
#include "RadarPresence.h"
#include "LD2450Presence.h"
#include "WeeklySchedule.h"
#include "WifiFallback.h"
#include <time.h>
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
WeeklySchedule schedule;
WifiFallback wifiFallback;
// 0 = disabled; 1 = LD2410C; 2 = LD2450
uint8_t sensorType = 0;
uint32_t onDelaySeconds = 15;
uint32_t offDelayMinutes = 15;
uint32_t offDelaySeconds = 900;
bool radarHold = false;
bool previousPresence = false;
uint32_t presenceSinceMs = 0;
uint32_t absenceSinceMs = 0;
bool scheduleOn = false;
bool clockReady = false;
char localTimeText[32] = "not synchronized";
uint32_t lastPresenceMs = 0;
enum class Mode { Auto, ForceOn, ForceOff };
Mode mode = Mode::Auto;
bool autoManualActive = false;
bool autoManualOn = false;
bool autoSnapshotRadar = false;
bool autoSnapshotSchedule = false;
// At boot the lighting must remain OFF until automation changes state
// (or the user explicitly selects a command).
bool bootOffPending = true;
bool otaOk = false;
bool otaStarted = false;


uint32_t lastRadarLogMs = 0;
void logRadarStatus() {
  if(!sensorType) return;
  const uint32_t now=millis();
  if((uint32_t)(now-lastRadarLogMs)<2000) return;
  lastRadarLogMs=now;
  if(sensorType==1) {
    Serial.printf("[LD2410C] t=%lu ms UART=%lu bytes frames=%lu link=%s state=%u presence=%s moving=%u cm (E=%u) stationary=%u cm (E=%u)\n",
      (unsigned long)now, (unsigned long)radar.bytesReceived(),
      (unsigned long)radar.frames(), radar.healthy()?"OK":"NO_VALID_FRAME",
      (unsigned)radar.targetState(), radar.present()?"YES":"NO",
      (unsigned)radar.movingDistanceCm(),(unsigned)radar.movingEnergy(),
      (unsigned)radar.stationaryDistanceCm(),(unsigned)radar.stationaryEnergy());
  } else if(sensorType==2) {
    Serial.printf("[LD2450] t=%lu ms UART=%lu bytes frames=%lu link=%s targets=%u presence=%s zone=%s\n",
      (unsigned long)now, (unsigned long)radar2450.bytesReceived(),
      (unsigned long)radar2450.frames(), radar2450.healthy()?"OK":"NO_VALID_FRAME",
      (unsigned)radar2450.count(),radar2450.present()?"YES":"NO",
      radar2450.zone().enabled?"ON":"OFF");
    if(radar2450.healthy()) {
      for(size_t i=0;i<3;++i) {
        const auto &t=radar2450.target(i);
        if(!t.valid) continue;
        Serial.printf("  target%u X=%d mm Y=%d mm speed=%d cm/s resolution=%u mm in_zone=%s\n",
          (unsigned)(i+1),(int)t.xMm,(int)t.yMm,(int)t.speedCmS,
          (unsigned)t.resolutionMm,radar2450.inside(t)?"YES":"NO");
      }
    }
  }
}

void updateClockAndSchedule() {
  const time_t now=time(nullptr);
  clockReady = now >= 1735689600; // refuse bogus pre-2025 clock values
  scheduleOn=false;
  if(!clockReady) {
    snprintf(localTimeText,sizeof(localTimeText),"not synchronized");
    return;
  }
  struct tm local;
  if(localtime_r(&now,&local)==nullptr) {
    clockReady=false;
    snprintf(localTimeText,sizeof(localTimeText),"not synchronized");
    return;
  }
  strftime(localTimeText,sizeof(localTimeText),"%Y-%m-%d %H:%M:%S",&local);
  scheduleOn=schedule.requestedOn(local);
}
bool autoLightRequested() { return radarHold || scheduleOn; }
void applyAutoLight() {
  if(mode!=Mode::Auto)return;
  if(bootOffPending) {
    bootOffPending=false;
    autoManualActive=true;
    autoManualOn=false;
    autoSnapshotRadar=radarHold;
    autoSnapshotSchedule=scheduleOn;
  }
  if(autoManualActive &&
     (radarHold!=autoSnapshotRadar || scheduleOn!=autoSnapshotSchedule))
    autoManualActive=false;
  output.request(autoManualActive?autoManualOn:autoLightRequested());
}
void resetAutoManual() { bootOffPending=false; autoManualActive=false; }
void setAutoManual(bool on) {
  bootOffPending=false;
  autoManualActive=true;
  autoManualOn=on;
  autoSnapshotRadar=radarHold;
  autoSnapshotSchedule=scheduleOn;
  output.request(on);
}
const char *modeName() {
  switch (mode) {
    case Mode::ForceOn: return "FORCE_ON";
    case Mode::ForceOff: return "FORCE_OFF";
    default: return "AUTO";
  }
}
bool portalClient() {
  // Setup without HTTP Basic only from the temporary AP subnet.
  return wifiFallback.accessPointActive() &&
    (server.client().remoteIP()[0]==192 &&
     server.client().remoteIP()[1]==168 &&
     server.client().remoteIP()[2]==4);
}
void redirectToSetup() {
  server.sendHeader("Location","http://192.168.4.1/setup",true);
  server.sendHeader("Cache-Control","no-store");
  server.send(302,"text/html; charset=utf-8",
    "<!doctype html><html><head><meta http-equiv='refresh' "
    "content='0;url=http://192.168.4.1/setup'></head><body>"
    "<a href='http://192.168.4.1/setup'>Configurer SeeYa</a></body></html>");
}
// An empty administrator password intentionally enables unrestricted HTTP
// access. Keep this check centralized so OTA uploads follow the same policy.
bool adminAuthenticationRequired() {
  return SEEYA_ADMIN_PASSWORD[0] != '\0';
}
bool authorize() {
  if (!adminAuthenticationRequired()) return true;
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
  json += ",\"autoManualActive\":";
  json += autoManualActive ? "true" : "false";
  json += ",\"autoManualOn\":";
  json += autoManualOn ? "true" : "false";
  json += ",\"clockReady\":";
  json += clockReady ? "true":"false";
  json += ",\"localTime\":\"";
  json += localTimeText;
  json += "\",\"scheduleOn\":";
  json += scheduleOn ? "true":"false";
  json += ",\"wifiFallback\":";
  json += wifiFallback.accessPointActive()?"true":"false";
  json += ",\"fallbackSsid\":\"";
  json += wifiFallback.apName();
  json += "\"";
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
  json += ",\"onDelaySeconds\":";
  json += String(onDelaySeconds);
  json += ",\"offDelayMinutes\":";
  json += String(offDelayMinutes);
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
#radarMap{display:block;width:100%;height:auto;border:1px solid #ccd4dc;border-radius:8px;background:#f8fafc;touch-action:pan-y}
#radarMap.draw{cursor:crosshair;touch-action:none}
#radarInfo{font-size:14px;min-height:1.4em}
</style></head><body><h1>SeeYa</h1>
<p>ESP32-C3 — firmware <span id="firmwareVersion">…</span></p>
<section><h2>Éclairage</h2><pre id="status">Chargement…</pre>
<button type="button" id="lightForceOn">Forcer Allumé</button>
<button type="button" id="lightForceOff">Forcer Éteindre</button>
<button type="button" id="lightAuto">Auto</button>
<p><small>Commandes ponctuelles uniquement en AUTO. L'automatisme reprend au prochain changement du radar ou des horaires.</small></p>
<button type="button" id="lightOn">Allumer</button>
<button type="button" id="lightOff">Éteindre</button>
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
<section><h2>Détection radar</h2>
<form onsubmit="saveRadar(event)">
<label>Capteur <select id="sensorType">
<option value="disabled">Désactivé</option>
<option value="ld2410c">LD2410C</option>
<option value="ld2450">LD2450</option></select></label>
<label>Présence continue avant allumage (secondes) <input id="onDelay" type="number" min="0" max="3600" value="15"></label>
<label>Absence avant extinction (minutes) <input id="offDelay" type="number" min="1" max="1440" value="15"></label>
<p><small>En AUTO : une présence doit durer sans interruption jusqu'au délai d'allumage. Une nouvelle présence pendant l'attente d'extinction annule cette attente.</small></p>
<button type="submit">Enregistrer</button>
</form><small>UART 256000 bauds : TX radar vers GPIO4, RX radar vers GPIO5.
La sélection d'un nouveau radar remplace l'ancien, sans modifier le câblage.</small>
</section>
<section id="zoneSection"><h2>LD2450 — Position des cibles</h2>
<p><small>Vue de dessus : radar en bas au centre, X horizontal (−6 à +6 m), Y vers l'avant (0 à 6 m).
Les cibles sont actualisées automatiquement. Vert = dans la zone, orange = hors zone.</small></p>
<canvas id="radarMap" width="720" height="390" aria-label="Plan radar LD2450 avec cibles et zone"></canvas>
<p id="radarInfo" role="status">En attente du radar…</p>
<label><input id="zoneDraw" type="checkbox"> Dessiner la zone sur le plan (glisser du premier coin au second)</label>
<p><small>La sélection graphique modifie les coordonnées ci-dessous. Cliquer sur Enregistrer la zone pour les appliquer au radar.</small></p>
<h3>Zone rectangulaire</h3>
<form onsubmit="saveZone(event)">
<label><input type="checkbox" id="zoneEnabled"> Activer le filtre de zone</label>
<p><small>Coordonnées en millimètres, origine au radar. X : latéral (±6000), Y : devant le radar (0 à 6000).</small></p>
<label>X min <input id="zoneMinX" type="number" min="-6000" max="6000" value="-1500"></label>
<label>X max <input id="zoneMaxX" type="number" min="-6000" max="6000" value="1500"></label>
<label>Y min <input id="zoneMinY" type="number" min="0" max="6000" value="0"></label>
<label>Y max <input id="zoneMaxY" type="number" min="0" max="6000" value="4000"></label>
<button type="submit">Enregistrer la zone</button>
</form><p id="zoneMessage" role="status"></p>
</section>
<section><h2>Programmation hebdomadaire — 10 événements</h2>
<p><small>Chaque événement définit ON, OFF ou Désactivé, une heure et des jours (L M M J V S D). Le dernier événement applicable détermine l'état du programme. Le radar peut toujours allumer en AUTO, même après un événement OFF.</small></p>
<p>Horloge : <strong id="clockText">Synchronisation...</strong> — Programme : <strong id="scheduleText">inactif</strong></p>
<form id="scheduleForm"><div id="scheduleEvents"></div>
<button type="submit">Enregistrer les 10 événements</button></form>
<p id="scheduleMessage" role="status" aria-live="polite"></p>
</section>
<section><h2>Configuration Wi-Fi</h2>
<p><small>Si le réseau domestique est indisponible, SeeYa crée son réseau Wi-Fi de secours. Portail captif à http://192.168.4.1.</small></p>
<form id="wifiForm">
<label>SSID du réseau domestique <input id="wifiSsid" maxlength="32" required></label>
<label>Mot de passe du réseau domestique <input id="wifiPass" type="password" maxlength="63" placeholder="Laisser vide pour conserver l'actuel"></label>
<label>Nom du Wi-Fi de secours <input id="fallbackSsid" maxlength="32" required></label>
<button type="submit">Enregistrer le Wi-Fi</button></form>
<p id="wifiMessage" role="status" aria-live="polite"></p>
</section>
<section><h2>Firmware OTA</h2>
<form action="/update" method="post" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<button type="submit">Installer firmware.bin</button></form>
<p><small>Ne pas couper l'alimentation durant le transfert.</small></p></section>
<script>
const weekdays=['L','M','M','J','V','S','D'];
const scheduleEvents=document.getElementById('scheduleEvents');
scheduleEvents.innerHTML=Array.from({length:10},(_,i)=>
 '<fieldset><legend>Événement '+(i+1)+'</legend>'+
 '<label>Action <select id="evAction'+i+'"><option value="0">Désactivé</option>'+
 '<option value="1">Allumer</option><option value="2">Éteindre</option></select></label>'+
 '<label>Heure <input type="time" id="evTime'+i+'" value="18:00"></label>'+
 '<div>Jours : '+weekdays.map((day,j)=>
 '<label style="display:inline-block;margin-right:10px"><input type="checkbox" id="evDay'+i+'_'+j+'"> '+day+'</label>').join('')+'</div></fieldset>').join('');
function minutes(t) {
 if(!/^([01][0-9]|2[0-3]):[0-5][0-9]$/.test(t))throw Error('Heure incorrecte : '+t);
 return Number(t.slice(0,2))*60+Number(t.slice(3));
}
function hhmm(min){return String(Math.floor(min/60)).padStart(2,'0')+':'+String(min%60).padStart(2,'0');}
async function loadSchedule(){
 const r=await fetch('/api/schedule',{cache:'no-store'});
 if(!r.ok)throw Error('HTTP '+r.status);
 const d=await r.json();
 for(let i=0;i<10;++i){
  const ev=d.events[i];
  document.getElementById('evAction'+i).value=ev.action;
  document.getElementById('evTime'+i).value=hhmm(ev.minute);
  for(let j=0;j<7;++j)
   document.getElementById('evDay'+i+'_'+j).checked=!!(ev.days&(1<<j));
 }
}
document.getElementById('scheduleForm').addEventListener('submit',async e=>{
 e.preventDefault();const message=document.getElementById('scheduleMessage');
 try{
  const data=new URLSearchParams();
  for(let i=0;i<10;++i){
   let days=0;
   for(let j=0;j<7;++j)if(document.getElementById('evDay'+i+'_'+j).checked)days|=1<<j;
   data.set('a'+i,document.getElementById('evAction'+i).value);
   data.set('m'+i,String(minutes(document.getElementById('evTime'+i).value)));
   data.set('d'+i,String(days));
  }
  const r=await fetch('/api/schedule',{method:'POST',body:data});
  if(!r.ok)throw Error('HTTP '+r.status+' '+await r.text());
  message.textContent='Événements enregistrés';await refresh();
 }catch(error){message.textContent='Erreur : '+error.message;}
});
async function loadWifi(){
 const r=await fetch('/api/wifi',{cache:'no-store'});
 if(!r.ok)throw Error('HTTP '+r.status);
 const d=await r.json();
 document.getElementById('wifiSsid').value=d.ssid;
 document.getElementById('fallbackSsid').value=d.fallbackSsid;
}
document.getElementById('wifiForm').addEventListener('submit',async e=>{
 e.preventDefault();
 const data=new URLSearchParams();
 data.set('ssid',document.getElementById('wifiSsid').value);
 data.set('password',document.getElementById('wifiPass').value);
 data.set('keepPassword',document.getElementById('wifiPass').value===''?'true':'false');
 data.set('fallbackSsid',document.getElementById('fallbackSsid').value);
 const message=document.getElementById('wifiMessage');
 try {
  const r=await fetch('/api/wifi',{method:'POST',body:data});
  if(!r.ok)throw Error('HTTP '+r.status+' '+await r.text());
  message.textContent='Configuration enregistrée. Reconnexion Wi-Fi en cours.';
  document.getElementById('wifiPass').value='';
 }catch(error){message.textContent='Erreur : '+error.message;}
});
loadSchedule().catch(e=>document.getElementById('scheduleMessage').textContent=e.message);
loadWifi().catch(e=>document.getElementById('wifiMessage').textContent=e.message);
let radarFormDirty=false;
document.getElementById('sensorType').addEventListener('change',()=>{radarFormDirty=true;});
document.getElementById('onDelay').addEventListener('input',()=>{radarFormDirty=true;});
document.getElementById('offDelay').addEventListener('input',()=>{radarFormDirty=true;});
// LD2450 top-view: X = [-6000,6000] mm, Y = [0,6000] mm.
// Never treat the LD2410C distance-only measurements as X/Y coordinates.
const radarCanvas=document.getElementById('radarMap');
const radarCtx=radarCanvas.getContext('2d');
const plot={left:54,right:702,top:24,bottom:349};
const zoneFields=['zoneMinX','zoneMaxX','zoneMinY','zoneMaxY'];
let zoneDirty=false,radarLive=null,dragStart=null,dragEnd=null,radarPolling=false;
const clamp=(v,lo,hi)=>Math.max(lo,Math.min(hi,v));
const xPixel=x=>plot.left+(x+6000)/12000*(plot.right-plot.left);
const yPixel=y=>plot.bottom-y/6000*(plot.bottom-plot.top);
function canvasPoint(e){
 const rect=radarCanvas.getBoundingClientRect();
 const px=(e.clientX-rect.left)*radarCanvas.width/rect.width;
 const py=(e.clientY-rect.top)*radarCanvas.height/rect.height;
 return {
  x:clamp(Math.round(((px-plot.left)/(plot.right-plot.left)*12000-6000)/100)*100,-6000,6000),
  y:clamp(Math.round(((plot.bottom-py)/(plot.bottom-plot.top)*6000)/100)*100,0,6000)
 };
}
function readZone(){
 return Object.fromEntries(zoneFields.map(k=>[k,Number(document.getElementById(k).value)]));
}
function drawRadar(){
 const c=radarCtx;
 c.clearRect(0,0,720,390);
 c.fillStyle='#f8fafc';c.fillRect(0,0,720,390);
 c.font='12px system-ui';c.lineWidth=1;
 for(let x=-6000;x<=6000;x+=2000){
  const px=xPixel(x);
  c.strokeStyle=x===0?'#8ca0b3':'#d9e1e9';
  c.beginPath();c.moveTo(px,plot.top);c.lineTo(px,plot.bottom);c.stroke();
  c.fillStyle='#46576a';c.textAlign='center';c.fillText(String(x/1000),px,plot.bottom+17);
 }
 for(let y=0;y<=6000;y+=1000){
  const py=yPixel(y);
  c.strokeStyle='#d9e1e9';c.beginPath();c.moveTo(plot.left,py);c.lineTo(plot.right,py);c.stroke();
  c.fillStyle='#46576a';c.textAlign='right';c.fillText(String(y/1000),plot.left-8,py+4);
 }
 c.textAlign='left';c.fillStyle='#334155';c.fillText('Y (m)',8,15);
 c.textAlign='right';c.fillText('X (m)',plot.right,383);
 c.strokeStyle='#94a3b8';c.strokeRect(plot.left,plot.top,plot.right-plot.left,plot.bottom-plot.top);
 const z=readZone(),valid=zoneFields.every(k=>Number.isFinite(z[k]))&&
   z.zoneMinX<z.zoneMaxX&&z.zoneMinY<z.zoneMaxY;
 if(valid) {
  const left=xPixel(clamp(z.zoneMinX,-6000,6000));
  const top=yPixel(clamp(z.zoneMaxY,0,6000));
  const width=xPixel(clamp(z.zoneMaxX,-6000,6000))-left;
  const height=yPixel(clamp(z.zoneMinY,0,6000))-top;
  c.fillStyle=document.getElementById('zoneEnabled').checked?'rgba(34,197,94,.10)':'rgba(100,116,139,.06)';
  c.strokeStyle=document.getElementById('zoneEnabled').checked?'#16a34a':'#64748b';
  c.lineWidth=2;c.setLineDash(document.getElementById('zoneEnabled').checked?[]:[7,5]);
  c.fillRect(left,top,width,height);c.strokeRect(left,top,width,height);
  c.setLineDash([]);
 }
 if(dragStart&&dragEnd){
  const x1=xPixel(Math.min(dragStart.x,dragEnd.x)),x2=xPixel(Math.max(dragStart.x,dragEnd.x));
  const y1=yPixel(Math.max(dragStart.y,dragEnd.y)),y2=yPixel(Math.min(dragStart.y,dragEnd.y));
  c.fillStyle='rgba(37,99,235,.10)';c.fillRect(x1,y1,x2-x1,y2-y1);
  c.strokeStyle='#2563eb';c.setLineDash([4,4]);c.strokeRect(x1,y1,x2-x1,y2-y1);c.setLineDash([]);
 }
 // Physical radar location, at x=0/y=0.
 c.fillStyle='#1e293b';c.beginPath();c.arc(xPixel(0),yPixel(0),7,0,Math.PI*2);c.fill();
 c.fillStyle='#1e293b';c.textAlign='center';c.fillText('Radar',xPixel(0),plot.bottom+32);
 if(!radarLive||!radarLive.healthy)return;
 for(let i=0;i<radarLive.targets.length;i++){
  const t=radarLive.targets[i];
  if(!t.valid||!Number.isFinite(t.xMm)||!Number.isFinite(t.yMm))continue;
  if(t.xMm< -6000||t.xMm>6000||t.yMm<0||t.yMm>6000)continue;
  const x=xPixel(t.xMm),y=yPixel(t.yMm);
  c.fillStyle=t.insideZone?'#15803d':'#ea580c';
  c.beginPath();c.arc(x,y,9,0,Math.PI*2);c.fill();
  c.strokeStyle='#fff';c.lineWidth=2;c.stroke();
  c.fillStyle='#fff';c.font='bold 11px system-ui';c.textAlign='center';
  c.fillText(String(i+1),x,y+4);
  c.font='12px system-ui';
 }
}
function updateRadarInfo(){
 const el=document.getElementById('radarInfo');
 if(!radarLive||!radarLive.healthy){
  el.textContent='Radar LD2450 : liaison absente ou données expirées';return;
 }
 const targets=radarLive.targets.filter(t=>t.valid);
 el.textContent=targets.length?
  targets.map((t,i)=>'Cible '+(i+1)+' : X '+(t.xMm/1000).toFixed(2)+
  ' m, Y '+(t.yMm/1000).toFixed(2)+' m, '+t.speedCmS+
  ' cm/s — '+(t.insideZone?'dans la zone':'hors zone')).join(' | '):
  'Liaison radar OK — aucune cible détectée';
}
function updateZoneView(){
 radarCanvas.classList.toggle('draw',document.getElementById('zoneDraw').checked);
 drawRadar();
}
for(const key of zoneFields){
 document.getElementById(key).addEventListener('input',()=>{zoneDirty=true;updateZoneView();});
}
document.getElementById('zoneEnabled').addEventListener('change',()=>{zoneDirty=true;updateZoneView();});
document.getElementById('zoneDraw').addEventListener('change',updateZoneView);
radarCanvas.addEventListener('pointerdown',e=>{
 if(!document.getElementById('zoneDraw').checked)return;
 dragStart=canvasPoint(e);dragEnd=dragStart;
 radarCanvas.setPointerCapture(e.pointerId);drawRadar();
});
radarCanvas.addEventListener('pointermove',e=>{
 if(!dragStart)return;
 dragEnd=canvasPoint(e);drawRadar();
});
radarCanvas.addEventListener('pointerup',e=>{
 if(!dragStart)return;
 dragEnd=canvasPoint(e);
 const minX=Math.min(dragStart.x,dragEnd.x),maxX=Math.max(dragStart.x,dragEnd.x);
 const minY=Math.min(dragStart.y,dragEnd.y),maxY=Math.max(dragStart.y,dragEnd.y);
 if(maxX>minX&&maxY>minY){
  for(const [k,v] of Object.entries({zoneMinX:minX,zoneMaxX:maxX,zoneMinY:minY,zoneMaxY:maxY}))
   document.getElementById(k).value=v;
  document.getElementById('zoneEnabled').checked=true;
  zoneDirty=true;
  document.getElementById('zoneMessage').textContent='Nouvelle zone dessinée : enregistrer pour appliquer.';
 }
 dragStart=null;dragEnd=null;drawRadar();
});
radarCanvas.addEventListener('pointercancel',()=>{dragStart=null;dragEnd=null;drawRadar();});
async function pollRadar(){
 if(radarPolling||document.getElementById('zoneSection').hidden)return;
 radarPolling=true;
 try{
  const r=await fetch('/api/radar/live',{cache:'no-store'});
  if(!r.ok)throw Error('HTTP '+r.status);
  radarLive=await r.json();
 }catch(e){radarLive=null;}
 finally{radarPolling=false;updateRadarInfo();drawRadar();}
}
setInterval(pollRadar,600);
drawRadar();
async function refresh(){
 try {
  const r=await fetch('/api/status');const d=await r.json();
  document.getElementById('status').textContent=JSON.stringify(d,null,2);
  document.getElementById('firmwareVersion').textContent=d.version;
  const isAuto=d.mode==='AUTO';
  document.getElementById('lightOn').disabled=!isAuto;
  document.getElementById('lightOff').disabled=!isAuto;
  document.getElementById('clockText').textContent=d.localTime;
  document.getElementById('scheduleText').textContent=d.scheduleOn?'ACTIF':'inactif';
 if(!radarFormDirty)document.getElementById('sensorType').value=d.sensorType;
 document.getElementById('zoneSection').hidden=document.getElementById('sensorType').value!=='ld2450';
 if(!zoneDirty){
  for(const key of zoneFields)document.getElementById(key).value=d[key];
  document.getElementById('zoneEnabled').checked=d.zoneEnabled;
 }
 updateZoneView();
 if(!radarFormDirty) {
  document.getElementById('onDelay').value=d.onDelaySeconds;
  document.getElementById('offDelay').value=d.offDelayMinutes;
 }
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
document.getElementById('lightForceOn').addEventListener('click',()=>sendLightCommand('on'));
document.getElementById('lightForceOff').addEventListener('click',()=>sendLightCommand('off'));
document.getElementById('lightAuto').addEventListener('click',()=>sendLightCommand('auto'));
document.getElementById('lightOn').addEventListener('click',()=>sendLightCommand('auto/on'));
document.getElementById('lightOff').addEventListener('click',()=>sendLightCommand('auto/off'));
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
 d.set('onDelay',document.getElementById('onDelay').value);
 d.set('offDelayMin',document.getElementById('offDelay').value);
 const r=await fetch('/api/radar',{method:'POST',body:d});
 if(!r.ok){alert('Radar non enregistré : '+await r.text());return;}
 radarFormDirty=false;
 await refresh();
}
async function saveZone(e) {
 e.preventDefault();
 const d=new URLSearchParams();
 d.set('enabled',document.getElementById('zoneEnabled').checked?'true':'false');
 for(const key of ['zoneMinX','zoneMaxX','zoneMinY','zoneMaxY'])
   d.set(key,document.getElementById(key).value);
 const message=document.getElementById('zoneMessage');
 try{
  const r=await fetch('/api/radar/zone',{method:'POST',body:d});
  if(!r.ok)throw Error(await r.text());
  zoneDirty=false;
  message.textContent='Zone enregistrée.';
  await refresh();
 }catch(error){message.textContent='Zone non enregistrée : '+error.message;}
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
    if(portalClient()){redirectToSetup();return;}
    if (!authorize()) return;
    server.send_P(200, "text/html; charset=utf-8", HOME);
  });
  // Captive portal detection on Android, iOS and Windows. All paths redirect
  // only while serving the temporary access point, not on the home network.
  // Captive portal probes used by Android, iOS/macOS, Windows and Firefox.
  // An HTTP redirect is deliberately NOT the expected probe response (204 or
  // "Success"), so the OS can offer its captive-network assistant.
  const char *probePaths[]={"/generate_204","/gen_204","/hotspot-detect.html",
    "/library/test/success.html","/connecttest.txt","/ncsi.txt","/redirect",
    "/canonical.html","/success.txt","/fwlink","/mobile/status.php",
    "/kindle-wifi/wifistub.html","/captive-portal"};
  for(const char *path:probePaths) {
    server.on(path, HTTP_GET, [](){
      if(portalClient()) {
        Serial.printf("[WiFi] Captive probe: %s\\n",server.uri().c_str());
        redirectToSetup();
      } else server.send(204,"text/plain","");
    });
  }
  server.on("/setup", HTTP_GET, [](){
    if(!portalClient() && !authorize())return;
    server.send(200,"text/html; charset=utf-8",R"PORTAL(
<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SeeYa - Configuration Wi-Fi</title>
<style>body{font:16px system-ui;max-width:440px;margin:24px auto;padding:16px}
label{display:block;margin:14px 0}input{padding:9px;width:95%}
button{padding:12px}small{color:#555}</style></head>
<body><h1>SeeYa — Configuration Wi-Fi</h1>
<p>Renseigner le réseau domestique. Une fois connecté, le réseau SeeYa de secours disparaîtra.</p>
<form id="setupForm">
<label>SSID domestique <input id="ssid" required maxlength="32"></label>
<label>Mot de passe Wi-Fi <input id="password" type="password" maxlength="63"></label>
<label>SSID du réseau de secours <input id="ap" required maxlength="32"></label>
<button type="submit">Enregistrer</button></form>
<p id="message" role="status"></p>
<small>Si le navigateur ne s'ouvre pas automatiquement, aller sur http://192.168.4.1/setup.</small>
<script>
fetch('/api/wifi').then(r=>r.json()).then(d=>{
 document.getElementById('ssid').value=d.ssid;
 document.getElementById('ap').value=d.fallbackSsid;
}).catch(()=>{});
document.getElementById('setupForm').addEventListener('submit',async e=>{
 e.preventDefault();
 const d=new URLSearchParams();
 d.set('ssid',document.getElementById('ssid').value);
 d.set('password',document.getElementById('password').value);
 d.set('keepPassword','false');
 d.set('fallbackSsid',document.getElementById('ap').value);
 const message=document.getElementById('message');
 try{
  const r=await fetch('/api/wifi',{method:'POST',body:d});
  if(!r.ok)throw Error(await r.text());
  message.textContent='Enregistré. Connexion au réseau domestique en cours. Le réseau de secours se fermera automatiquement.';
 }catch(error){message.textContent='Erreur : '+error.message;}
});
</script></body></html>
)PORTAL");
  });
  server.on("/api/wifi", HTTP_GET, [](){
    if(!portalClient() && !authorize())return;
    // Do not return the saved Wi-Fi password.
    String response="{\"ssid\":\"";
    // Reject quotes and control characters in SSIDs to keep JSON simple.
    const String ssid=wifiFallback.ssid();
    for(size_t i=0;i<ssid.length();++i){
      if(ssid[i]=='"'||ssid[i]=='\\')response+='\\';
      if((uint8_t)ssid[i]>=32)response+=ssid[i];
    }
    response += "\",\"fallbackSsid\":\"";
    const String ap=wifiFallback.apName();
    for(size_t i=0;i<ap.length();++i){
      if(ap[i]=='"'||ap[i]=='\\')response+='\\';
      if((uint8_t)ap[i]>=32)response+=ap[i];
    }
    response += "\"}";
    jsonHeader();server.send(200,"application/json",response);
  });
  server.on("/api/wifi", HTTP_POST, [](){
    if(!portalClient() && !authorize())return;
    const String ssid=server.arg("ssid");
    String password=server.arg("password");
    const String ap=server.arg("fallbackSsid");
    if(server.arg("keepPassword")=="true" && password.isEmpty() &&
       ssid==wifiFallback.ssid()) {
      // Keep current secret without exposing it to clients: handled by manager.
      password=String("\x01"); // sentinel, resolved below
    }
    if(!WifiFallback::validApName(ap) ||
       ssid.length()==0 || ssid.length()>32 || password.length()>63){
      server.send(400,"text/plain","Invalid Wi-Fi settings");return;
    }
    // Replace a blank password only when explicitly submitted.
    const bool keep=password==String("\x01");
    jsonHeader();server.send(200,"application/json","{\"saved\":true}");
    server.client().flush();
    wifiFallback.configure(ssid,keep?wifiFallback.password():password,ap);
  });
  server.on("/api/status", HTTP_GET, status);
  server.on("/api/light/on", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::ForceOn; resetAutoManual(); output.request(true); status();
  });
  server.on("/api/light/off", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::ForceOff; resetAutoManual(); output.request(false); status();
  });
  server.on("/api/light/auto", HTTP_POST, []() {
    if (!authorize()) return;
    mode = Mode::Auto; resetAutoManual(); updateClockAndSchedule(); applyAutoLight(); status();
  });
  server.on("/api/light/auto/on", HTTP_POST, []() {
    if(!authorize())return;
    if(mode!=Mode::Auto){server.send(409,"text/plain","AUTO required");return;}
    updateClockAndSchedule();setAutoManual(true);status();
  });
  server.on("/api/light/auto/off", HTTP_POST, []() {
    if(!authorize())return;
    if(mode!=Mode::Auto){server.send(409,"text/plain","AUTO required");return;}
    updateClockAndSchedule();setAutoManual(false);status();
  });
  server.on("/api/schedule", HTTP_GET, [](){
    if(!authorize())return;
    String response="{\"events\":[";
    for(size_t i=0;i<WeeklySchedule::SlotCount;++i){
      if(i)response+=",";
      const auto &s=schedule.slot(i);
      response += "{\"action\":" + String((unsigned)s.action);
      response += ",\"minute\":" + String(s.minute);
      response += ",\"days\":" + String(s.days) + "}";
    }
    response += "]}";
    jsonHeader();server.send(200,"application/json",response);
  });
  server.on("/api/schedule", HTTP_POST, [](){
    if(!authorize())return;
    WeeklySchedule::Slot proposed[WeeklySchedule::SlotCount];
    for(size_t i=0;i<WeeklySchedule::SlotCount;++i){
      const String suffix=String(i);
      const char *prefixes[3]={"a","m","d"};
      int values[3]={};
      for(int k=0;k<3;++k) {
        const String v=server.arg(String(prefixes[k])+suffix);
        if(v.isEmpty()||v.length()>4){server.send(400,"text/plain","Invalid event");return;}
        for(size_t n=0;n<v.length();++n)if(!isDigit(v[n])){
          server.send(400,"text/plain","Invalid event");return;
        }
        values[k]=v.toInt();
      }
      proposed[i].action=static_cast<WeeklySchedule::Action>(values[0]);
      proposed[i].minute=values[1];
      proposed[i].days=values[2];
      if(values[0]>2||values[1]>=1440||values[2]>127||!WeeklySchedule::valid(proposed[i])){
        server.send(400,"text/plain","Invalid event range");return;
      }
    }
    for(size_t i=0;i<WeeklySchedule::SlotCount;++i)schedule.setSlot(i,proposed[i]);
    schedule.save();updateClockAndSchedule();
    applyAutoLight();
    server.send(200,"application/json","{\"saved\":true}");
  });
  server.on("/api/radar", HTTP_POST, []() {
    if (!authorize()) return;
    // Both timings are user-configurable and validated before NVS changes.
    auto parseNumber=[](const String &raw, uint32_t min, uint32_t max, uint32_t &value)->bool {
      if(raw.isEmpty() || raw.length()>5)return false;
      for(size_t i=0;i<raw.length();++i)if(!isDigit(raw[i]))return false;
      value=(uint32_t)raw.toInt();
      return value>=min && value<=max;
    };
    uint32_t newOnDelay=0, newOffDelayMin=0;
    if(!parseNumber(server.arg("onDelay"),0,3600,newOnDelay) ||
       !parseNumber(server.arg("offDelayMin"),1,1440,newOffDelayMin)) {
      server.send(400,"text/plain","Invalid radar delays");return;
    }
    const String newType=server.arg("type");
    uint8_t next=0;
    if(newType=="ld2410c")next=1;
    else if(newType=="ld2450")next=2;
    else if(newType!="disabled"){server.send(400,"text/plain","Invalid radar");return;}
    const bool delayChanged=(onDelaySeconds!=newOnDelay || offDelayMinutes!=newOffDelayMin);
    onDelaySeconds=newOnDelay;
    offDelayMinutes=newOffDelayMin;
    offDelaySeconds=offDelayMinutes*60UL;
    prefs.putUInt("radOnSec",onDelaySeconds);
    prefs.putUInt("radOffMin",offDelayMinutes);
    if(delayChanged) {
      // Restart timing from the present moment, preventing stale elapsed timers.
      previousPresence=false;
      presenceSinceMs=millis();
      absenceSinceMs=millis();
    }
    if(sensorType!=next) {
      if(sensorType)radarUart.end();
      sensorType=next;
      if(sensorType==1)radar.begin(radarUart,4,5);
      else if(sensorType==2)radar2450.begin(radarUart,4,5);
      radarHold=false;
      previousPresence=false;
      presenceSinceMs=millis();
      absenceSinceMs=millis();
      lastRadarLogMs=millis()-2000;
      Serial.printf("[SeeYa] Radar selection: %s\n",sensorType==1?"LD2410C":(sensorType==2?"LD2450":"OFF"));
    }
    prefs.putUChar("sensorType",sensorType);
    prefs.remove("radarOn");
    updateClockAndSchedule(); applyAutoLight();
    status();
  });
  // Lightweight telemetry endpoint for the interactive LD2450 map.
  server.on("/api/radar/live", HTTP_GET, [](){
    if(!authorize())return;
    const bool enabled=sensorType==2;
    const bool health=enabled&&radar2450.healthy();
    String json="{\"healthy\":";
    json+=health?"true":"false";
    json+=",\"targets\":[";
    for(size_t i=0;i<3;++i){
      if(i)json+=",";
      const auto &t=radar2450.target(i);
      json+="{\"valid\":";
      json+=(health&&t.valid)?"true":"false";
      json+=",\"xMm\":"+String(t.xMm);
      json+=",\"yMm\":"+String(t.yMm);
      json+=",\"speedCmS\":"+String(t.speedCmS);
      json+=",\"insideZone\":";
      json+=(health&&radar2450.inside(t))?"true":"false";
      json+="}";
    }
    json+="]}";
    jsonHeader();server.send(200,"application/json",json);
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
      if (adminAuthenticationRequired() && !server.authenticate("admin", SEEYA_ADMIN_PASSWORD)) return;
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
    if(portalClient()) {
      Serial.printf("[WiFi] Captive HTTP redirect: %s\\n",server.uri().c_str());
      redirectToSetup();
      return;
    }
    if (!authorize()) return;
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
}
} // namespace

void setup() {
  Serial.begin(115200);
  Serial.println("[SeeYa] Boot - USB serial monitor 115200 bauds");
  Serial.println("[SeeYa] Radar UART1: ESP RX=GPIO4 (radar TX), ESP TX=GPIO5 (radar RX), 256000 bauds");
  prefs.begin("seeya", false);
  output.begin(prefs);
  schedule.begin(prefs);
  // New preference keys deliberately start with 15 s / 15 min defaults.
  // Legacy "offDelay" (seconds) is left untouched but no longer loaded.
  onDelaySeconds=prefs.getUInt("radOnSec",15);
  if(onDelaySeconds>3600)onDelaySeconds=15;
  offDelayMinutes=prefs.getUInt("radOffMin",15);
  if(offDelayMinutes<1 || offDelayMinutes>1440)offDelayMinutes=15;
  offDelaySeconds=offDelayMinutes*60UL;
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
  Serial.printf("[SeeYa] Radar initial: %s\n",sensorType==1?"LD2410C":(sensorType==2?"LD2450":"OFF"));
  if (String(SEEYA_ADMIN_PASSWORD) == "CHANGE_ME_ADMIN_PASSWORD") {
    Serial.println("ERROR: replace the example administrator password in include/secrets.h (empty is allowed)");
    return;
  }
  if (!adminAuthenticationRequired()) {
    Serial.println("[SeeYa] WARNING: HTTP authentication disabled (empty admin password).");
    Serial.println("[SeeYa] Web UI, lighting API, configuration and OTA are unrestricted.");
  }
  const char *initialSsid=String(SEEYA_WIFI_SSID)=="CHANGE_ME_WIFI"?"":SEEYA_WIFI_SSID;
  wifiFallback.begin(prefs,initialSsid,SEEYA_WIFI_PASSWORD);
  // French civil time with automatic CET/CEST daylight-saving transitions.
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3","pool.ntp.org","time.google.com");
  setupRoutes();
}
void loop() {
  if(sensorType) {
    if(sensorType==1)radar.update();
    else radar2450.update();
    const uint32_t now=millis();
    const bool presence=sensorType==1?radar.present():radar2450.present();
    if(presence!=previousPresence) {
      // A single loss of presence restarts the continuous-presence timer.
      if(presence)presenceSinceMs=now;
      else absenceSinceMs=now;
      previousPresence=presence;
    }
    if(presence) {
      if(!radarHold && (uint32_t)(now-presenceSinceMs)>=onDelaySeconds*1000UL)
        radarHold=true;
    } else if(radarHold && (uint32_t)(now-absenceSinceMs)>=offDelaySeconds*1000UL) {
      radarHold=false;
    }
    // Connection loss is treated as absent once radar.present() is false.
  } else {
    radarHold=false;
    previousPresence=false;
  }
  updateClockAndSchedule();
  applyAutoLight();
  server.handleClient();
  output.update();
  logRadarStatus();
  wifiFallback.update();
  delay(2);
}
