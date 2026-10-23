
//=====================================================================
//  Interface web (WiFi)
//
//  - page /            : les trois mesures courantes et l'historique de
//                        la pression sur 6, 12, 24 ou 96 h ;
//  - page /wifi        : saisie du reseau domestique (enregistree en NVS) ;
//  - /api/data         : JSON des mesures courantes ;
//  - /api/history?h=6  : JSON des series d'historique (6/12/24/96 h).
//
//  La page ne depend d'aucune ressource externe (pas de CDN) : elle
//  fonctionne aussi quand le point d'acces de secours est actif, sans
//  acces Internet.
//=====================================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <math.h>

#include "webui.h"
#include "barometer.h"
#include "screen.h"
#include "Definition.h"
#include "datetime_local.h"
#include "rtc_clock.h"

// Mesures courantes et reglages, definis dans Barometre_marin.ino
extern float   pres;
extern float   tempC;         // temperature en degres C (unite d'affichage a part)
extern int16_t humidity;
extern bool    bfahrenheit;
extern bool    bme280_ok;
extern int8_t  history_mode;
extern int32_t speed_nmea;    // 0 = NMEA 2000 seul, sinon debit RS485

// Defini dans Barometre_marin.ino : applique un reglage venu du web (menu
// tactile mis a jour et page courante redessinee).
void uiSettingsChanged();

//---------------------------------------------------------------------
// Parametres
//---------------------------------------------------------------------

#define WIFI_AP_NAME    "Barometre"
#define WIFI_AP_PASS    "barometre123"     // 8 caracteres minimum
#define WIFI_HOSTNAME   "barometre"        // http://barometre.local/
#define WIFI_STA_WAIT   20000UL            // attente avant l'AP de secours
#define WIFI_RETRY      30000UL            // nouvel essai de connexion
#define WIFI_TX_POWER   WIFI_POWER_8_5dBm  // puissance d'emission reduite : la radio
                                           // perturbe sinon la dalle RGB (lignes qui
                                           // clignotent). Suffisant a l'interieur.

#define TAG             "web"              // etiquette des traces ESP_LOG

static WebServer  server(80);
static Preferences prefs;

static String     s_ssid;
static String     s_pass;
static bool       s_staConfigured = false;
static bool       s_apStarted     = false;
static bool       s_mdnsStarted   = false;
static uint32_t   s_staStart      = 0;
static uint32_t   s_lastRetry     = 0;

// Nombre de coupures de la liaison WiFi depuis le demarrage : permet de
// savoir si un incident d'affichage coincide avec une reassociation.
static volatile uint32_t s_staDisconnects = 0;

static void onNetworkEvent(WiFiEvent_t event)
{
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_staDisconnects++;
}

//---------------------------------------------------------------------
// Langue de l'interface web
//
// Une seule page est ecrite : les textes sont remplaces au moment de
// l'envoi (page principale) ou lus dans la table ci-dessous (pages
// construites dynamiquement). Le choix est conserve en NVS et ne
// concerne que le web : l'ecran de la carte reste en francais.
//---------------------------------------------------------------------

#define LANG_FR   0
#define LANG_EN   1

struct WebText {
  const char *fr;
  const char *en;
};

enum : uint8_t {
  TX_LANG, TX_SETTINGS, TX_SAVED, TX_BAD_TIME,
  TX_AP_HINT, TX_CFG_WIFI, TX_PRESSURE, TX_TEMPERATURE, TX_HUMIDITY,
  TX_HIST_TITLE, TX_CONNECTING, TX_OFFLINE, TX_AP_PREFIX, TX_EMPTY_HIST, TX_DEC,
  TX_SCREEN, TX_RESTART_DISP, TX_REBOOT, TX_SCREEN_NOTE,
  TX_MSG_BUSY, TX_MSG_FAIL, TX_MSG_NORESP, TX_MSG_DISPLAY_OK, TX_MSG_REBOOT,
  TX_CONFIRM_REBOOT,
  TX_UNITS, TX_SAVE, TX_NMEA, TX_NMEA_PORT, TX_NMEA_ONLY, TX_BAUD, TX_NMEA_NOTE,
  TX_WIFI, TX_NET_SAVED, TX_NONE, TX_CONNECTED, TX_AP_MODE, TX_CONFIGURE,
  TX_DATETIME, TX_CLOCK, TX_SET_CLOCK, TX_THIS_TIME,
  TX_RTC_LOST, TX_RTC_HW_OK, TX_RTC_SOFT, TX_BACK,
  TX_SSID, TX_PASS, TX_SAVE_RESTART, TX_WIFI_NOTE, TX_BACK_SHORT,
  TX_SSID_EMPTY, TX_SAVED_TITLE, TX_REBOOT_JOIN, TX_RECONNECT, TX_NADA
};

static const WebText kText[] = {
  { "Langue",                                      "Language" },
  { "Reglages",                                    "Settings" },
  { "Reglage enregistre.",                         "Setting saved." },
  { "Date ou heure invalide : rien n'a ete change.",
    "Invalid date or time: nothing was changed." },
  { "Point d'acces de secours :",                  "Backup access point:" },
  { "configurer le WiFi",                          "set up WiFi" },
  { "Pression",                                    "Pressure" },
  { "Temperature",                                 "Temperature" },
  { "Humidite",                                    "Humidity" },
  { "Historique de la pression",                   "Pressure history" },
  { "connexion...",                                "connecting..." },
  { "hors ligne",                                  "offline" },
  { "point d'acces ",                              "access point " },
  { "Pas encore de mesure enregistree pour cette periode.",
    "No measurement recorded for this period yet." },
  { ",",                                           "." },
  { "Ecran",                                       "Display" },
  { "Redemarrer l'ecran",                          "Restart display" },
  { "Redemarrer la carte",                         "Restart board" },
  { "Le redemarrage de l'ecran reinitialise la dalle (image decalee) en une "
    "seconde environ : les mesures et l'historique sont conserves. Le "
    "redemarrage de la carte efface l'historique en cours.",
    "Restarting the display resets the panel (shifted image) in about a "
    "second: measurements and history are kept. Restarting the board clears "
    "the current history." },
  { "En cours...",                                 "Working..." },
  { "Echec, code ",                                "Failed, code " },
  { "Pas de reponse de la carte.",                 "No response from the board." },
  { "Ecran reinitialise.",                         "Display restarted." },
  { "Redemarrage en cours...",                     "Restarting..." },
  { "Redemarrer la carte ? L'historique en cours sera perdu.",
    "Restart the board? The current history will be lost." },
  { "Unites",                                      "Units" },
  { "Enregistrer",                                 "Save" },
  { "Sortie NMEA",                                 "NMEA output" },
  { "Port RS485 (NMEA 0183)",                      "RS485 port (NMEA 0183)" },
  { "NMEA 2000 seul (CAN)",                        "NMEA 2000 only (CAN)" },
  { " bauds",                                      " baud" },
  { "Le bus CAN (NMEA 2000) emet dans tous les cas les PGN 130316, 130313 et "
    "130314, une fois par seconde.",
    "The CAN bus (NMEA 2000) always transmits PGNs 130316, 130313 and 130314, "
    "once per second." },
  { "Reseau WiFi",                                 "WiFi network" },
  { "Reseau enregistre :",                         "Saved network:" },
  { "(aucun)",                                     "(none)" },
  { "Connecte - ",                                 "Connected - " },
  { "Point d'acces de secours \"Barometre\" (192.168.4.1)",
    "Backup access point \"Barometre\" (192.168.4.1)" },
  { "Configurer le reseau",                        "Configure network" },
  { "Date et heure",                               "Date and time" },
  { "Horloge de la carte",                         "Board clock" },
  { "Regler l'horloge",                            "Set clock" },
  { "Heure de cet appareil",                       "This device's time" },
  { "Horloge materielle presente, mais elle a perdu l'heure : un reglage est "
    "necessaire.",
    "Hardware clock present, but it has lost the time: it must be set." },
  { "Horloge materielle detectee : l'heure est conservee hors tension.",
    "Hardware clock detected: the time is kept when the board is unpowered." },
  { "Aucune horloge materielle : l'heure est perdue a chaque mise hors tension.",
    "No hardware clock: the time is lost at every power-off." },
  { "Retour au barometre",                         "Back to the barometer" },
  { "Nom du reseau (SSID)",                        "Network name (SSID)" },
  { "Mot de passe",                                "Password" },
  { "Enregistrer et redemarrer",                   "Save and restart" },
  { "Laisse le mot de passe vide pour conserver l'ancien. Apres redemarrage, "
    "la page reste joignable sur http://barometre.local/ ou, si le reseau est "
    "absent, sur http://192.168.4.1/ (reseau Barometre).",
    "Leave the password empty to keep the current one. After the restart, the "
    "page is still reachable at http://barometre.local/ or, if the network is "
    "missing, at http://192.168.4.1/ (network Barometre)." },
  { "Retour",                                      "Back" },
  { "SSID vide",                                   "Empty SSID" },
  { "Enregistre",                                  "Saved" },
  { "Le barometre redemarre et rejoint le reseau ",
    "The barometer is restarting and joining the network " },
  { "Reconnecte-toi a ton reseau (ou au reseau Barometre si la connexion "
    "echoue), puis ouvre http://barometre.local/ ou http://192.168.4.1/",
    "Reconnect to your network (or to the Barometre network if the connection "
    "fails), then open http://barometre.local/ or http://192.168.4.1/" },
  { "introuvable",                                 "not found" }
};

#define TEXT_COUNT  ((uint8_t)(sizeof(kText) / sizeof(kText[0])))

static uint8_t s_lang = LANG_FR;

static inline const char *T(uint8_t id)
{
  if (id >= TEXT_COUNT) return "";

  return (s_lang == LANG_EN) ? kText[id].en : kText[id].fr;
}

static inline const char *langTag()
{
  return (s_lang == LANG_EN) ? "en" : "fr";
}

//---------------------------------------------------------------------
// Page principale
//---------------------------------------------------------------------

static const char PAGE_INDEX[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="{{LANG}}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Barometre</title>
<style>
:root{--bg:#0f172a;--card:#1e293b;--txt:#e2e8f0;--dim:#94a3b8;--accent:#38bdf8}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:16px 20px 4px;max-width:900px;margin:0 auto}
h1{font-size:20px;margin:0;font-weight:600}
.btn{display:inline-block;background:var(--accent);color:#062133;font-weight:600;font-size:15px;
text-decoration:none;padding:11px 20px;border-radius:11px;letter-spacing:.01em}
#clock{color:var(--dim);font-variant-numeric:tabular-nums;font-size:14px}
main{max-width:900px;margin:0 auto;padding:0 16px 24px}
.cards{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:14px 0}
.card{background:var(--card);border-radius:14px;padding:14px 16px}
.card .lbl{color:var(--dim);font-size:12px;text-transform:uppercase;letter-spacing:.07em}
.card .val{display:flex;align-items:baseline;gap:6px;margin-top:4px}
.card b{font-size:30px;font-weight:600;font-variant-numeric:tabular-nums}
.card i{font-style:normal;color:var(--dim)}
.panel{background:var(--card);border-radius:14px;padding:14px 16px}
.head{display:flex;flex-wrap:wrap;gap:10px;justify-content:space-between;align-items:center;margin-bottom:8px}
h2{font-size:15px;margin:0;font-weight:600}
.ranges{display:flex;gap:6px}
.ranges button{background:#0f172a;color:var(--dim);border:1px solid #334155;border-radius:9px;padding:6px 12px;font-size:14px;cursor:pointer}
.ranges button.on{background:var(--accent);border-color:var(--accent);color:#062133;font-weight:600}
canvas{width:100%;height:300px;display:block}
#note{color:var(--dim);font-size:13px;margin-top:6px;min-height:17px}
.banner{display:none;background:#7c2d12;color:#fed7aa;border-radius:10px;padding:10px 14px;margin-top:14px;font-size:14px}
.banner a{color:#fed7aa}
footer{max-width:900px;margin:0 auto;padding:8px 20px 22px;color:#64748b;font-size:12px;display:flex;justify-content:space-between}
footer a{color:#94a3b8}
@media(max-width:640px){.cards{grid-template-columns:1fr}.card b{font-size:26px}}
</style>
</head>
<body>
<header><h1>Barometre</h1><a class="btn" href="/params">{{SETTINGS}}</a></header>
<main>
<div class="banner" id="banner">{{AP_HINT}} <a href="/wifi">{{CFG_WIFI}}</a></div>
<section class="cards">
  <div class="card"><div class="lbl">{{PRESSURE}}</div><div class="val"><b id="p">--</b><i>hPa</i></div></div>
  <div class="card"><div class="lbl">{{TEMPERATURE}}</div><div class="val"><b id="t">--</b><i id="tu">&deg;C</i></div></div>
  <div class="card"><div class="lbl">{{HUMIDITY}}</div><div class="val"><b id="h">--</b><i>%</i></div></div>
</section>
<section class="panel">
  <div class="head">
    <h2>{{HIST_TITLE}}</h2>
    <div class="ranges">
      <button data-h="6" class="on">6 h</button>
      <button data-h="12">12 h</button>
      <button data-h="24">24 h</button>
      <button data-h="96">96 h</button>
    </div>
  </div>
  <canvas id="chart"></canvas>
  <div id="note"></div>
</section>
</main>
<footer><span id="state">{{CONNECTING}}</span><span id="clock">--</span></footer>
<script>
var range = 6, hist = null;
function $(id){ return document.getElementById(id); }
function num(v, d){ return v.toFixed(d).replace('.', '{{DEC}}'); }
function hhmm(sec){
  sec = ((Math.round(sec) % 86400) + 86400) % 86400;
  var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60);
  return ('0' + h).slice(-2) + ':' + ('0' + m).slice(-2);
}
function niceStep(raw){
  var mag = Math.pow(10, Math.floor(Math.log10(raw)));
  var n = raw / mag;
  return (n <= 1 ? 1 : n <= 2 ? 2 : n <= 5 ? 5 : 10) * mag;
}

function tick(){
  fetch('/api/data', {cache:'no-store'}).then(function(r){ return r.json(); }).then(function(d){
    $('p').textContent = d.ok ? num(d.p, 2) : '--';
    $('t').textContent = d.ok ? num(d.fh ? d.t * 9 / 5 + 32 : d.t, 1) : '--';
    $('tu').innerHTML  = d.fh ? '&deg;F' : '&deg;C';
    $('h').textContent = d.ok ? d.h : '--';
    $('clock').textContent = d.time;
    $('banner').style.display = d.sta ? 'none' : 'block';
    $('state').textContent = d.sta ? ('WiFi ' + d.rssi + ' dBm - ' + d.ip) : "{{AP_PREFIX}}" + d.ip;
  }).catch(function(){
    $('state').textContent = "{{OFFLINE}}";
  });
}

function loadHist(h){
  range = h;
  var bs = document.querySelectorAll('.ranges button');
  for (var i = 0; i < bs.length; i++)
    bs[i].className = (+bs[i].dataset.h === h) ? 'on' : '';
  fetch('/api/history?h=' + h, {cache:'no-store'}).then(function(r){ return r.json(); })
    .then(function(d){ hist = d; draw(); })
    .catch(function(){ hist = null; draw(); });
}

function draw(){
  var cv = $('chart'), ctx = cv.getContext('2d');
  var dpr = window.devicePixelRatio || 1;
  var W = cv.clientWidth, H = cv.clientHeight;
  cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr);
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, W, H);

  var padL = 46, padR = 12, padT = 12, padB = 24;
  var iw = W - padL - padR, ih = H - padT - padB;

  var data = (hist && hist.pres) ? hist.pres : [];
  var n = data.length, vals = [], i;
  for (i = 0; i < n; i++) if (data[i] !== null) vals.push(data[i]);

  // Echelle verticale identique a celle de l'ecran de la carte : 50 hPa,
  // une ligne et une etiquette tous les 10 hPa.
  var lo = (hist && hist.pmin !== undefined) ? hist.pmin : 980;
  var hi = (hist && hist.pmax !== undefined) ? hist.pmax : 1030;

  function X(i){ return padL + (n > 1 ? i * iw / (n - 1) : iw / 2); }
  function Y(v){
    v = Math.max(lo, Math.min(hi, v));      // hors echelle : plaque sur le bord
    return padT + (hi - v) * ih / (hi - lo);
  }

  ctx.font = '12px system-ui,sans-serif';
  ctx.textBaseline = 'middle';

  for (var v = hi; v >= lo - 0.001; v -= 5) {
    var yy = Y(v);
    var major = (Math.abs(v % 10) < 0.001);
    ctx.strokeStyle = major ? '#334155' : '#243247'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(padL, yy); ctx.lineTo(W - padR, yy); ctx.stroke();
    if (major) {
      ctx.fillStyle = '#94a3b8'; ctx.textAlign = 'right';
      ctx.fillText(String(Math.round(v)), padL - 7, yy);
    }
  }

  ctx.strokeStyle = '#475569';
  ctx.beginPath(); ctx.moveTo(padL, padT + ih); ctx.lineTo(W - padR, padT + ih); ctx.stroke();

  // Graduations d'heure, du plus ancien au plus recent
  if (hist && n > 1) {
    var per = hist.step * 60;
    ctx.fillStyle = '#94a3b8'; ctx.textBaseline = 'top';
    for (var k = 0; k <= 4; k++) {
      var idx = Math.round(k * (n - 1) / 4);
      ctx.textAlign = (k === 0) ? 'left' : (k === 4 ? 'right' : 'center');
      ctx.fillText(hhmm(hist.tod - (n - 1 - idx) * per), X(idx), padT + ih + 6);
    }
    ctx.textAlign = 'left';
  }

  $('note').textContent = vals.length ? '' : "{{EMPTY_HIST}}";
  if (!vals.length) return;

  // Courbe : segment rouge si la pression chute de 3 hPa ou plus
  ctx.lineWidth = 2.2; ctx.lineJoin = 'round'; ctx.lineCap = 'round';
  var prev = null;
  for (i = 0; i < n; i++) {
    if (data[i] === null) { prev = null; continue; }
    if (prev !== null) {
      ctx.strokeStyle = (prev.v - data[i] >= 3) ? '#f87171' : '#38bdf8';
      ctx.beginPath(); ctx.moveTo(X(prev.i), Y(prev.v)); ctx.lineTo(X(i), Y(data[i])); ctx.stroke();
    }
    prev = { i: i, v: data[i] };
  }

  ctx.fillStyle = '#38bdf8';
  for (i = 0; i < n; i++) {
    if (data[i] === null) continue;
    ctx.beginPath(); ctx.arc(X(i), Y(data[i]), 2.6, 0, 6.2832); ctx.fill();
  }

  var last = data[n - 1];
  if (last !== null) {
    ctx.fillStyle = '#e2e8f0'; ctx.textAlign = 'right'; ctx.textBaseline = 'bottom';
    ctx.fillText(Math.round(last) + ' hPa', W - padR, Y(last) - 7);
  }
}

document.querySelectorAll('.ranges button').forEach(function(b){
  b.addEventListener('click', function(){ loadHist(+b.dataset.h); });
});
window.addEventListener('resize', draw);
tick(); loadHist(6);
setInterval(tick, 3000);
setInterval(function(){ if (!document.hidden) loadHist(range); }, 60000);
</script>
</body>
</html>)rawliteral";

//---------------------------------------------------------------------
// Page de configuration WiFi
//---------------------------------------------------------------------

static String escapeHtml(const String &s)
{
  String out;

  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];

    if      (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else               out += c;
  }

  return out;
}

static void handleWifiPage()
{
  String page = F("<!DOCTYPE html><html lang=\"");
  page += langTag();
  page += F("\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>WiFi - Barometre</title><style>"
            "body{margin:0;background:#0f172a;color:#e2e8f0;font:16px/1.5 system-ui,sans-serif;padding:20px}"
            "*{box-sizing:border-box}"
            "h1{font-size:20px;margin:0 0 6px}"
            ".note{color:#94a3b8;font-size:13px}"
            "form{background:#1e293b;padding:16px;border-radius:14px;max-width:420px;margin-top:12px}"
            "label{display:block;margin:0 0 12px;color:#94a3b8;font-size:13px}"
            "input{display:block;width:100%;margin-top:5px;padding:10px;border-radius:9px;"
            "border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:16px}"
            "button{padding:10px 16px;border-radius:9px;border:0;background:#38bdf8;"
            "color:#062133;font-weight:600;font-size:15px;cursor:pointer}"
            "a{color:#94a3b8}</style></head><body><h1>");
  page += T(TX_WIFI);
  page += F("</h1>");

  page += F("<p class=\"note\">");
  page += T(TX_NET_SAVED);
  page += F(" <b>");
  page += s_ssid.length() ? escapeHtml(s_ssid) : String(T(TX_NONE));
  page += F("</b></p><form method=\"post\" action=\"/save\"><label>");
  page += T(TX_SSID);
  page += F("<input name=\"ssid\" maxlength=\"31\" required value=\"");
  page += escapeHtml(s_ssid);
  page += F("\"></label><label>");
  page += T(TX_PASS);
  page += F("<input name=\"pass\" type=\"password\" maxlength=\"63\"></label><button>");
  page += T(TX_SAVE_RESTART);
  page += F("</button></form><p class=\"note\">");
  page += T(TX_WIFI_NOTE);
  page += F("</p><p><a href=\"/\">");
  page += T(TX_BACK_SHORT);
  page += F("</a></p></body></html>");

  server.send(200, "text/html; charset=utf-8", page);
}

static void handleSave()
{
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  ssid.trim();

  if (!ssid.length()) {
    server.send(400, "text/plain; charset=utf-8", T(TX_SSID_EMPTY));
    return;
  }

  prefs.begin("baro", false);
  prefs.putString("ssid", ssid);
  if (pass.length())
    prefs.putString("pass", pass);
  prefs.end();

  String page = F("<meta charset=\"utf-8\">"
                  "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<body style=\"background:#0f172a;color:#e2e8f0;font:16px system-ui;padding:24px\">"
                  "<h1 style=\"font-size:20px\">");
  page += T(TX_SAVED_TITLE);
  page += F("</h1><p>");
  page += T(TX_REBOOT_JOIN);
  page += F("<b>");
  page += escapeHtml(ssid);
  page += F("</b>.</p><p style=\"color:#94a3b8\">");
  page += T(TX_RECONNECT);
  page += F("</p></body>");

  server.send(200, "text/html; charset=utf-8", page);

  delay(800);
  ESP.restart();
}

//---------------------------------------------------------------------
// Page de reglages
//
//   GET  /params  formulaire (unite de temperature, sortie NMEA)
//   POST /params  enregistrement
//   POST /time    reglage de la date et de l'heure
//
// Les actions sur l'ecran se font avec les commandes deja en place :
//   /api/panel?display=1  reinitialise la dalle (historique conserve)
//   /api/panel?reboot=1   redemarre la carte
//---------------------------------------------------------------------

// Debits proposes pour la sortie NMEA 0183 (0 = N2K seul). Les libelles
// sont construits a l'affichage, dans la langue courante.
static const int32_t kNmeaSpeeds[] = { 0, 4800, 9600, 19200, 38400 };

#define NMEA_CHOICES ((uint8_t)(sizeof(kNmeaSpeeds) / sizeof(kNmeaSpeeds[0])))

static bool nmeaSpeedValid(int32_t speed)
{
  for (uint8_t i = 0; i < NMEA_CHOICES; i++)
    if (kNmeaSpeeds[i] == speed) return true;

  return false;
}

//---------------------------------------------------------------------

void webSettingsLoad()
{
  prefs.begin("baro", true);
  bfahrenheit = prefs.getBool("fah", false);
  speed_nmea  = prefs.getInt("nmea", 0);
  s_lang      = prefs.getUChar("lang", LANG_FR);
  prefs.end();

  if (!nmeaSpeedValid(speed_nmea)) speed_nmea = 0;
  if (s_lang > LANG_EN)            s_lang      = LANG_FR;

  ESP_LOGI(TAG, "reglages : unites %s, NMEA %d, langue %s",
           bfahrenheit ? "F" : "C", (int)speed_nmea, langTag());
}

//---------------------------------------------------------------------

static void handleParamsPage()
{
  DateTime now = rtcClockNow();
  char     stamp[20];

  snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d",
           now.year(), now.month(), now.day(), now.hour(), now.minute());

  String page = F("<!DOCTYPE html><html lang=\"");
  page += langTag();
  page += F("\"><head>"
            "<meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>");
  page += T(TX_SETTINGS);
  page += F(" - Barometre</title><style>"
            "body{margin:0;background:#0f172a;color:#e2e8f0;font:16px/1.5 system-ui,sans-serif;padding:20px;max-width:580px}"
            "*{box-sizing:border-box}"
            "h1{font-size:20px;margin:0 0 6px}"
            "h2{font-size:15px;margin:22px 0 8px;color:#38bdf8;font-weight:600}"
            ".note{color:#94a3b8;font-size:13px;margin:6px 0 0}"
            "form{background:#1e293b;padding:16px;border-radius:14px;margin-top:8px}"
            "label{display:block;margin:0 0 12px;color:#94a3b8;font-size:13px}"
            "input,select{display:block;width:100%;margin-top:5px;padding:10px;border-radius:9px;"
            "border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:16px}"
            ".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin-bottom:12px}"
            "button{padding:10px 16px;border-radius:9px;border:0;background:#38bdf8;"
            "color:#062133;font-weight:600;font-size:15px;cursor:pointer}"
            "button.warn{background:#f87171}"
            "button:disabled{opacity:.55;cursor:default}"
            ".inline{display:inline-flex;align-items:center;gap:7px;margin:0 18px 0 0;color:#e2e8f0;font-size:15px}"
            ".inline input{display:inline;width:auto;margin:0}"
            ".ok{background:#14532d;color:#bbf7d0;border-radius:10px;padding:10px 14px;margin-top:14px;font-size:14px}"
            ".ko{background:#7c2d12;color:#fed7aa;border-radius:10px;padding:10px 14px;margin-top:14px;font-size:14px}"
            "a{color:#94a3b8}"
            ".btn{display:inline-block;background:#38bdf8;color:#062133;font-weight:600;"
            "padding:11px 18px;border-radius:10px;text-decoration:none;font-size:15px}"
            "</style></head><body><h1>");
  page += T(TX_SETTINGS);
  page += F("</h1>");

  if (server.hasArg("saved")) {
    page += F("<div class=\"ok\">");
    page += T(TX_SAVED);
    page += F("</div>");
  }

  if (server.hasArg("error")) {
    page += F("<div class=\"ko\">");
    page += T(TX_BAD_TIME);
    page += F("</div>");
  }

  //--- Langue --------------------------------------------------------
  page += F("<h2>");
  page += T(TX_LANG);
  page += F("</h2><form method=\"post\" action=\"/params\"><div class=\"row\">"
            "<label class=\"inline\"><input type=\"radio\" name=\"lang\" value=\"fr\"");
  if (s_lang == LANG_FR) page += F(" checked");
  page += F("> Fran&ccedil;ais</label>"
            "<label class=\"inline\"><input type=\"radio\" name=\"lang\" value=\"en\"");
  if (s_lang == LANG_EN) page += F(" checked");
  page += F("> English</label></div><button>");
  page += T(TX_SAVE);
  page += F("</button></form>");

  //--- Ecran ---------------------------------------------------------
  page += F("<h2>");
  page += T(TX_SCREEN);
  page += F("</h2><form><div class=\"row\"><button type=\"button\""
            " onclick=\"act('/api/panel?display=1','");
  page += T(TX_MSG_DISPLAY_OK);
  page += F("',this)\">");
  page += T(TX_RESTART_DISP);
  page += F("</button><button type=\"button\" class=\"warn\" onclick=\"reboot()\">");
  page += T(TX_REBOOT);
  page += F("</button></div><p class=\"note\">");
  page += T(TX_SCREEN_NOTE);
  page += F("</p><p class=\"note\" id=\"msg\"></p></form>");

  //--- Unites --------------------------------------------------------
  page += F("<h2>");
  page += T(TX_UNITS);
  page += F("</h2><form method=\"post\" action=\"/params\"><label>");
  page += T(TX_TEMPERATURE);
  page += F("</label><div class=\"row\">"
            "<label class=\"inline\"><input type=\"radio\" name=\"unit\" value=\"c\"");
  if (!bfahrenheit) page += F(" checked");
  page += F("> Celsius (&deg;C)</label>"
            "<label class=\"inline\"><input type=\"radio\" name=\"unit\" value=\"f\"");
  if (bfahrenheit) page += F(" checked");
  page += F("> Fahrenheit (&deg;F)</label></div><button>");
  page += T(TX_SAVE);
  page += F("</button></form>");

  //--- Sortie NMEA ---------------------------------------------------
  page += F("<h2>");
  page += T(TX_NMEA);
  page += F("</h2><form method=\"post\" action=\"/params\"><label>");
  page += T(TX_NMEA_PORT);
  page += F("<select name=\"nmea\">");

  for (uint8_t i = 0; i < NMEA_CHOICES; i++) {
    page += F("<option value=\"");
    page += String(kNmeaSpeeds[i]);
    page += F("\"");

    if (kNmeaSpeeds[i] == speed_nmea) page += F(" selected");

    page += F(">");

    if (kNmeaSpeeds[i] == 0) {
      page += T(TX_NMEA_ONLY);
    } else {
      page += F("NMEA 0183 - ");
      page += String(kNmeaSpeeds[i]);
      page += T(TX_BAUD);
    }

    page += F("</option>");
  }

  page += F("</select></label><p class=\"note\">");
  page += T(TX_NMEA_NOTE);
  page += F("</p><button>");
  page += T(TX_SAVE);
  page += F("</button></form>");

  //--- Reseau WiFi ---------------------------------------------------
  page += F("<h2>");
  page += T(TX_WIFI);
  page += F("</h2><form><p class=\"note\" style=\"margin-top:0\">");
  page += T(TX_NET_SAVED);
  page += F(" <b>");
  page += s_ssid.length() ? escapeHtml(s_ssid) : String(T(TX_NONE));
  page += F("</b><br>");

  if (WiFi.status() == WL_CONNECTED) {
    page += T(TX_CONNECTED);
    page += WiFi.localIP().toString();
  } else {
    page += T(TX_AP_MODE);
  }

  page += F("</p><a class=\"btn\" href=\"/wifi\">");
  page += T(TX_CONFIGURE);
  page += F("</a></form>");

  //--- Date et heure -------------------------------------------------
  page += F("<h2>");
  page += T(TX_DATETIME);
  page += F("</h2><form method=\"post\" action=\"/time\"><label>");
  page += T(TX_CLOCK);
  page += F("<input type=\"datetime-local\" name=\"dt\" id=\"dt\" value=\"");
  page += stamp;
  page += F("\"></label><div class=\"row\"><button>");
  page += T(TX_SET_CLOCK);
  page += F("</button><button type=\"button\" onclick=\"nowTime()\">");
  page += T(TX_THIS_TIME);
  page += F("</button></div>");

  if (rtcClockIsHardware()) {
    page += F("<p class=\"note\">");
    page += rtcClockLostPower() ? T(TX_RTC_LOST) : T(TX_RTC_HW_OK);
    page += F("</p>");
  } else {
    page += F("<p class=\"note\">");
    page += T(TX_RTC_SOFT);
    page += F("</p>");
  }

  page += F("</form><p class=\"note\"><a href=\"/\">");
  page += T(TX_BACK);
  page += F("</a></p>");

  page += F("<script>"
            "function act(url,ok,btn){"
            "var m=document.getElementById('msg');"
            "m.textContent=\"");
  page += T(TX_MSG_BUSY);
  page += F("\";if(btn)btn.disabled=true;"
            "fetch(url,{cache:'no-store'}).then(function(r){return r.json();})"
            ".then(function(d){var bad=(d.err!==undefined&&d.err!==0);"
            "m.textContent=bad?(\"");
  page += T(TX_MSG_FAIL);
  page += F("\"+d.err):ok;if(btn)btn.disabled=false;})"
            ".catch(function(){m.textContent=\"");
  page += T(TX_MSG_NORESP);
  page += F("\";if(btn)btn.disabled=false;});}"
            "function reboot(){"
            "if(!confirm(\"");
  page += T(TX_CONFIRM_REBOOT);
  page += F("\"))return;"
            "document.getElementById('msg').textContent=\"");
  page += T(TX_MSG_REBOOT);
  page += F("\";"
            "fetch('/api/panel?reboot=1',{cache:'no-store'}).catch(function(){});}"
            "function nowTime(){var d=new Date();"
            "function p(n){return(n<10?'0':'')+n;}"
            "document.getElementById('dt').value=d.getFullYear()+'-'+p(d.getMonth()+1)+'-'"
            "+p(d.getDate())+'T'+p(d.getHours())+':'+p(d.getMinutes());}"
            "</script></body></html>");

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", page);
}

//---------------------------------------------------------------------

static void handleParamsSave()
{
  if (server.hasArg("lang"))
    s_lang = (server.arg("lang") == "en") ? LANG_EN : LANG_FR;

  if (server.hasArg("unit"))
    bfahrenheit = (server.arg("unit") == "f");

  if (server.hasArg("nmea")) {
    int32_t speed = server.arg("nmea").toInt();

    if (nmeaSpeedValid(speed)) speed_nmea = speed;
  }

  prefs.begin("baro", false);
  prefs.putUChar("lang", s_lang);
  prefs.putBool("fah", bfahrenheit);
  prefs.putInt("nmea", speed_nmea);
  prefs.end();

  uiSettingsChanged();          // menu tactile + redessin de la page
  Screen::restartPanel();       // l'ecriture en flash a pu bousculer la dalle

  server.sendHeader("Location", "/params?saved=1");
  server.send(303, "text/plain", "");
}

//---------------------------------------------------------------------

static void handleTimeSet()
{
  String dt = server.arg("dt");
  int    y = 0, mo = 0, d = 0, h = 0, mi = 0;

  bool ok = (sscanf(dt.c_str(), "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) == 5);

  if (ok) {
    DateTime t((uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, (uint8_t)mi, 0);

    ok = t.isValid() && rtcClockAdjust(t);
  }

  if (ok) {
    uiSettingsChanged();        // l'heure s'affiche sur la page de mesure
    server.sendHeader("Location", "/params?saved=1");
  } else {
    server.sendHeader("Location", "/params?error=1");
  }

  server.send(303, "text/plain", "");
}

//---------------------------------------------------------------------
// API JSON
//---------------------------------------------------------------------

static void handleRoot()
{
  // La page est ecrite une seule fois (en francais) : les textes sont
  // remplaces ici par leur equivalent dans la langue choisie.
  String page = PAGE_INDEX;

  page.replace("{{LANG}}",        langTag());
  page.replace("{{SETTINGS}}",    T(TX_SETTINGS));
  page.replace("{{AP_HINT}}",     T(TX_AP_HINT));
  page.replace("{{CFG_WIFI}}",    T(TX_CFG_WIFI));
  page.replace("{{PRESSURE}}",    T(TX_PRESSURE));
  page.replace("{{TEMPERATURE}}", T(TX_TEMPERATURE));
  page.replace("{{HUMIDITY}}",    T(TX_HUMIDITY));
  page.replace("{{HIST_TITLE}}",  T(TX_HIST_TITLE));
  page.replace("{{CONNECTING}}",  T(TX_CONNECTING));
  page.replace("{{AP_PREFIX}}",   T(TX_AP_PREFIX));
  page.replace("{{OFFLINE}}",     T(TX_OFFLINE));
  page.replace("{{EMPTY_HIST}}",  T(TX_EMPTY_HIST));
  page.replace("{{DEC}}",         T(TX_DEC));

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", page);
}

static void handleNotFound()
{
  server.send(404, "text/plain; charset=utf-8", T(TX_NADA));
}

static void handleData()
{
  DateTime now = rtcClockNow();
  char when[24];
  bool sta = (WiFi.status() == WL_CONNECTED);

  snprintf(when, sizeof(when), "%02d/%02d/%04d %02d:%02d:%02d",
           now.day(), now.month(), now.year(),
           now.hour(), now.minute(), now.second());

  String j = "{";

  j += "\"ok\":"   + String(bme280_ok ? 1 : 0);
  j += ",\"p\":"   + String(pres, 2);
  j += ",\"t\":"   + String(tempC, 1);
  j += ",\"h\":"   + String(humidity);
  j += ",\"fh\":"  + String(bfahrenheit ? 1 : 0);
  j += ",\"mode\":" + String(history_mode);
  j += ",\"time\":\"" + String(when) + "\"";
  j += ",\"sta\":" + String(sta ? 1 : 0);
  j += ",\"ip\":\"" + (sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\"";
  j += ",\"rssi\":" + String(sta ? WiFi.RSSI() : 0);
  j += "}";

  server.send(200, "application/json", j);
}

static void handleHistory()
{
  int8_t hours = server.hasArg("h") ? (int8_t)server.arg("h").toInt() : MODE_06H;

  if (hours != MODE_06H && hours != MODE_12H && hours != MODE_24H && hours != MODE_96H)
    hours = MODE_06H;

  int16_t n = barometerHistCount();

  String j = "{";
  j += "\"h\":"     + String(hours);
  j += ",\"step\":" + String(barometerHistStep(hours));
  j += ",\"n\":"    + String(n);
  j += ",\"stamp\":" + String(barometerHistStamp(hours));
  j += ",\"tod\":"   + String(barometerHistTimeOfDay(hours));

  int16_t plo, phi;
  barometerHistPressureScale(hours, &plo, &phi);
  j += ",\"pmin\":" + String(plo);
  j += ",\"pmax\":" + String(phi);

  const HistSeriesKind kinds[3] = { HIST_PRESSURE, HIST_TEMPERATURE, HIST_HUMIDITY };
  const char          *names[3] = { "pres", "temp", "hum" };

  for (int8_t k = 0; k < 3; k++) {
    j += ",\"";
    j += names[k];
    j += "\":[";

    for (int16_t i = 0; i < n; i++) {
      float v = barometerHistValue(kinds[k], hours, i);

      if (i) j += ",";

      if (isnan(v))            j += "null";
      else if (k == 0)         j += String((int)lroundf(v));       // hPa
      else if (k == 1)         j += String(v, 1);                  // degres C
      else                     j += String((int)lroundf(v));       // %HR
    }

    j += "]";
  }

  j += "}";

  server.send(200, "application/json", j);
}

//---------------------------------------------------------------------
// Diagnostic : etat de la dalle (bounce buffer) et de la liaison WiFi
//---------------------------------------------------------------------

static void handleDiag()
{
  uint32_t frames = 0, stalls = 0, maxUs = 0;

  Screen::frameStats(&frames, &stalls, &maxUs);

  // Relecture directe des reglages en NVS (diagnostic).
  prefs.begin("baro", true);
  bool    nvsFah  = prefs.getBool("fah", false);
  int32_t nvsNmea = prefs.getInt("nmea", -1);
  prefs.end();

  String j = "{";

  j += "\"bounce\":" + String(Screen::bounceBufferActive() ? 1 : 0);
  j += ",\"pclk\":"  + String(Screen::pixelClockHz());
  j += ",\"lcd\":"   + String(Screen::ready() ? 1 : 0);
  j += ",\"heap\":"  + String((unsigned)ESP.getFreeHeap());
  j += ",\"psram\":" + String((unsigned)ESP.getFreePsram());
  j += ",\"sta\":"   + String(WiFi.status() == WL_CONNECTED ? 1 : 0);
  j += ",\"rssi\":"  + String(WiFi.RSSI());
  j += ",\"tx\":"    + String((int)WiFi.getTxPower());
  j += ",\"sleep\":" + String(WiFi.getSleep() ? 1 : 0);
  j += ",\"frames\":" + String((unsigned)frames);
  j += ",\"stalls\":" + String((unsigned)stalls);
  j += ",\"maxdt\":"  + String((unsigned)maxUs);
  j += ",\"disc\":"   + String((unsigned)s_staDisconnects);
  j += ",\"nvsfah\":" + String(nvsFah ? 1 : 0);
  j += ",\"nvsnmea\":" + String((int)nvsNmea);
  j += "}";

  server.send(200, "application/json", j);
}

//---------------------------------------------------------------------
// Diagnostic : coupe la radio pendant N secondes (puis la rallume toute
// seule), pour voir si les defauts d'affichage disparaissent.
// Exemple : http://192.168.4.1/api/radio?off=30
//---------------------------------------------------------------------

static uint32_t s_radioOffAt = 0;         // echeance de rallumage (0 = inactif)

static void handleRadio()
{
  long sec = server.hasArg("off") ? server.arg("off").toInt() : 0;

  if (sec < 0)   sec = 0;
  if (sec > 120) sec = 120;

  if (sec == 0) {
    server.send(200, "application/json", "{\"radio\":\"on\"}");
    return;
  }

  s_radioOffAt = millis() + (uint32_t)sec * 1000UL;
  if (s_radioOffAt == 0) s_radioOffAt = 1;

  server.send(200, "application/json", "{\"radio\":\"off\",\"secondes\":" + String(sec) + "}");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

//---------------------------------------------------------------------
// Diagnostic / depannage de la dalle.
//   http://192.168.0.22/api/panel?display=1     reinitialise l'affichage
//                                               (garde mesures et historique)
//   http://192.168.0.22/api/panel?restart=1     resynchronisation du DMA
//   http://192.168.0.22/api/panel?pclk=14000000 frequence pixel a chaud
//   http://192.168.0.22/api/panel?reboot=1      redemarrage de la carte
//---------------------------------------------------------------------

static void handlePanel()
{
  if (server.hasArg("display")) {
    bool ok = Screen::reinit();

    server.send(200, "application/json",
                "{\"display\":" + String(ok ? 1 : 0) + ",\"lcd\":"
                + String(Screen::ready() ? 1 : 0) + "}");
    return;
  }

  if (server.hasArg("reboot")) {
    server.send(200, "application/json", "{\"reboot\":1}");

    delay(300);
    ESP.restart();
  }

  if (server.hasArg("pclk")) {
    uint32_t hz  = (uint32_t)server.arg("pclk").toInt();
    int      err = Screen::setPixelClock(hz);

    server.send(200, "application/json",
                "{\"pclk\":" + String(Screen::pixelClockHz()) + ",\"err\":" + String(err) + "}");
    return;
  }

  Screen::restartPanel();
  server.send(200, "application/json",
              "{\"restart\":1,\"err\":" + String(Screen::restartError()) + "}");
}

//---------------------------------------------------------------------
// Mise en route
//---------------------------------------------------------------------

// Puissance d'emission et modem sleep : a reappliquer apres chaque
// changement de mode ou de connexion, car la radio repart sinon avec ses
// valeurs par defaut (20 dBm, ce qui perturbe fortement la dalle RGB).
//
// WIFI_PS_MAX_MODEM : la radio dort entre deux fenetres d'ecoute, au lieu de
// se reveiller toutes les 100 ms. Chaque reveil occupe le bus memoire et peut
// retarder la dalle ; les espacer divise ces a-coups. En contrepartie la page
// web repond avec quelques centaines de millisecondes de latence.
static void applyRadioSettings()
{
  WiFi.setSleep(WIFI_PS_MAX_MODEM);
  WiFi.setTxPower(WIFI_TX_POWER);
}

static void startAccessPoint()
{
  WiFi.mode(WIFI_AP_STA);

  if (!WiFi.softAP(WIFI_AP_NAME, WIFI_AP_PASS))
    ESP_LOGE(TAG, "WiFi : echec du demarrage du point d'acces");

  applyRadioSettings();

  s_apStarted = true;

  ESP_LOGI(TAG, "WiFi : point d'acces \"%s\" (mot de passe %s) -> http://%s/",
        WIFI_AP_NAME, WIFI_AP_PASS, WiFi.softAPIP().toString().c_str());
}

void webInit()
{
  prefs.begin("baro", true);
  s_ssid = prefs.getString("ssid", "");
  s_pass = prefs.getString("pass", "");
  prefs.end();

  WiFi.persistent(false);
  // Configuration WiFi gardee en RAM : aucune ecriture en flash (NVS) au
  // demarrage de la radio, donc aucun blocage du bus memoire a ce moment-la.
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  WiFi.onEvent(onNetworkEvent);          // compte les coupures de liaison

  // Le WiFi perturbe la dalle RGB (lignes qui clignotent, image decalee) :
  // puissance d'emission reduite et modem sleep actif (voir
  // applyRadioSettings(), reappelee apres chaque changement de mode).
  WiFi.setSleep(true);

  if (s_ssid.length()) {
    s_staConfigured = true;

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(s_ssid.c_str(), s_pass.c_str());
    applyRadioSettings();

    s_staStart  = millis();
    s_lastRetry = millis();

    ESP_LOGI(TAG, "WiFi : connexion a \"%s\"...", s_ssid.c_str());
  } else {
    ESP_LOGI(TAG, "WiFi : aucun reseau enregistre, demarrage du point d'acces.");
    startAccessPoint();
  }


  server.on("/",            handleRoot);
  server.on("/api/data",    handleData);
  server.on("/api/history", handleHistory);
  server.on("/api/diag",    handleDiag);
  server.on("/api/radio",   handleRadio);
  server.on("/api/panel",   handlePanel);
  server.on("/wifi",        handleWifiPage);
  server.on("/params",      HTTP_GET,  handleParamsPage);
  server.on("/params",      HTTP_POST, handleParamsSave);
  server.on("/time",        HTTP_POST, handleTimeSet);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();

  ESP_LOGI(TAG, "Interface web : port 80");
}

void webLoop()
{
  // Radio coupee pour le diagnostic : on la rallume a l'echeance.
  if (s_radioOffAt && (int32_t)(millis() - s_radioOffAt) >= 0) {
    s_radioOffAt = 0;

    if (s_staConfigured) {
      WiFi.mode(WIFI_STA);
      WiFi.setHostname(WIFI_HOSTNAME);
      WiFi.begin(s_ssid.c_str(), s_pass.c_str());
      applyRadioSettings();
    } else {
      startAccessPoint();
    }
    s_staStart  = millis();
    s_lastRetry = millis();
    return;
  }

  if (s_radioOffAt)
    return;

  server.handleClient();

  // Etat de la liaison, une fois toutes les dix secondes. Sert au diagnostic
  // quand la page n'est pas joignable (les traces Arduino partent sur le
  // port USB et sont perdues si personne ne l'ecoute).
  static uint32_t lastLog = 0;

  if (millis() - lastLog > 10000) {
    lastLog = millis();

    String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString()
                                                : WiFi.softAPIP().toString();

    ESP_LOGI(TAG, "etat : sta=%d ap=%d ip=%s heap=%u",
             (int)WiFi.status(), (int)s_apStarted, ip.c_str(),
             (unsigned)ESP.getFreeHeap());
  }

  if (!s_staConfigured)
    return;

  if (WiFi.status() == WL_CONNECTED) {
    if (!s_mdnsStarted) {
      if (MDNS.begin(WIFI_HOSTNAME))
        MDNS.addService("http", "tcp", 80);

      s_mdnsStarted = true;

      ESP_LOGI(TAG, "WiFi connecte : http://%s/  (http://%s.local/)",
                    WiFi.localIP().toString().c_str(), WIFI_HOSTNAME);
    }
    return;
  }

  // Pas (ou plus) connecte : point d'acces de secours apres l'attente, et
  // nouvel essai de connexion a intervalle regulier.
  if (!s_apStarted && millis() - s_staStart > WIFI_STA_WAIT)
    startAccessPoint();

  if (millis() - s_lastRetry > WIFI_RETRY) {
    s_lastRetry = millis();
    WiFi.begin(s_ssid.c_str(), s_pass.c_str());
    applyRadioSettings();
  }
}
