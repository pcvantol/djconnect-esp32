// HTTP dashboard for inspecting and updating the Spotify remote from a phone.
#include "WebPortal.h"

#include "AppLog.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <new>

#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "ScopedWatchdogPause.h"
#include "TextHelpers.h"
#include "assets/djconnect_favicon_ico.h"
#include "assets/djconnect_icon_192_png.h"
#include "assets/djconnect_site_webmanifest.h"

#ifndef WEB_SHOW_WIFI_PASSWORD
#define WEB_SHOW_WIFI_PASSWORD 0
#endif

namespace {
int hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

String decodeFormValue(const String &value) {
  String decoded;
  decoded.reserve(value.length());
  for (size_t index = 0; index < value.length(); index++) {
    const char c = value[index];
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%' && index + 2 < value.length()) {
      const int high = hexNibble(value[index + 1]);
      const int low = hexNibble(value[index + 2]);
      if (high >= 0 && low >= 0) {
        decoded += static_cast<char>((high << 4) | low);
        index += 2;
      } else {
        decoded += c;
      }
    } else {
      decoded += c;
    }
  }
  return decoded;
}

void serviceWebOtaLoop(LedRing *ledRing) {
  ScopedWatchdogPause::resetIfAttached();
  if (ledRing != nullptr) {
    ledRing->showFirmwareUpdateAnimation();
  }
  delay(1);
  yield();
}

String formValueFromBody(const String &body, const char *key) {
  const String prefix = String(key) + "=";
  size_t start = 0;
  while (start < body.length()) {
    size_t end = body.indexOf('&', start);
    if (end == static_cast<size_t>(-1)) {
      end = body.length();
    }
    const String pair = body.substring(start, end);
    if (pair.startsWith(prefix)) {
      return decodeFormValue(pair.substring(prefix.length()));
    }
    start = end + 1;
  }
  return "";
}

String jsonValueFromBody(const String &body, const char *key) {
  if (body.isEmpty()) {
    return "";
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    return "";
  }
  return doc[key] | "";
}

String postedValue(WebServer &server, const String &rawBody, const char *primaryKey, const char *fallbackKey = nullptr) {
  if (server.hasArg(primaryKey)) {
    return server.arg(primaryKey);
  }
  String value = jsonValueFromBody(rawBody, primaryKey);
  if (!value.isEmpty()) {
    return value;
  }
  value = formValueFromBody(rawBody, primaryKey);
  if (!value.isEmpty() || fallbackKey == nullptr) {
    return value;
  }
  if (server.hasArg(fallbackKey)) {
    return server.arg(fallbackKey);
  }
  value = jsonValueFromBody(rawBody, fallbackKey);
  if (!value.isEmpty()) {
    return value;
  }
  return formValueFromBody(rawBody, fallbackKey);
}

String jsonEscaped(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", "\\n");
  value.replace("\r", "\\r");
  value.replace("\t", "\\t");
  return value;
}

void sendJsonEscapedContent(WebServer &server, const String &value) {
  const String escaped = jsonEscaped(value);
  if (escaped.length() > 0) {
    server.sendContent(escaped);
  }
}

void sendNoStore(WebServer &server) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
}

void sendStaticCache(WebServer &server, uint32_t maxAgeSeconds) {
  server.sendHeader("Cache-Control", "public, max-age=" + String(maxAgeSeconds) + ", immutable");
}

void sendProgmemAsset(
    WebServer &server,
    const char *contentType,
    const uint8_t *data,
    size_t length,
    uint32_t maxAgeSeconds = 604800UL) {
  sendStaticCache(server, maxAgeSeconds);
  server.send_P(200, contentType, reinterpret_cast<const char *>(data), length);
}
}  // namespace

static const char IndexHtml[] PROGMEM = R"rawliteral(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta name="theme-color" content="#080b0c"><meta name="mobile-web-app-capable" content="yes"><meta name="apple-mobile-web-app-capable" content="yes"><meta name="apple-mobile-web-app-title" content="DJConnect"><meta name="apple-mobile-web-app-status-bar-style" content="black-translucent"><meta name="application-name" content="DJConnect"><link rel="shortcut icon" href="/favicon.ico?v=3" sizes="any"><link rel="icon" href="/favicon.ico?v=3" sizes="any"><link rel="icon" type="image/png" sizes="192x192" href="/icon-192.png?v=3"><link rel="apple-touch-icon" sizes="180x180" href="/apple-touch-icon.png?v=3"><link rel="apple-touch-icon" sizes="192x192" href="/icon-192.png?v=3"><link rel="apple-touch-icon-precomposed" sizes="180x180" href="/apple-touch-icon-precomposed.png?v=3"><link rel="apple-touch-icon-precomposed" sizes="192x192" href="/icon-192.png?v=3"><link rel="manifest" href="/site.webmanifest?v=2"><title>DJConnect</title><style>:root{color-scheme:light dark;--bg:#070a14;--panel:#101522;--muted:#9aa5bd;--line:#27304a;--green:#1ed760;--yellow:#d6b84a;--orange:#ff9f1a;--blue:#2f8cff;--purple:#9b72ff;--purple-soft:#c9b8ff;--playback-lila:#d733f2;--playback-lila-deep:#9b2ee8;--red:#ff735d;--text:#f5f7ff;--header:rgba(7,10,20,.95);--field:#0b1020;--bar-bg:#0a0f1c;--art-bg:#050816;--row-line:rgba(159,183,255,.10);--log-bg:#050816;--log-text:#cad3ea}html[data-theme="light"]{--bg:#f5f7ff;--panel:#ffffff;--muted:#63708e;--line:#d7ddf0;--green:#148a3c;--yellow:#8f710f;--orange:#c86f00;--blue:#1f6fd1;--purple:#7046d8;--purple-soft:#5f46a7;--red:#b23a31;--text:#12182a;--header:rgba(245,247,255,.96);--field:#ffffff;--bar-bg:#e9edf8;--art-bg:#eef2ff;--row-line:rgba(18,24,42,.09);--log-bg:#f1f3fb;--log-text:#25304a}html[data-theme="dark"]{--bg:#070a14;--panel:#101522;--muted:#9aa5bd;--line:#27304a;--green:#1ed760;--yellow:#d6b84a;--orange:#ff9f1a;--blue:#2f8cff;--purple:#9b72ff;--purple-soft:#c9b8ff;--red:#ff735d;--text:#f5f7ff;--header:rgba(7,10,20,.95);--field:#0b1020;--bar-bg:#0a0f1c;--art-bg:#050816;--row-line:rgba(159,183,255,.10);--log-bg:#050816;--log-text:#cad3ea}@media (prefers-color-scheme:light){html[data-theme="auto"]{--bg:#f5f7ff;--panel:#ffffff;--muted:#63708e;--line:#d7ddf0;--green:#148a3c;--yellow:#8f710f;--orange:#c86f00;--blue:#1f6fd1;--purple:#7046d8;--purple-soft:#5f46a7;--red:#b23a31;--text:#12182a;--header:rgba(245,247,255,.96);--field:#ffffff;--bar-bg:#e9edf8;--art-bg:#eef2ff;--row-line:rgba(18,24,42,.09);--log-bg:#f1f3fb;--log-text:#25304a}}*{box-sizing:border-box}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}header{position:sticky;top:0;z-index:2;background:linear-gradient(135deg,rgba(47,140,255,.16),rgba(155,114,255,.18)),var(--header);border-bottom:1px solid var(--line);padding:16px}h1{margin:0;font-size:22px;letter-spacing:0;display:flex;align-items:center;gap:8px;min-width:0}.brand-icon{width:28px;height:28px;border-radius:6px}.brand-copy{display:flex;flex-direction:column;gap:2px;min-width:0}.brand-line{display:flex;align-items:center;gap:8px;min-width:0;flex-wrap:wrap}.brand-tagline{color:var(--purple-soft);font-size:13px;line-height:1.15;font-weight:700}.model-chip{color:var(--muted);font-size:12px;font-weight:700;max-width:170px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.sub{color:var(--muted);font-size:13px;margin-top:4px}.header-status{display:flex;justify-content:flex-end;align-items:center;gap:8px}.header-website{color:var(--purple-soft);font-size:12px;font-weight:800;text-decoration:none;border:1px solid rgba(155,114,255,.38);border-radius:999px;padding:2px 8px;line-height:1.35}.header-website:hover,.header-website:focus-visible{color:var(--text);border-color:var(--purple);outline:none}main{padding:12px;display:grid;gap:12px;max-width:960px;margin:0 auto}.panel{background:linear-gradient(180deg,rgba(47,140,255,.06),rgba(155,114,255,.04)),var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px}h2{margin:0 0 10px;font-size:15px;color:var(--purple-soft);font-weight:700}.hero-title{font-size:26px;line-height:1.08;font-weight:750;margin:2px 0 8px;overflow-wrap:anywhere}.artist{color:#d8e3df;font-size:18px;margin-bottom:12px;overflow-wrap:anywhere}.now{display:grid;grid-template-columns:96px 1fr;gap:12px;align-items:start;margin-bottom:12px}.now.no-art{grid-template-columns:1fr}.album-art{width:96px;height:96px;border-radius:8px;border:1px solid var(--line);object-fit:cover;background:var(--art-bg);display:none;cursor:pointer}.album-art:focus-visible{outline:2px solid var(--purple);outline-offset:3px}.grid{display:grid;gap:8px;grid-template-columns:1fr}.row{display:flex;justify-content:space-between;gap:12px;border-top:1px solid var(--row-line);padding-top:8px;font-size:14px}.row:first-child{border-top:0;padding-top:0}.key{color:var(--muted);min-width:110px}.value{text-align:right;overflow-wrap:anywhere}.signal{display:inline-flex;align-items:flex-end;gap:2px;min-width:22px;height:16px;vertical-align:middle}.signal i{display:block;width:4px;border-radius:2px 2px 0 0;background:#293436}.signal i:nth-child(1){height:5px}.signal i:nth-child(2){height:8px}.signal i:nth-child(3){height:11px}.signal i:nth-child(4){height:14px}.signal.level-1 i:nth-child(-n+1){background:#ff6f61}.signal.level-2 i:nth-child(-n+2){background:#f3d37b}.signal.level-3 i:nth-child(-n+3),.signal.level-4 i:nth-child(-n+4){background:var(--green)}.status-icons{display:inline-flex;gap:5px;vertical-align:middle}.status-dot{display:inline-flex;align-items:center;justify-content:center;width:18px;height:18px;border-radius:50%;border:1px solid var(--red);color:var(--red);font-size:11px;font-weight:800;line-height:1}.status-dot.ok{border-color:var(--green);color:var(--green)}.status-dot.idle{border-color:var(--muted);color:var(--muted)}.pill{display:inline-flex;align-items:center;min-height:24px;border-radius:999px;padding:2px 10px;background:rgba(47,140,255,.18);color:#b8d8ff;font-size:13px}.pill.ok{background:rgba(30,215,96,.22);color:#c9ffd9}.pill.warn{background:rgba(155,114,255,.20);color:var(--purple-soft)}.pill.bad{background:#421b17;color:#ffb4aa}.bar{height:8px;border:1px solid #39484a;border-radius:999px;overflow:hidden;background:var(--bar-bg)}.bar>i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--blue),var(--purple))}.controls{display:grid;gap:10px}label{display:grid;gap:5px;color:var(--muted);font-size:13px}select,button,input:not([type]),input[type=text],input[type=password],input[type=file]{width:100%;min-height:42px;border-radius:8px;border:1px solid var(--line);background:var(--field);color:var(--text);padding:8px 10px;font-size:15px}input[type=range]{width:100%;accent-color:var(--green)}input.volume-slider{accent-color:var(--playback-lila)}.volume-value,.volume-label{color:var(--playback-lila)}button{background:linear-gradient(135deg,var(--blue),var(--purple));border-color:rgba(201,184,255,.78);color:#fff;font-weight:700;cursor:pointer;box-shadow:inset 0 -1px 0 rgba(0,0,0,.25)}.compact-actions{display:grid;grid-template-columns:repeat(5,46px);justify-content:start;gap:8px;align-items:center}.playback-volume{grid-column:1 / -1;display:grid;gap:6px;margin-top:2px}.playback-volume .volume-row{display:flex;justify-content:space-between;gap:10px;font-size:13px}button.icon-button{width:46px;height:46px;min-width:46px;min-height:46px;display:inline-flex;align-items:center;justify-content:center;padding:0;border-radius:8px;color:#f3fff7}button.icon-button svg{width:22px;height:22px;stroke:currentColor;fill:none;stroke-width:2.25;stroke-linecap:round;stroke-linejoin:round}button.icon-button .icon-pause,button.icon-button.is-playing .icon-play{display:none}button.icon-button.is-playing .icon-pause{display:block}button.icon-button.is-off{background:#243238;border-color:#3d5660;color:#a8b3af}button.icon-button.is-on{background:linear-gradient(135deg,var(--blue),var(--purple));border-color:var(--purple-soft);color:#fff}button.icon-button.is-track{background:var(--purple);border-color:var(--purple-soft);color:#fff}.playback-actions button.icon-button{background:linear-gradient(135deg,var(--playback-lila),var(--playback-lila-deep));border-color:rgba(231,139,255,.86);color:#fff}.playback-actions button.icon-button.is-off{background:#222733;border-color:#3b4254;color:#aeb7c8}.playback-actions button.icon-button.is-on,.playback-actions button.icon-button.is-track{background:linear-gradient(135deg,var(--playback-lila),var(--playback-lila-deep));border-color:rgba(231,139,255,.86);color:#fff}button.secondary{background:#243238;border-color:#3d5660;color:#f0f6f4}button.warning{background:#a57912;border-color:#d6a329;color:#fff3c4}button.firmware{background:#6f3bd8;border-color:#9b72ff;color:#f4edff}button.ptt{width:auto;min-width:160px;min-height:40px;justify-self:start;background:linear-gradient(135deg,var(--blue),var(--purple));border-color:var(--purple-soft);color:#fff;font-size:14px}button.ptt.recording{background:linear-gradient(135deg,#1f6fd1,#7046d8);border-color:#c9b8ff;color:#fff}button:disabled,select:disabled,input:disabled{opacity:.45;cursor:not-allowed}.section-action{margin-top:10px}button.danger{background:#3a1714;border-color:#632b25;color:#ffd1c9}.two{display:grid;grid-template-columns:1fr 1fr;gap:10px}.playback-actions{margin-top:12px}.queue{display:grid;gap:8px}#queueList,#playlistList{max-height:320px;overflow-y:auto;padding-right:6px;overscroll-behavior:contain;scrollbar-color:rgba(201,184,255,.72) rgba(155,114,255,.16);scrollbar-width:thin}#queueList::-webkit-scrollbar,#playlistList::-webkit-scrollbar{width:8px}#queueList::-webkit-scrollbar-track,#playlistList::-webkit-scrollbar-track{background:rgba(155,114,255,.14);border-radius:999px}#queueList::-webkit-scrollbar-thumb,#playlistList::-webkit-scrollbar-thumb{background:rgba(201,184,255,.72);border-radius:999px}.queue-item{border-top:1px solid var(--row-line);padding-top:8px;display:grid;grid-template-columns:42px 1fr 38px;gap:10px;align-items:center}.queue-item:first-child{border-top:0;padding-top:0}.queue-art{width:42px;height:42px;border-radius:6px;border:1px solid var(--line);background:var(--art-bg);object-fit:cover}.queue-art.empty{display:flex;align-items:center;justify-content:center;color:var(--muted);font-size:17px}.queue-title{font-size:14px;color:var(--text);overflow-wrap:anywhere}.queue-subtitle{margin-top:2px;font-size:12px;color:var(--muted);overflow-wrap:anywhere}button.queue-play{width:38px;height:38px;min-width:38px;min-height:38px;justify-self:end}button.queue-play svg{width:18px;height:18px}.section-title-row{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}.section-title-row h2{margin:0}button.header-icon{width:38px;height:38px;min-width:38px;min-height:38px}button.header-icon svg{width:18px;height:18px}.fine{color:var(--muted);font-size:12px;line-height:1.35}.fine+.fine,button+.fine,form+.fine{margin-top:10px}.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px}.status{margin-top:8px;color:#b7c5c1;font-size:13px;min-height:18px}.status.alert{color:#ffdf5d;font-weight:750}.status.error{color:#ff8a78;font-weight:750}.wifi-grid{margin-bottom:14px}.header-battery{display:inline-flex;align-items:center;justify-content:center;position:relative;width:58px;height:22px;border:1px solid currentColor;border-radius:4px;color:var(--green);font-size:11px;font-weight:800;line-height:1;vertical-align:middle;overflow:visible}.header-battery::after{content:"";position:absolute;right:-5px;top:6px;width:3px;height:9px;border-radius:0 2px 2px 0;background:currentColor}.header-battery .battery-fill{position:absolute;left:2px;top:2px;bottom:2px;width:0;border-radius:2px;background:currentColor;opacity:.24;transition:width .25s ease,color .25s ease}.header-battery .battery-text{position:relative;z-index:1;color:var(--text);text-shadow:0 1px 2px rgba(0,0,0,.65)}.header-battery .battery-flash{display:none;position:absolute;right:4px;top:2px;z-index:1;color:var(--yellow);font-size:12px;text-shadow:0 1px 2px rgba(0,0,0,.65)}.header-battery.charging .battery-flash{display:block;animation:batteryPulse 1s ease-in-out infinite}.header-battery.charging .battery-text{padding-right:12px}.header-battery.low{color:var(--red)}.header-battery.medium{color:var(--yellow)}.header-battery.high{color:var(--green)}@keyframes batteryPulse{0%,100%{opacity:.45;transform:scale(.92)}50%{opacity:1;transform:scale(1.08)}}.pair-banner{display:none;grid-template-columns:30px 1fr;gap:12px;align-items:start;margin:10px;padding:14px;border:1px solid rgba(155,114,255,.50);border-radius:8px;background:linear-gradient(135deg,rgba(47,140,255,.18),rgba(155,114,255,.18));color:var(--text)}.pair-banner.visible{display:grid}.pair-alert-icon{width:30px;height:30px;color:#ffdf5d;flex:0 0 30px;filter:drop-shadow(0 0 8px rgba(255,223,93,.22))}.pair-banner strong{display:block;font-size:18px;margin-bottom:4px;color:var(--purple-soft)}.pair-banner a{color:#fff;font-weight:800;text-decoration:none}.pair-banner .pair-code{display:inline-block;margin-left:4px;padding:2px 7px;border:1px solid rgba(255,255,255,.18);border-radius:6px;background:rgba(0,0,0,.22);font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;letter-spacing:.08em}.game-shell{display:grid;gap:10px}.game-tabs{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:8px}.game-tab{min-height:38px;padding:6px;background:var(--field);border-color:var(--line);color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.game-tab.active{border-color:var(--purple);color:var(--purple-soft);box-shadow:0 0 0 1px rgba(155,114,255,.25) inset}.game-hud{display:flex;justify-content:space-between;gap:10px;color:var(--muted);font-size:13px}canvas.game-canvas{width:100%;max-width:640px;aspect-ratio:320/170;border:1px solid var(--line);border-radius:8px;background:#020405;touch-action:none;image-rendering:pixelated}.game-controls{display:grid;grid-template-columns:repeat(4,46px);gap:8px;justify-content:start}.game-controls button{min-height:42px;padding:0}.art-popover{position:fixed;inset:0;z-index:20;display:none;align-items:center;justify-content:center;padding:18px;background:rgba(0,0,0,.78);backdrop-filter:blur(8px)}.art-popover.open{display:flex}.art-popover-inner{position:relative;max-width:min(92vw,720px);max-height:88vh}.art-popover img{display:block;width:auto;max-width:100%;max-height:88vh;border-radius:8px;border:1px solid rgba(255,255,255,.22);box-shadow:0 22px 80px rgba(0,0,0,.55);background:var(--art-bg)}button.art-popover-close{position:absolute;top:8px;right:8px;width:38px;height:38px;min-width:38px;min-height:38px;border-radius:8px;padding:0;background:rgba(8,11,12,.76);border-color:rgba(255,255,255,.28);color:#fff;font-size:24px;line-height:1}pre.logs{min-height:220px;max-height:360px;overflow:auto;margin:0;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--log-bg);color:var(--log-text);font:12px/1.35 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre-wrap;overflow-wrap:anywhere}@media (min-width:720px){main{grid-template-columns:1fr 1fr}.wide{grid-column:1 / -1}}@media (min-width:720px){#queueList,#playlistList{max-height:360px}}@media (max-width:640px){#queueList,#playlistList{max-height:280px}}@media (max-width:640px){.game-tabs{grid-template-columns:repeat(2,minmax(0,1fr))}}@media (max-width:420px){button.ptt{width:100%}}</style></head><body><header><h1><img class="brand-icon" src="/icon-192.png?v=3" alt=""><span class="brand-copy"><span class="brand-line">DJConnect <span id="appVersion" class="sub">-</span><span id="appModel" class="model-chip">-</span></span><span class="brand-tagline">Muziekbediening met karakter</span></span></h1><div class="sub header-status"><a class="header-website" href="https://djconnect.dev" target="_blank" rel="noopener noreferrer">djconnect.dev</a><span class="status-icons"><span id="haHeaderStatus" class="status-dot" title="Home Assistant">H</span><span id="spotifyHeaderStatus" class="status-dot" title="Playback">♪</span></span><span id="wifiHeaderSignal" class="signal level-0"><i></i><i></i><i></i><i></i></span><span id="batteryHeader" class="header-battery high" title="Battery"><span id="batteryHeaderFill" class="battery-fill"></span><span id="batteryHeaderText" class="battery-text">--%</span><span class="battery-flash">⚡</span></span></div></header><div id="haPairBanner" class="pair-banner"><svg class="pair-alert-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3.2 22 20H2L12 3.2z" fill="rgba(255,223,93,.16)" stroke="currentColor" stroke-width="1.8" stroke-linejoin="round"></path><path d="M12 8.1v6.1" stroke="currentColor" stroke-width="2.4" stroke-linecap="round"></path><circle cx="12" cy="17.5" r="1.25" fill="currentColor"></circle></svg><div><strong data-i18n="deviceNotPaired">Device not paired with Home Assistant</strong><a data-i18n="setup" href="https://my.home-assistant.io/redirect/config_flow_start?domain=djconnect" target="_blank" rel="noopener noreferrer">Click here to setup</a><span data-i18n="providePair">and provide pairing code:</span><span id="haPairBannerCode" class="pair-code">------</span></div></div><main><section class="panel wide"><h2 data-i18n="nowPlaying">Now Playing</h2><div id="nowContent" class="now no-art"><img id="albumArt" class="album-art" alt="Album art" role="button" tabindex="0"><div><div id="playbackPill" class="pill">Loading</div><div id="track" class="hero-title">-</div><div id="artist" class="artist">-</div></div></div><div class="bar"><i id="progressBar"></i></div><div class="row"><span class="key" data-i18n="time">Time</span><span id="time" class="value">-</span></div><div class="playback-actions compact-actions" aria-label="Playback controls"><button id="previousButton" class="icon-button" type="button" aria-label="Previous song" title="Previous song"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 5v14"></path><path d="M18 6l-9 6 9 6V6z"></path></svg></button><button id="playPauseButton" class="icon-button" type="button" aria-label="Play" title="Play"><svg class="icon-play" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14l11-7z"></path></svg><svg class="icon-pause" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14"></path><path d="M16 5v14"></path></svg></button><button id="nextButton" class="icon-button" type="button" aria-label="Next song" title="Next song"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l9 6-9 6V6z"></path><path d="M18 5v14"></path></svg></button><button id="shuffleButton" class="icon-button is-off" type="button" aria-label="Shuffle off" title="Shuffle off"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M16 3h5v5"></path><path d="M4 20l5.5-5.5"></path><path d="M15 9l6-6"></path><path d="M4 4l5 5"></path><path d="M13 13l8 8"></path><path d="M16 21h5v-5"></path></svg></button><button id="repeatButton" class="icon-button is-off" type="button" aria-label="Repeat off" title="Repeat off"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M17 1l4 4-4 4"></path><path d="M3 11V9a4 4 0 0 1 4-4h14"></path><path d="M7 23l-4-4 4-4"></path><path d="M21 13v2a4 4 0 0 1-4 4H3"></path><text id="repeatOneMark" x="12" y="15" text-anchor="middle" font-size="8" fill="currentColor" stroke="none" style="display:none">1</text></svg></button><div class="playback-volume"><div class="volume-row"><span class="volume-label" data-i18n="volume">Volume</span><span id="volume" class="volume-value">-</span></div><input id="volumeSlider" class="volume-slider" type="range" min="0" max="60" value="0" aria-label="Volume"></div></div><div id="shuffleStatus" class="status"></div><div id="repeatStatus" class="status"></div><button id="startLikedProxyButton" class="section-action" type="button" style="display:none">Start default playlist</button><div id="playbackCommandStatus" class="status"></div><select id="soundOutputSelect" aria-label="Sound output"><option value="" data-i18n="loadingOutputs">Loading outputs...</option></select><div id="soundOutputStatus" class="status"></div><div id="volumeStatus" class="status"></div><button id="webPttButton" class="ptt section-action" type="button" data-i18n="webPttHold">Test DJ announcement</button><div class="fine" data-i18n="webPttFlowInfo"></div><div id="webPttTranscript" class="fine"></div><div id="webPttStatus" class="status"></div></section><section id="queuePanel" class="panel"><div class="section-title-row"><h2 data-i18n="upNext">Up Next</h2><button id="refreshQueueButton" class="icon-button header-icon" type="button" aria-label="Refresh Up Next" title="Refresh Up Next"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M21 12a9 9 0 0 1-15.5 6.2"></path><path d="M3 12A9 9 0 0 1 18.5 5.8"></path><path d="M18 2v4h4"></path><path d="M6 22v-4H2"></path></svg></button></div><div id="queueList" class="queue"><div class="fine" data-i18n="loadingQueue">Loading queue...</div></div><div id="queueStatus" class="status"></div></section><section id="playlistsPanel" class="panel"><div class="section-title-row"><h2 data-i18n="playlists">Playlists</h2><button id="refreshPlaylistsButton" class="icon-button header-icon" type="button" aria-label="Refresh playlists" title="Refresh playlists"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M21 12a9 9 0 0 1-15.5 6.2"></path><path d="M3 12A9 9 0 0 1 18.5 5.8"></path><path d="M18 2v4h4"></path><path d="M6 22v-4H2"></path></svg></button></div><div id="playlistList" class="queue"><div class="fine" data-i18n="loadingPlaylists">Loading playlists...</div></div><div id="playlistStatus" class="status"></div></section><section class="panel wide"><h2 data-i18n="games">Games</h2><div class="game-shell"><div class="game-tabs" role="tablist" aria-label="Games"><button class="game-tab active" type="button" data-game="none" data-i18n="none">None</button><button class="game-tab" type="button" data-game="pong">Paddle Rally</button><button class="game-tab" type="button" data-game="asteroids">Meteor Run</button><button class="game-tab" type="button" data-game="fly">Sky Dash</button><button class="game-tab" type="button" data-game="maze">Maze Chase</button></div><div id="gameHud" class="game-hud"><span id="gameScore">Score 0</span><span id="gameHighScore">High 0</span></div><canvas id="gameCanvas" class="game-canvas" width="320" height="170" tabindex="0"></canvas><div id="gameControls" class="game-controls"><button id="gameUpButton" class="icon-button" type="button" aria-label="Up" title="Up"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5l7 8H5z"></path></svg></button><button id="gameDownButton" class="icon-button" type="button" aria-label="Down" title="Down"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 19l-7-8h14z"></path></svg></button><button id="gameFireButton" class="icon-button" type="button" aria-label="Fire" title="Fire"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3l2.6 6.4L21 12l-6.4 2.6L12 21l-2.6-6.4L3 12l6.4-2.6z"></path></svg></button><button id="gameResetButton" class="icon-button" type="button" aria-label="Reset" title="Reset"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 4v6h6"></path><path d="M20 20v-6h-6"></path><path d="M20 9a8 8 0 0 0-13.7-3.6L4 7.7"></path><path d="M4 15a8 8 0 0 0 13.7 3.6L20 16.3"></path></svg></button></div></div></section><section class="panel"><h2 data-i18n="settings">Settings</h2><form id="settingsForm" class="controls"><label data-i18n-label="brightness">Screen brightness
<select id="brightness"><option value="25">25%</option><option value="50">50%</option><option value="75">75%</option><option value="100">100%</option></select></label><label data-i18n-label="dimTimeout">Screen dim timeout
<select id="offTimeout"><option value="30000" data-i18n="timeout30s">30 seconds</option><option value="60000" data-i18n="timeout1m">1 minute</option><option value="120000" data-i18n="timeout2m">2 minutes</option><option value="240000" data-i18n="timeout4m">4 minutes</option></select><span class="fine" data-i18n="settingsFine">Screen turns off after the selected idle timeout. LED ring follows the screen power state.</span></label><label data-i18n-label="deepSleep">Turn off after
<select id="sleepTimeout"><option value="300000" data-i18n="timeout5m">5 minutes</option><option value="900000" data-i18n="timeout15m">15 minutes</option><option value="1800000" data-i18n="timeout30m">30 minutes</option><option value="3600000" data-i18n="timeout60m">60 minutes</option></select></label><label data-i18n-label="speakerVolume">Speaker volume
<select id="speakerVolume"><option value="25">25%</option><option value="50">50%</option><option value="75">75%</option><option value="100">100%</option></select></label><label data-i18n-label="language">Language
<select id="language"><option value="en" data-i18n="languageEnglish">English</option><option value="nl" data-i18n="languageDutch">Dutch</option></select></label><label data-i18n-label="theme">Theme
<select id="theme"><option value="dark" data-i18n="themeDark">Dark</option><option value="light" data-i18n="themeLight">Light</option><option value="auto" data-i18n="themeAuto">Auto</option></select></label><label data-i18n-label="logLevel">Log level
<select id="logLevel"><option value="debug" data-i18n="logLevelDebug">Debug</option><option value="info" data-i18n="logLevelInfo">Info</option><option value="warning" data-i18n="logLevelWarning">Warning</option><option value="error" data-i18n="logLevelError">Error</option></select></label><label data-i18n-label="wakeWord">Wake word
<select id="wakeWordEnabled"><option value="0" data-i18n="off">Off</option><option value="1" data-i18n="on">On</option></select></label><button data-i18n="saveSettings" type="submit">Save settings</button></form><div id="settingsStatus" class="status"></div></section><section class="panel"><h2 data-i18n="wifi">WiFi</h2><div class="grid wifi-grid"><div class="row"><span class="key" data-i18n="state">State</span><span id="wifiConnected" class="value">-</span></div><div class="row"><span class="key">IP</span><span id="wifiIp" class="value mono">-</span></div><div class="row"><span class="key">SSID</span><span id="wifiSsid" class="value">-</span></div><div class="row"><span class="key">RSSI</span><span class="value"><span id="wifiSignal" class="signal level-0"><i></i><i></i><i></i><i></i></span><span id="wifiRssi">-</span></span></div><div class="row"><span class="key">MAC</span><span id="wifiMac" class="value">-</span></div></div><form id="wifiForm" class="controls"><label data-i18n-label="newWifiSsid">New WiFi SSID
<input id="wifiNewSsid" name="ssid" autocomplete="off" required></label><label data-i18n-label="newWifiPassword">New WiFi password
<input id="wifiNewPassword" name="password" type="password" autocomplete="new-password" data-i18n-placeholder="wifiPasswordPlaceholder" placeholder="leave blank to keep current"></label><button data-i18n="wifiButton" type="submit">Test WiFi &amp; restart device</button></form><div class="fine" data-i18n="wifiFine">The device tests the new WiFi after this page responds. If it connects, credentials are saved and the device restarts automatically.</div><div id="wifiSettingsStatus" class="status"></div></section><section class="panel"><h2 data-i18n="ha">Home Assistant</h2><div class="grid"><div class="row"><span class="key" data-i18n="pairing">Pairing</span><span id="haPaired" class="value">-</span></div><div class="row"><span class="key" data-i18n="pairCode">Pair code</span><span id="haPairCode" class="value mono">-</span></div><div class="row"><span class="key">Device ID</span><span id="haDeviceId" class="value mono">-</span></div><div class="row"><span class="key">mDNS URL</span><span id="haMdnsUrl" class="value mono">-</span></div><div class="row"><span class="key">mDNS service</span><span id="haMdnsService" class="value mono">_djconnect._tcp</span></div><div class="row"><span class="key" data-i18n="firmware">Firmware</span><span id="haFirmware" class="value">-</span></div><div class="row"><span class="key" data-i18n="model">Model</span><span id="haModel" class="value">-</span></div><div class="row"><span class="key">URL</span><span id="haUrl" class="value mono">-</span></div></div><button id="resetPairingButton" data-i18n="resetPairing" class="warning section-action" type="button">Reset pairing</button><div id="haStatus" class="status"></div></section><section class="panel"><h2 data-i18n="playback">Music</h2><div class="grid"><div class="row"><span class="key" data-i18n="musicStatus">Status</span><span id="spotifyState" class="value">-</span></div><div class="row"><span class="key" data-i18n="musicLastIssue">Last issue</span><span id="spotifyError" class="value">-</span></div></div><button id="refreshButton" data-i18n="refreshPlayback" class="section-action" type="button">Refresh music status</button><div id="refreshStatus" class="status"></div></section><section class="panel"><h2 data-i18n="diagnostics">Diagnostics</h2><div class="grid"><div class="row"><span class="key" data-i18n="screen">Screen</span><span id="screenState" class="value">-</span></div><div class="row"><span class="key" data-i18n="ledRing">LED ring</span><span id="ledState" class="value">-</span></div><div class="row"><span class="key" data-i18n="uptime">Uptime</span><span id="uptime" class="value">-</span></div><div class="row"><span class="key" data-i18n="loopLoad">Loop load</span><span id="cpu" class="value">-</span></div><div class="row"><span class="key" data-i18n="heap">Heap</span><span id="heap" class="value">-</span></div><div class="row"><span class="key" data-i18n="storage">Storage</span><span id="storage" class="value">-</span></div><div class="row"><span class="key" data-i18n="sketch">Sketch</span><span id="sketch" class="value">-</span></div></div><button id="rebootButton" data-i18n="restart" class="warning section-action" type="button">Restart device</button></section><section id="logsPanel" class="panel wide"><h2 data-i18n="logs">Logs</h2><div class="two"><button id="pauseLogsButton" data-i18n="pauseLogs" class="secondary" type="button">Pause logs</button><button id="copyLogsButton" data-i18n="selectAll" class="secondary" type="button">Select all</button></div><div id="logsStatus" class="status"></div><pre id="logs" class="logs">Loading logs...</pre></section><section class="panel"><h2 data-i18n="firmwareOta">Firmware OTA</h2><form id="otaForm" class="controls"><input id="firmware" name="firmware" type="file" accept=".bin" required><button data-i18n="uploadFirmware" class="firmware" type="submit">Upload firmware</button></form><div class="fine" data-i18n="firmwareFine">Firmware updates run automatically when DJConnect is paired with Home Assistant.</div><div id="otaStatus" class="status"></div><button id="hardResetButton" data-i18n="factoryReset" class="danger" type="button">Factory reset</button></section><section class="panel"><h2 data-i18n="legal">Legal</h2><div class="fine" data-i18n="copyrightNotice">Copyright (c) 2026 Peter van Tol. All rights reserved. DJConnect firmware is proprietary software.</div><div class="fine" data-i18n="trademarkNotice">Spotify is a trademark of Spotify AB. DJConnect is not affiliated with, endorsed by, or sponsored by Spotify AB.</div><div class="fine" data-i18n="ossNotice">This firmware includes open-source software components. Their licenses remain with their respective authors.</div></section></main><div id="albumArtPopover" class="art-popover" role="dialog" aria-modal="true" aria-label="Album art"><div class="art-popover-inner"><img id="albumArtLarge" alt="Album art"><button id="albumArtClose" class="art-popover-close" type="button" aria-label="Close album art" title="Close album art">&times;</button></div></div><script>
const $ = id => document.getElementById(id);
const text = (id, value) => { $(id).textContent = value ?? "-"; };
function classifyStatusElement(el) {
if (!el || !el.classList || !el.classList.contains("status")) return;
const value = (el.textContent || "").toLowerCase();
el.classList.remove("alert", "error");
if (!value) return;
const errorWords = ["failed", "mislukt", "error", "fout", "not found", "niet gevonden", "authorization", "autorisatie", "auth", "endpoint"];
const alertWords = ["reset", "koppeling", "pairing", "warning", "waarschuwing"];
if (errorWords.some(word => value.includes(word))) {
el.classList.add("error");
} else if (alertWords.some(word => value.includes(word))) {
el.classList.add("alert");
}
}
function setupStatusStyling() {
document.querySelectorAll(".status").forEach(el => {
classifyStatusElement(el);
new MutationObserver(() => classifyStatusElement(el)).observe(el, { childList:true, characterData:true, subtree:true });
});
}
const bytes = n => {
if (!Number.isFinite(n)) return "-";
const units = ["B","KB","MB","GB"];
let value = n, unit = 0;
while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit++; }
return `${value.toFixed(value >= 10 || unit === 0 ? 0 : 1)} ${units[unit]}`;
};
const duration = ms => {
if (!Number.isFinite(ms)) return "-";
const s = Math.floor(ms / 1000), h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
return h ? `${h}:${String(m).padStart(2,"0")}:${String(sec).padStart(2,"0")}` : `${m}:${String(sec).padStart(2,"0")}`;
};
function pill(el, state) {
el.className = "pill" + (state === "ok" ? " ok" : state === "bad" ? " bad" : state === "warn" ? " warn" : "");
}
let currentLanguage = "en";
const translations = {
en: {
deviceNotPaired:"Device not paired with Home Assistant", setup:"Click here to setup", providePair:"and provide pairing code:",
nowPlaying:"Now Playing", time:"Time", previous:"Previous song", next:"Next song", play:"Play", pause:"Pause", liked:"Start default playlist",
webPttHold:"Test DJ announcement", webPttListening:"Testing DJ announcement...", webPttProcessing:"Sending test command...", webPttSent:"DJ announcement test sent", webPttTimeout:"DJ announcement test is still running on the device.",
webPttUnsupported:"Voice test is unavailable.", webPttNoSpeech:"No test command",
webPttFailed:"Voice command failed", webPttTestCommand:"Test the DJConnect announcement flow", webPttFlowInfo:"Tests: browser -> ESP /api/voice-text -> Home Assistant /api/djconnect/voice -> DJ announcement text on the device.",
spotifyUnavailable:"Playback not connected",
output:"Sound output", loadingOutputs:"Loading outputs...", volume:"Volume", upNext:"Queue", refreshUpNext:"Refresh queue", refreshPlaylists:"Refresh playlists", loadingQueue:"Loading queue...",
playlists:"Playlists", loadingPlaylists:"Loading playlists...", startPlaylist:"Start playlist", games:"Games", settings:"Settings",
brightness:"Screen brightness", dimTimeout:"Screen dim timeout", deepSleep:"Turn off after", speakerVolume:"Speaker volume", wakeWord:"Wake word",
language:"Language", languageEnglish:"English", languageDutch:"Dutch", theme:"Theme", themeAuto:"Auto", themeDark:"Dark", themeLight:"Light", logLevel:"Log level", logLevelDebug:"Debug", logLevelInfo:"Info", logLevelWarning:"Warning", logLevelError:"Error",
timeout30s:"30 seconds", timeout1m:"1 minute", timeout2m:"2 minutes", timeout4m:"4 minutes", timeout5m:"5 minutes", timeout15m:"15 minutes", timeout30m:"30 minutes", timeout60m:"60 minutes",
shuffle:"Shuffle", shuffleOff:"Shuffle off", shuffleOn:"Shuffle on", repeat:"Repeat", repeatOff:"Repeat off", repeatOnce:"Repeat once", repeatInfinite:"Repeat infinite", saveSettings:"Save settings", settingsFine:"Screen turns off after the selected idle timeout. LED ring follows the screen power state.",
wifi:"WiFi", state:"State", newWifiSsid:"New WiFi SSID", newWifiPassword:"New WiFi password", wifiButton:"Test WiFi & restart device",
wifiFine:"The device tests the new WiFi after this page responds. If it connects, credentials are saved and the device restarts automatically.",
wifiPasswordPlaceholder:"leave blank to keep current",
ha:"Home Assistant", pairing:"Pairing", pairCode:"Pair code", firmware:"Firmware", model:"Model", resetPairing:"Reset pairing",
spotify:"Playback", playback:"Music", connection:"Connection", musicStatus:"Status", musicLastIssue:"Last issue", token:"Backend", error:"Error", refreshSpotify:"Refresh playback status", refreshPlayback:"Refresh music status",
username:"Username", discovery:"HA discovery", lastPublished:"Last published", diagnostics:"Diagnostics", screen:"Screen",
ledRing:"LED ring", uptime:"Uptime", loopLoad:"Loop load", heap:"Heap", storage:"Storage", sketch:"Sketch", restart:"Restart device",
logs:"Logs", pauseLogs:"Pause logs", selectAll:"Select all", firmwareOta:"Firmware OTA", uploadFirmware:"Upload firmware",
firmwareFine:"Firmware updates run automatically when DJConnect is paired with Home Assistant.", factoryReset:"Factory reset",
loading:"Loading", playing:"Playing", paused:"Paused", noPlayback:"No playback", connected:"Connected", disconnected:"Disconnected",
authorized:"Authorized", notAuthorized:"Not authorized", tokenSecondsLeft:"s left", disabled:"Disabled", on:"On", off:"Off", charging:"charging", full:"full",
discharging:"discharging", paired:"Paired", pairingMode:"Pairing mode", pairingUnavailable:"Pairing info unavailable",
none:"None", noOutputs:"No sound outputs", outputsFailed:"Sound outputs failed", noQueuedSongs:"No queued songs", noPlaylists:"No playlists",
playlistsFailed:"Playlists failed", noLogs:"No logs yet", switchingOutput:"Switching output...", skipping:"Skipping...",
goingBack:"Going back...", startingLiked:"Starting default playlist...", startingQueueItem:"Starting selected song...", selectPlaylist:"Select a playlist",
startingPlaylist:"Starting playlist...", resumeLogs:"Resume logs", logsPaused:"Logs paused", logsLive:"Logs live",
logsPausedSelected:"Logs paused and selected", saving:"Saving...", testWifiConfirm:"Test these WiFi credentials? The web page may disconnect during the test.",
startingWifiTest:"Starting WiFi test...", refreshing:"Refreshing...", restartConfirm:"Restart DJConnect?",
resetPairingConfirm:"Reset Home Assistant pairing and restart to the pairing screen?", factoryResetConfirm:"Do you want to factory reset DJConnect device?",
noIp:"No IP", wifiSignal:"WiFi signal", wifiDisconnected:"WiFi disconnected", ago:"ago",
legal:"Legal", copyrightNotice:"Copyright (c) 2026 Peter van Tol. All rights reserved. DJConnect firmware is proprietary software.",
trademarkNotice:"Spotify is a trademark of Spotify AB. DJConnect is not affiliated with, endorsed by, or sponsored by Spotify AB.",
ossNotice:"This firmware includes open-source software components. Their licenses remain with their respective authors."
},
nl: {
deviceNotPaired:"Device niet gekoppeld met Home Assistant", setup:"Klik hier om te koppelen", providePair:"en vul koppelcode in:",
nowPlaying:"Speelt nu", time:"Tijd", previous:"Vorig nummer", next:"Volgend nummer", play:"Afspelen", pause:"Pauzeren", liked:"Start standaard playlist",
webPttHold:"Test DJ aankondiging", webPttListening:"DJ aankondiging testen...", webPttProcessing:"Testcommando versturen...", webPttSent:"DJ aankondiging test verstuurd", webPttTimeout:"DJ aankondiging test loopt nog op het device.",
webPttUnsupported:"Voice test is niet beschikbaar.", webPttNoSpeech:"Geen testcommando",
webPttFailed:"Voice command mislukt", webPttTestCommand:"Test de DJConnect aankondiging flow", webPttFlowInfo:"Test: browser -> ESP /api/voice-text -> Home Assistant /api/djconnect/voice -> DJ aankondiging tekst op het device.",
spotifyUnavailable:"Afspelen niet verbonden",
output:"Geluidsuitgang", loadingOutputs:"Geluidsuitgangen laden...", volume:"Volume", upNext:"Wachtrij", refreshUpNext:"Wachtrij verversen", refreshPlaylists:"Afspeellijsten verversen", loadingQueue:"Wachtrij laden...",
playlists:"Afspeellijsten", loadingPlaylists:"Afspeellijsten laden...", startPlaylist:"Start afspeellijst", games:"Games", settings:"Instellingen",
brightness:"Schermhelderheid", dimTimeout:"Scherm uit na", deepSleep:"Uitzetten na", speakerVolume:"Speakervolume", wakeWord:"Wakeword",
language:"Taal", languageEnglish:"Engels", languageDutch:"Nederlands", theme:"Thema", themeAuto:"Auto", themeDark:"Donker", themeLight:"Licht", logLevel:"Logniveau", logLevelDebug:"Debug", logLevelInfo:"Info", logLevelWarning:"Waarschuwing", logLevelError:"Fout",
timeout30s:"30 seconden", timeout1m:"1 minuut", timeout2m:"2 minuten", timeout4m:"4 minuten", timeout5m:"5 minuten", timeout15m:"15 minuten", timeout30m:"30 minuten", timeout60m:"60 minuten",
shuffle:"Shuffle", shuffleOff:"Shuffle uit", shuffleOn:"Shuffle aan", repeat:"Herhalen", repeatOff:"Herhalen uit", repeatOnce:"Eenmaal herhalen", repeatInfinite:"Oneindig herhalen", saveSettings:"Instellingen opslaan", settingsFine:"Scherm gaat uit na de ingestelde inactiviteit. LED-ring volgt de schermstatus.",
wifi:"WiFi", state:"Status", newWifiSsid:"Nieuwe WiFi SSID", newWifiPassword:"Nieuw WiFi wachtwoord", wifiButton:"Test WiFi & herstart device",
wifiFine:"Het device test de nieuwe WiFi nadat deze pagina antwoord krijgt. Bij succes worden credentials opgeslagen en herstart het device.",
wifiPasswordPlaceholder:"leeg laten om huidige te behouden",
ha:"Home Assistant", pairing:"Koppeling", pairCode:"Koppelcode", firmware:"Firmware", model:"Model", resetPairing:"Home Assistant koppeling resetten",
spotify:"Afspelen", playback:"Muziek", connection:"Verbinding", musicStatus:"Status", musicLastIssue:"Laatste melding", token:"Backend", error:"Fout", refreshSpotify:"Afspeelstatus verversen", refreshPlayback:"Muziekstatus verversen",
username:"Gebruikersnaam", discovery:"HA discovery", lastPublished:"Laatst gepubliceerd", diagnostics:"Diagnostiek", screen:"Scherm",
ledRing:"LED-ring", uptime:"Uptime", loopLoad:"Loop load", heap:"Heap", storage:"Opslag", sketch:"Sketch", restart:"Device herstarten",
logs:"Logs", pauseLogs:"Pauzeer logs", selectAll:"Selecteer alles", firmwareOta:"Firmware OTA", uploadFirmware:"Upload firmware",
firmwareFine:"Firmware update wordt automatisch uitgevoerd indien DJConnect is gekoppeld aan Home Assistant.", factoryReset:"Fabrieksreset",
loading:"Laden", playing:"Speelt", paused:"Gepauzeerd", noPlayback:"Geen playback", connected:"Verbonden", disconnected:"Niet verbonden",
authorized:"Geautoriseerd", notAuthorized:"Niet geautoriseerd", tokenSecondsLeft:"s over", disabled:"Uitgeschakeld", on:"Aan", off:"Uit", charging:"laden", full:"vol",
discharging:"ontladen", paired:"Gekoppeld", pairingMode:"Koppelmodus", pairingUnavailable:"Koppelinformatie niet beschikbaar",
none:"Geen", noOutputs:"Geen geluidsuitgangen", outputsFailed:"Geluidsuitgangen mislukt", noQueuedSongs:"Geen nummers in wachtrij", noPlaylists:"Geen afspeellijsten",
playlistsFailed:"Afspeellijsten mislukt", noLogs:"Nog geen logs", switchingOutput:"Output wisselen...", skipping:"Overslaan...",
goingBack:"Teruggaan...", startingLiked:"Standaard playlist starten...", startingQueueItem:"Gekozen nummer starten...", selectPlaylist:"Selecteer een afspeellijst",
startingPlaylist:"Afspeellijst starten...", resumeLogs:"Logs hervatten", logsPaused:"Logs gepauzeerd", logsLive:"Logs live",
logsPausedSelected:"Logs gepauzeerd en geselecteerd", saving:"Opslaan...", testWifiConfirm:"Deze WiFi-gegevens testen? De webpagina kan tijdens de test loskoppelen.",
startingWifiTest:"WiFi-test starten...", refreshing:"Verversen...", restartConfirm:"DJConnect herstarten?",
resetPairingConfirm:"Home Assistant koppeling resetten en herstarten naar het koppelscherm?", factoryResetConfirm:"Wilt u het DJConnect apparaat resetten naar fabrieksinstellingen?",
noIp:"Geen IP", wifiSignal:"WiFi signaal", wifiDisconnected:"WiFi niet verbonden", ago:"geleden",
legal:"Juridisch", copyrightNotice:"Copyright (c) 2026 Peter van Tol. Alle rechten voorbehouden. DJConnect firmware is proprietary software.",
trademarkNotice:"Spotify is een handelsmerk van Spotify AB. DJConnect is niet verbonden aan, goedgekeurd door of gesponsord door Spotify AB.",
ossNotice:"Deze firmware bevat open-source softwarecomponenten. De licenties daarvan blijven bij de respectievelijke auteurs."
}
};
const tr = key => (translations[currentLanguage] && translations[currentLanguage][key]) || translations.en[key] || key;
function applyTranslations() {
document.documentElement.lang = currentLanguage;
document.querySelectorAll("[data-i18n]").forEach(el => { el.textContent = tr(el.dataset.i18n); });
document.querySelectorAll("[data-i18n-label]").forEach(el => {
const value = tr(el.dataset.i18nLabel);
const first = Array.from(el.childNodes).find(node => node.nodeType === Node.TEXT_NODE);
if (first) first.textContent = value + "\n          ";
});
document.querySelectorAll("[data-i18n-placeholder]").forEach(el => {
el.placeholder = tr(el.dataset.i18nPlaceholder);
});
updatePlaybackButtonLabels();
$("startLikedProxyButton").textContent = tr("liked");
if (!webPttRunning) $("webPttButton").textContent = tr("webPttHold");
$("pauseLogsButton").textContent = logsPaused ? tr("resumeLogs") : tr("pauseLogs");
setButtonLabel("refreshQueueButton", tr("refreshUpNext"));
setButtonLabel("refreshPlaylistsButton", tr("refreshPlaylists"));
const outputOption = $("soundOutputSelect").querySelector("option[data-i18n='loadingOutputs']");
if (spotifyControlsEnabled && outputOption) outputOption.textContent = tr("loadingOutputs");
}
function setButtonLabel(id, label) {
const button = $(id);
button.setAttribute("aria-label", label);
button.title = label;
}
function updatePlaybackButtonLabels() {
if (!$("previousButton")) return;
setButtonLabel("previousButton", tr("previous"));
setButtonLabel("nextButton", tr("next"));
setButtonLabel("playPauseButton", playbackIsPlaying ? tr("pause") : tr("play"));
setButtonLabel("shuffleButton", shuffleEnabled ? tr("shuffleOn") : tr("shuffleOff"));
const repeatLabel = repeatState === "track" ? tr("repeatOnce") : repeatState === "context" ? tr("repeatInfinite") : tr("repeatOff");
setButtonLabel("repeatButton", repeatLabel);
}
function updatePlaybackButtonStates() {
$("playPauseButton").classList.toggle("is-playing", playbackIsPlaying);
$("playPauseButton").disabled = !spotifyControlsEnabled || !playbackHasPlayback;
$("shuffleButton").classList.toggle("is-on", shuffleEnabled);
$("shuffleButton").classList.toggle("is-off", !shuffleEnabled);
$("repeatButton").classList.toggle("is-off", repeatState === "off");
$("repeatButton").classList.toggle("is-on", repeatState === "context");
$("repeatButton").classList.toggle("is-track", repeatState === "track");
$("repeatOneMark").style.display = repeatState === "track" ? "block" : "none";
updatePlaybackButtonLabels();
}
const dirtyInputs = new Set();
const settingsInputIds = ["brightness", "offTimeout", "sleepTimeout", "speakerVolume", "language", "theme", "logLevel", "wakeWordEnabled"];
const userEditableInputIds = [...settingsInputIds, "wifiNewSsid", "wifiNewPassword"];
function setInput(id, value) {
const el = $(id);
if (document.activeElement !== el && !dirtyInputs.has(id)) el.value = value ?? "";
}
function setupDirtyInputTracking() {
for (const id of userEditableInputIds) {
const el = $(id);
if (!el) continue;
const markDirty = () => dirtyInputs.add(id);
el.addEventListener("focus", markDirty);
el.addEventListener("input", markDirty);
el.addEventListener("change", markDirty);
}
}
function clearDirtyInputs(ids) {
for (const id of ids) dirtyInputs.delete(id);
}
function wifiSignalLevel(connected, rssi) {
if (!connected || !Number.isFinite(rssi) || rssi === 0) return 0;
if (rssi > -55) return 4;
if (rssi > -67) return 3;
if (rssi > -75) return 2;
return 1;
}
function setWifiSignal(id, connected, rssi) {
const level = wifiSignalLevel(connected, rssi);
$(id).className = `signal level-${level}`;
$(id).title = connected ? `${tr("wifiSignal")} ${rssi} dBm` : tr("wifiDisconnected");
}
function setStatusDot(id, state) {
const el = $(id);
el.className = "status-dot" + (state === true || state === "ok" ? " ok" : state === "idle" ? " idle" : "");
}
function setBatteryHeader(battery) {
const el = $("batteryHeader");
if (!battery || battery.available === false) {
el.style.display = "none";
return;
}
el.style.display = "inline-flex";
const percent = Math.max(0, Math.min(100, Number(battery.percent ?? 0)));
const charging = !!battery.charging;
const full = !!battery.full;
el.className = "header-battery " + (charging ? "charging " : "") + (percent < 20 ? "low" : percent < 50 ? "medium" : "high");
el.title = `${battery.label || `${percent}%`} ${charging ? tr("charging") : full ? tr("full") : ""}`.trim();
$("batteryHeaderFill").style.width = `${percent}%`;
$("batteryHeaderText").textContent = charging ? `${percent}%` : (battery.label || `${percent}%`);
}
let volumeTimer = 0;
let logsPaused = false;
let soundOutputLoadedAt = 0;
let queueLoadedAt = 0;
let playlistsLoadedAt = 0;
let pairingInfoLoadedAt = 0;
let spotifyControlsEnabled = false;
let playbackIsPlaying = false;
let playbackHasPlayback = false;
let shuffleEnabled = false;
let repeatState = "off";
let webPttRunning = false;
let webPttAvailable = false;
let homeAssistantRuntimePaired = false;
let albumArtUrl = "";
let logsVisible = false;
let queueVisible = false;
let playlistsVisible = false;
let soundOutputsVisible = false;
let playbackFetchChain = Promise.resolve();
async function fetchPlaybackJson(url) {
const run = async () => {
const response = await fetch(url, { cache: "no-store" });
if (!response.ok) throw new Error(`${url} ${response.status}`);
return response.json();
};
const next = playbackFetchChain.then(run, run);
playbackFetchChain = next.catch(() => {});
return next;
}
function openAlbumArtPopover() {
if (!albumArtUrl) return;
$("albumArtLarge").src = albumArtUrl;
$("albumArtPopover").classList.add("open");
$("albumArtClose").focus();
}
function closeAlbumArtPopover() {
$("albumArtPopover").classList.remove("open");
$("albumArtLarge").removeAttribute("src");
}
function watchVisibility(id, callback) {
const el = $(id);
if (!el) return;
if ("IntersectionObserver" in window) {
const observer = new IntersectionObserver(entries => {
const visible = entries.some(entry => entry.isIntersecting);
callback(visible);
}, { rootMargin:"120px 0px", threshold:0.01 });
observer.observe(el);
} else {
callback(true);
}
}
function setupVisibilityObservers() {
watchVisibility("logsPanel", visible => {
logsVisible = visible;
if (visible && !logsPaused) refreshLogs();
});
watchVisibility("queuePanel", visible => {
queueVisible = visible;
if (visible && spotifyControlsEnabled) loadQueue();
});
watchVisibility("playlistsPanel", visible => {
playlistsVisible = visible;
if (visible && spotifyControlsEnabled) loadPlaylists();
});
watchVisibility("soundOutputSelect", visible => {
soundOutputsVisible = visible;
if (visible && spotifyControlsEnabled) loadSoundOutputs();
});
}
const game = {
mode:"none", visible:true, score:0, high:{ pong:0, asteroids:0, fly:0, maze:0 },
paddleY:86, ballX:160, ballY:86, ballVX:3, ballVY:2, paddleFlashUntil:0, wallFlashUntil:0, resetAt:0,
shipX:160, asteroidX:180, asteroidY:48, asteroidVY:2, asteroidSize:7, asteroidShape:0, bullet:false, bulletY:0, meteorBoomUntil:0, meteorBoomX:180, meteorBoomY:48,
planeY:86, obstacleX:300, obstacleY:86, obstacleShape:0, obstacleColor:"#9a6a43", shot:false, shotX:0, flyBoomUntil:0, flyBoomX:300, flyBoomY:86,
mazeX:52, mazeLane:1, mazeDirX:1, mazeDirY:0, ghostX:278, ghostLane:1, pelletX:180, pelletLane:1, mazePowerUntil:0, powerPellets:[],
flashUntil:0, last:0, stars:[]
};
const gameNames = { pong:"Paddle Rally", asteroids:"Meteor Run", fly:"Sky Dash", maze:"Maze Chase" };
let gameAudio = null;
function gameSound(kind) {
try {
gameAudio = gameAudio || new (window.AudioContext || window.webkitAudioContext)();
const patterns = {
pellet:[[1175,0.018,0.018]], power:[[880,0.035,0.022],[1319,0.05,0.022]], shoot:[[1568,0.028,0.018]],
break:[[1319,0.026,0.02],[988,0.045,0.02]], crash:[[262,0.08,0.03],[196,0.11,0.03]],
start:[[784,0.035,0.018]], bounce:[[740,0.022,0.015]], move:[[523,0.014,0.01]]
};
let t = gameAudio.currentTime;
for (const [freq, dur, gain] of patterns[kind] || []) {
const osc = gameAudio.createOscillator();
const amp = gameAudio.createGain();
osc.type = "square"; osc.frequency.value = freq;
amp.gain.setValueAtTime(gain, t);
amp.gain.exponentialRampToValueAtTime(0.001, t + dur);
osc.connect(amp); amp.connect(gameAudio.destination);
osc.start(t); osc.stop(t + dur);
t += dur + 0.014;
}
} catch (_) {}
}
function gameHighKey(mode) { return `djconnect.web.${mode}.high`; }
function loadGameHighScores() {
for (const mode of ["pong","asteroids","fly","maze"]) game.high[mode] = Number(localStorage.getItem(gameHighKey(mode)) || 0);
}
function updateGameHud() {
$("gameScore").textContent = `Score ${game.score}`;
$("gameHighScore").textContent = `High ${game.high[game.mode] || 0}`;
}
function focusGameCanvas() {
if (game.mode !== "none") $("gameCanvas").focus({ preventScroll:true });
}
function setGameButton(button, label, svgPath) {
button.setAttribute("aria-label", label);
button.title = label;
button.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${svgPath}"></path></svg>`;
}
function updateGameControls() {
const active = game.mode !== "none";
$("gameHud").style.display = active ? "" : "none";
$("gameCanvas").style.display = active ? "" : "none";
$("gameControls").style.display = active ? "grid" : "none";
if (!active) return;
$("gameFireButton").style.display = "";
$("gameResetButton").style.display = "";
if (game.mode === "maze") {
setGameButton($("gameUpButton"), "Up", "M12 5l7 8H5z");
setGameButton($("gameDownButton"), "Down", "M12 19l-7-8h14z");
setGameButton($("gameFireButton"), "Left", "M5 12l8-7v14z");
setGameButton($("gameResetButton"), "Right", "M19 12l-8 7V5z");
return;
}
const horizontal = game.mode === "asteroids";
setGameButton($("gameUpButton"), horizontal ? "Left" : "Up", horizontal ? "M5 12l8-7v14z" : "M12 5l7 8H5z");
setGameButton($("gameDownButton"), horizontal ? "Right" : "Down", horizontal ? "M19 12l-8 7V5z" : "M12 19l-7-8h14z");
setGameButton($("gameFireButton"), "Fire", "M12 3l2.6 6.4L21 12l-6.4 2.6L12 21l-2.6-6.4L3 12l6.4-2.6z");
setGameButton($("gameResetButton"), "Reset", "M4 4v6h6M20 20v-6h-6M20 9a8 8 0 0 0-13.7-3.6L4 7.7M4 15a8 8 0 0 0 13.7 3.6L20 16.3");
}
function setGameScore(score) {
game.score = score;
if (score > (game.high[game.mode] || 0)) {
game.high[game.mode] = score;
localStorage.setItem(gameHighKey(game.mode), String(score));
}
updateGameHud();
}
function resetGame() {
game.score = 0;
game.flashUntil = 0;
game.resetAt = 0;
game.paddleFlashUntil = 0;
game.wallFlashUntil = 0;
game.meteorBoomUntil = 0;
game.flyBoomUntil = 0;
if (!game.stars.length) game.stars = Array.from({length:22}, (_, i) => ({ x:(i * 47) % 320, y:42 + ((i * 31) % 112), s:1 + (i % 3) }));
if (game.mode === "pong") {
game.paddleY = 86; game.ballX = 160; game.ballY = 86; game.ballVX = 3; game.ballVY = 2;
} else if (game.mode === "asteroids") {
spawnMeteor(); game.shipX = 160; game.bullet = false;
} else if (game.mode === "fly") {
game.planeY = 86; spawnObstacle(); game.obstacleX = 300; game.shot = false;
} else if (game.mode === "maze") {
game.mazeX = 52; game.mazeLane = 1; game.mazeDirX = 1; game.mazeDirY = 0; game.ghostX = 278; game.ghostLane = Math.floor(Math.random() * 3); game.pelletX = 90 + Math.random() * 170; game.pelletLane = Math.floor(Math.random() * 3); game.mazePowerUntil = 0; game.powerPellets = [{x:42,lane:0,on:true},{x:278,lane:0,on:true},{x:42,lane:2,on:true},{x:278,lane:2,on:true}];
}
updateGameHud();
}
function spawnMeteor() {
game.asteroidX = 30 + Math.random() * 260; game.asteroidY = 44; game.asteroidVY = 1.2 + Math.random() * 1.8 + Math.min(Math.floor(game.score / 10), 2); game.asteroidSize = 5 + Math.floor(Math.random() * 7); game.asteroidShape = Math.floor(Math.random() * 3);
}
function spawnObstacle() {
const colors = ["#9a6a43","#ff6ab7","#1ed760","#ff9f1a"];
game.obstacleX = 310; game.obstacleY = 52 + Math.random() * 92; game.obstacleShape = Math.floor(Math.random() * 3); game.obstacleColor = colors[Math.floor(Math.random() * colors.length)];
}
function selectGame(mode) {
game.mode = mode;
document.querySelectorAll(".game-tab").forEach(button => button.classList.toggle("active", button.dataset.game === mode));
updateGameControls();
resetGame();
focusGameCanvas();
}
function moveGame(delta) {
if (game.mode === "pong") game.paddleY = Math.max(42, Math.min(126, game.paddleY + delta * 6));
if (game.mode === "asteroids") game.shipX = Math.max(24, Math.min(296, game.shipX + delta * 8));
if (game.mode === "fly") { game.planeY = Math.max(52, Math.min(138, game.planeY + delta * 7)); gameSound("move"); }
if (game.mode === "maze") moveMaze(delta, 0);
}
function moveMaze(dx, dy) {
game.mazeDirX = dx; game.mazeDirY = dy;
game.mazeX = Math.max(30, Math.min(290, game.mazeX + dx * 10));
game.mazeLane = Math.max(0, Math.min(2, game.mazeLane + dy));
}
function fireGame() {
if (game.mode === "asteroids" && !game.bullet) { game.bullet = true; game.bulletY = 120; gameSound("shoot"); }
if (game.mode === "fly" && !game.shot) { game.shot = true; game.shotX = 58; gameSound("shoot"); }
}
function drawStars(ctx, fast) {
ctx.fillStyle = fast ? "#87d8ff" : "#7c8590";
for (const star of game.stars) {
if (fast) ctx.fillRect(star.x, star.y, 3 + star.s * 2, 1);
else ctx.fillRect(star.x, star.y, star.s === 1 ? 1 : 2, star.s === 1 ? 1 : 2);
}
}
function drawGame() {
if (game.mode === "none") return;
const canvas = $("gameCanvas");
const ctx = canvas.getContext("2d");
ctx.clearRect(0, 0, 320, 170);
ctx.fillStyle = "#020405"; ctx.fillRect(0, 0, 320, 170);
ctx.strokeStyle = "#30383a"; ctx.beginPath(); ctx.moveTo(8, 36); ctx.lineTo(312, 36); ctx.stroke();
ctx.font = "14px system-ui"; ctx.fillStyle = game.mode === "pong" ? "#ff9f1a" : game.mode === "asteroids" ? "#5fa7ff" : game.mode === "maze" ? "#ffd84f" : "#5cccff";
ctx.fillText(gameNames[game.mode] || "", 8, 20);
ctx.fillStyle = "#f3f7f5"; ctx.textAlign = "right"; ctx.fillText(`Score ${game.score}  High ${game.high[game.mode] || 0}`, 312, 20); ctx.textAlign = "left";
if (game.mode === "pong") {
ctx.strokeStyle = "#30383a"; ctx.beginPath(); ctx.moveTo(160,42); ctx.lineTo(160,160); ctx.stroke();
ctx.fillStyle = Date.now() < game.paddleFlashUntil ? "#ffd84f" : "#ff9f1a"; ctx.fillRect(18, game.paddleY, 8, 34);
if (Date.now() < game.wallFlashUntil) { ctx.strokeStyle = "#1ed760"; ctx.strokeRect(9, 42, 302, 118); }
ctx.fillStyle = "#1ed760"; ctx.beginPath(); ctx.arc(game.ballX, game.ballY, 4, 0, Math.PI * 2); ctx.fill();
} else if (game.mode === "asteroids") {
drawStars(ctx, false);
ctx.strokeStyle = "#5fa7ff"; ctx.beginPath(); ctx.moveTo(game.shipX, 128); ctx.lineTo(game.shipX - 8, 146); ctx.lineTo(game.shipX + 8, 146); ctx.closePath(); ctx.stroke();
ctx.strokeStyle = "#ff6ab7"; ctx.beginPath(); ctx.arc(game.asteroidX, game.asteroidY, 9, 0, Math.PI * 2); ctx.stroke();
if (game.asteroidShape === 1) { ctx.beginPath(); ctx.moveTo(game.asteroidX, game.asteroidY - game.asteroidSize); ctx.lineTo(game.asteroidX - game.asteroidSize, game.asteroidY + game.asteroidSize); ctx.lineTo(game.asteroidX + game.asteroidSize, game.asteroidY + game.asteroidSize - 2); ctx.closePath(); ctx.stroke(); }
else if (game.asteroidShape === 2) ctx.strokeRect(game.asteroidX - game.asteroidSize, game.asteroidY - game.asteroidSize, game.asteroidSize * 2, game.asteroidSize * 2);
else { ctx.beginPath(); ctx.arc(game.asteroidX, game.asteroidY, game.asteroidSize, 0, Math.PI * 2); ctx.stroke(); }
if (Date.now() < game.meteorBoomUntil) { ctx.strokeStyle = "#ffd84f"; ctx.beginPath(); ctx.arc(game.meteorBoomX, game.meteorBoomY, game.asteroidSize + 10, 0, Math.PI * 2); ctx.stroke(); }
if (game.bullet) { ctx.fillStyle = "#5cccff"; ctx.fillRect(game.shipX - 2, game.bulletY, 4, 10); }
} else if (game.mode === "fly") {
drawStars(ctx, true);
ctx.fillStyle = "#5cccff"; ctx.beginPath(); ctx.moveTo(60, game.planeY); ctx.lineTo(30, game.planeY - 10); ctx.lineTo(30, game.planeY + 10); ctx.closePath(); ctx.fill();
ctx.strokeStyle = "#5cccff"; ctx.beginPath(); ctx.moveTo(38, game.planeY); ctx.lineTo(20, game.planeY - 18); ctx.moveTo(38, game.planeY); ctx.lineTo(20, game.planeY + 18); ctx.stroke();
ctx.fillStyle = game.obstacleColor;
if (game.obstacleShape === 1) { ctx.beginPath(); ctx.moveTo(game.obstacleX, game.obstacleY - 18); ctx.lineTo(game.obstacleX - 13, game.obstacleY + 16); ctx.lineTo(game.obstacleX + 13, game.obstacleY + 16); ctx.closePath(); ctx.fill(); }
else if (game.obstacleShape === 2) { ctx.beginPath(); ctx.arc(game.obstacleX, game.obstacleY, 13, 0, Math.PI * 2); ctx.fill(); }
else ctx.fillRect(game.obstacleX - 8, game.obstacleY - 16, 16, 32);
if (Date.now() < game.flyBoomUntil) { ctx.strokeStyle = "#ffd84f"; ctx.beginPath(); ctx.arc(game.flyBoomX, game.flyBoomY, 22, 0, Math.PI * 2); ctx.stroke(); }
if (game.shot) { ctx.fillStyle = "#5cccff"; ctx.fillRect(game.shotX, game.planeY - 2, 14, 4); }
} else if (game.mode === "maze") {
const lanes = [58, 96, 134];
ctx.strokeStyle = "#5fa7ff";
lanes.forEach(y => { ctx.beginPath(); ctx.moveTo(18, y); ctx.lineTo(302, y); ctx.stroke(); });
ctx.fillStyle = "#f3f7f5"; ctx.beginPath(); ctx.arc(game.pelletX, lanes[game.pelletLane], 4, 0, Math.PI * 2); ctx.fill();
for (const pellet of game.powerPellets) if (pellet.on) { ctx.fillStyle = "#ffd84f"; ctx.beginPath(); ctx.arc(pellet.x, lanes[pellet.lane], 7, 0, Math.PI * 2); ctx.fill(); ctx.strokeStyle = "#ff6ab7"; ctx.stroke(); }
const py = lanes[game.mazeLane];
if (game.resetAt) { ctx.strokeStyle = "#ff735d"; ctx.beginPath(); ctx.arc(game.mazeX, py, 14 + ((Date.now() / 70) % 5), 0, Math.PI * 2); ctx.stroke(); }
else {
let start = 0.25 * Math.PI, end = 1.75 * Math.PI;
if (game.mazeDirX < 0) { start = 1.25 * Math.PI; end = 0.75 * Math.PI; }
if (game.mazeDirY < 0) { start = 1.75 * Math.PI; end = 1.25 * Math.PI; }
if (game.mazeDirY > 0) { start = 0.75 * Math.PI; end = 0.25 * Math.PI; }
ctx.fillStyle = "#ffd84f"; ctx.beginPath(); ctx.arc(game.mazeX, py, 10, start, end); ctx.lineTo(game.mazeX, py); ctx.fill();
ctx.fillStyle = "#050816"; ctx.beginPath(); ctx.arc(game.mazeX + (game.mazeDirX || .5) * 3, py + (game.mazeDirY ? game.mazeDirY * 3 : -4), 2, 0, Math.PI * 2); ctx.fill();
}
const vulnerable = Date.now() < game.mazePowerUntil;
ctx.fillStyle = vulnerable ? (Math.floor(Date.now() / 180) % 2 ? "#f3f7f5" : "#5fa7ff") : "#d72ff3"; ctx.fillRect(game.ghostX - 10, lanes[game.ghostLane] - 10, 20, 20);
if (vulnerable) { ctx.strokeStyle = "#ffd84f"; ctx.strokeRect(game.ghostX - 12, lanes[game.ghostLane] - 12, 24, 24); }
ctx.fillStyle = "#f3f7f5"; ctx.beginPath(); ctx.arc(game.ghostX - 4, lanes[game.ghostLane] - 3, 2, 0, Math.PI * 2); ctx.arc(game.ghostX + 4, lanes[game.ghostLane] - 3, 2, 0, Math.PI * 2); ctx.fill();
}
if (Date.now() < game.flashUntil) { ctx.strokeStyle = "#ff735d"; ctx.lineWidth = 2; ctx.strokeRect(1, 1, 318, 168); ctx.lineWidth = 1; }
}
function stepGame(ts) {
if (!game.last) game.last = ts;
if (game.mode !== "none" && game.visible && ts - game.last > 32) {
game.last = ts;
for (const star of game.stars) {
star.x -= game.mode === "fly" ? 2 + star.s : 0;
star.y += game.mode === "asteroids" ? star.s : 0;
if (star.x < 8) star.x = 312;
if (star.y > 156) star.y = 42;
}
if (game.resetAt && Date.now() >= game.resetAt) resetGame();
if (game.resetAt) { drawGame(); requestAnimationFrame(stepGame); return; }
if (game.mode === "pong") {
game.ballX += game.ballVX; game.ballY += game.ballVY;
if (game.ballY <= 42 || game.ballY >= 156) { game.ballVY *= -1; game.wallFlashUntil = Date.now() + 120; gameSound("bounce"); }
if (game.ballX >= 306) { game.ballVX = -Math.abs(game.ballVX); game.wallFlashUntil = Date.now() + 120; gameSound("bounce"); }
if (game.ballX <= 30) {
if (game.ballY >= game.paddleY - 4 && game.ballY <= game.paddleY + 38) { game.ballVX = Math.abs(game.ballVX); game.paddleFlashUntil = Date.now() + 140; setGameScore(game.score + 1); gameSound("bounce"); }
else { game.resetAt = Date.now() + 650; game.flashUntil = Date.now() + 350; gameSound("crash"); }
}
} else if (game.mode === "asteroids") {
game.asteroidY += game.asteroidVY;
if (game.bullet) {
game.bulletY -= 8;
if (game.bulletY < 38) game.bullet = false;
else if (Math.abs(game.asteroidX - game.shipX) < game.asteroidSize + 9 && Math.abs(game.asteroidY - game.bulletY) < game.asteroidSize + 9) {
setGameScore(game.score + 1); game.bullet = false; game.meteorBoomX = game.asteroidX; game.meteorBoomY = game.asteroidY; game.meteorBoomUntil = Date.now() + 180; gameSound("break"); spawnMeteor();
}
}
if (game.asteroidY > 150) { game.flashUntil = Date.now() + 350; gameSound("crash"); spawnMeteor(); setGameScore(0); }
} else if (game.mode === "fly") {
game.obstacleX -= 4 + Math.min(Math.floor(game.score / 6), 4);
if (game.shot) {
game.shotX += 9;
if (game.shotX > 310) game.shot = false;
else if (Math.abs(game.shotX - game.obstacleX) < 16 && Math.abs(game.planeY - game.obstacleY) < 24) { setGameScore(game.score + 1); game.shot = false; game.flyBoomX = game.obstacleX; game.flyBoomY = game.obstacleY; game.flyBoomUntil = Date.now() + 180; gameSound("break"); spawnObstacle(); }
}
if (game.obstacleX < 24) { spawnObstacle(); setGameScore(game.score + 1); }
if (game.obstacleX < 64 && game.obstacleX > 28 && Math.abs(game.planeY - game.obstacleY) < 28) { game.flashUntil = Date.now() + 350; game.resetAt = Date.now() + 650; gameSound("crash"); }
} else if (game.mode === "maze") {
const vulnerable = Date.now() < game.mazePowerUntil;
const speed = vulnerable ? 1 : 1 + Math.min(Math.floor(game.score / 10), 3);
game.ghostX += game.ghostX > game.mazeX ? -speed : speed;
if (Math.random() < (vulnerable ? .05 : .07)) game.ghostLane += game.ghostLane < game.mazeLane ? 1 : game.ghostLane > game.mazeLane ? -1 : 0;
if (Math.abs(game.mazeX - game.pelletX) < 13 && game.mazeLane === game.pelletLane) {
setGameScore(game.score + 1); gameSound("pellet"); game.pelletX = 42 + Math.random() * 236; game.pelletLane = Math.floor(Math.random() * 3);
}
for (const pellet of game.powerPellets) if (pellet.on && Math.abs(game.mazeX - pellet.x) < 13 && game.mazeLane === pellet.lane) {
pellet.on = false; setGameScore(game.score + 3); game.mazePowerUntil = Date.now() + 6000; gameSound("power");
}
if (Math.abs(game.mazeX - game.ghostX) < 15 && game.mazeLane === game.ghostLane) {
if (vulnerable) {
setGameScore(game.score + 5); game.flashUntil = Date.now() + 180; gameSound("break"); game.ghostX = game.mazeX < 160 ? 278 : 42; game.ghostLane = Math.floor(Math.random() * 3);
} else {
game.flashUntil = Date.now() + 350; game.resetAt = Date.now() + 750; gameSound("crash");
}
}
}
drawGame();
}
requestAnimationFrame(stepGame);
}
function setupGames() {
loadGameHighScores();
updateGameControls();
resetGame();
document.querySelectorAll(".game-tab").forEach(button => button.addEventListener("click", () => selectGame(button.dataset.game)));
$("gameUpButton").addEventListener("click", () => { moveGame(-1); focusGameCanvas(); });
$("gameDownButton").addEventListener("click", () => { moveGame(1); focusGameCanvas(); });
$("gameFireButton").addEventListener("pointerdown", event => { event.preventDefault(); if (game.mode === "maze") moveMaze(-1, 0); else fireGame(); focusGameCanvas(); });
$("gameResetButton").addEventListener("click", () => { if (game.mode === "maze") moveMaze(1, 0); else { resetGame(); gameSound("start"); } focusGameCanvas(); });
$("gameCanvas").addEventListener("pointerdown", event => { event.preventDefault(); focusGameCanvas(); fireGame(); });
window.addEventListener("keydown", event => {
if (game.mode === "none" || !game.visible || ["INPUT","SELECT","TEXTAREA","BUTTON"].includes(document.activeElement?.tagName || "")) return;
const gameKeys = ["ArrowUp","ArrowDown","ArrowLeft","ArrowRight"," ","Enter"];
if (!gameKeys.includes(event.key)) return;
event.preventDefault();
const movementKeys = (game.mode === "asteroids" || game.mode === "maze") ? ["ArrowLeft","ArrowRight"] : ["ArrowUp","ArrowDown"];
if (event.key === movementKeys[0]) moveGame(-1);
if (event.key === movementKeys[1]) moveGame(1);
if (game.mode === "maze" && event.key === "ArrowUp") moveMaze(0, -1);
if (game.mode === "maze" && event.key === "ArrowDown") moveMaze(0, 1);
if (event.key === " " || event.key === "Enter") fireGame();
});
watchVisibility("gameCanvas", visible => { game.visible = visible; if (visible) drawGame(); });
requestAnimationFrame(stepGame);
}
async function startWebPtt() {
if ($("webPttButton").disabled) return;
if (webPttRunning) return;
webPttRunning = true;
$("webPttButton").disabled = true;
$("webPttTranscript").textContent = tr("webPttTestCommand");
$("webPttStatus").textContent = tr("webPttProcessing");
$("webPttButton").textContent = tr("webPttListening");
$("webPttButton").classList.add("recording");
const controller = new AbortController();
const timeout = setTimeout(() => controller.abort(), 65000);
try {
const body = new URLSearchParams({ text: tr("webPttTestCommand") });
const response = await fetch("/api/voice-text", { method:"POST", body, signal:controller.signal });
let payload = {};
try { payload = await response.json(); } catch (_) {}
if (response.ok && payload.success !== false) {
$("webPttStatus").textContent = payload.message && payload.message !== "Voice command sent" ? payload.message : tr("webPttSent");
} else {
$("webPttStatus").textContent = payload.message || tr("webPttFailed");
}
refresh();
} catch (error) {
$("webPttStatus").textContent = error && error.name === "AbortError" ? tr("webPttTimeout") : tr("webPttFailed");
} finally {
clearTimeout(timeout);
webPttRunning = false;
$("webPttButton").classList.remove("recording");
$("webPttButton").textContent = tr("webPttHold");
$("webPttButton").disabled = !webPttAvailable;
}
}
function setSpotifyControlsEnabled(enabled) {
spotifyControlsEnabled = !!enabled;
for (const id of ["previousButton", "nextButton", "shuffleButton", "repeatButton", "volumeSlider", "soundOutputSelect", "startLikedProxyButton", "refreshQueueButton", "refreshPlaylistsButton"]) {
$(id).disabled = !spotifyControlsEnabled;
}
updatePlaybackButtonStates();
if (!spotifyControlsEnabled) {
$("soundOutputStatus").textContent = "";
$("playbackCommandStatus").textContent = "";
$("shuffleStatus").textContent = "";
$("repeatStatus").textContent = "";
$("volumeStatus").textContent = "";
$("soundOutputSelect").innerHTML = `<option value="none">${tr("none")}</option>`;
$("playlistList").innerHTML = `<div class="fine">${tr("spotifyUnavailable")}</div>`;
$("queueList").innerHTML = '<div class="fine"></div>';
}
}
function render(data) {
currentLanguage = data.settings.language || "en";
document.documentElement.dataset.theme = data.settings.theme || "dark";
applyTranslations();
text("appVersion", data.app.version);
text("appModel", data.app.model || "-");
text("wifiIp", data.wifi.ip || tr("noIp"));
text("track", data.playback.hasPlayback ? (data.playback.track || "-") : "");
text("artist", data.playback.hasPlayback ? (data.playback.artist || data.playback.type || "-") : "");
const albumArt = $("albumArt");
const nowContent = $("nowContent");
if (data.playback.albumImageUrl) {
if (albumArtUrl !== data.playback.albumImageUrl) {
albumArtUrl = data.playback.albumImageUrl;
albumArt.src = albumArtUrl;
}
albumArt.style.display = "block";
nowContent.classList.remove("no-art");
} else {
albumArtUrl = "";
albumArt.removeAttribute("src");
albumArt.style.display = "none";
closeAlbumArtPopover();
nowContent.classList.add("no-art");
}
const state = data.playback.isPlaying ? tr("playing") : data.playback.hasPlayback ? tr("paused") : tr("noPlayback");
text("playbackPill", state);
pill($("playbackPill"), data.playback.isPlaying ? "ok" : data.playback.hasPlayback ? "warn" : "bad");
$("startLikedProxyButton").style.display = data.spotify.authorized && !data.playback.hasPlayback ? "block" : "none";
playbackIsPlaying = !!data.playback.isPlaying;
playbackHasPlayback = !!data.playback.hasPlayback;
shuffleEnabled = !!data.playback.shuffle;
repeatState = data.playback.repeatState || "off";
updatePlaybackButtonStates();
text("time", `${duration(data.playback.progressMs)} / ${duration(data.playback.durationMs)}`);
$("progressBar").style.width = `${data.playback.progressPercent || 0}%`;
text("volume", data.device.volume >= 0 ? `${data.device.volume}%` : "-");
setInput("volumeSlider", data.device.volume >= 0 ? String(data.device.volume) : "0");
$("volumeSlider").disabled = !data.spotify.authorized || !data.playback.hasPlayback || !data.device.supportsVolume;
if (!data.playback.hasPlayback || !data.device.supportsVolume) $("volumeStatus").textContent = "";
setBatteryHeader(data.battery);
text("wifiConnected", data.wifi.connected ? tr("connected") : tr("disconnected"));
text("wifiSsid", data.wifi.ssid || "-");
setInput("wifiNewSsid", data.wifi.ssid || "");
text("wifiRssi", data.wifi.rssi ? `${data.wifi.rssi} dBm` : "-");
setWifiSignal("wifiSignal", data.wifi.connected, data.wifi.rssi);
setWifiSignal("wifiHeaderSignal", data.wifi.connected, data.wifi.rssi);
text("wifiMac", data.wifi.mac);
const playbackStatus = data.spotify.status || (data.spotify.authorized ? "ok" : "error");
text("spotifyState", playbackStatus === "ok" ? tr("authorized") : playbackStatus === "idle" ? tr("noPlayback") : tr("notAuthorized"));
setStatusDot("spotifyHeaderStatus", playbackStatus);
setSpotifyControlsEnabled(!!data.spotify.authorized);
text("spotifyError", data.spotify.error || "-");
homeAssistantRuntimePaired = !!(data.ha && data.ha.paired);
setStatusDot("haHeaderStatus", homeAssistantRuntimePaired);
text("haPaired", homeAssistantRuntimePaired ? tr("paired") : tr("pairingMode"));
webPttAvailable = !!(data.voice && data.voice.available);
if (!webPttRunning) $("webPttButton").disabled = !webPttAvailable;
text("screenState", `${data.screen.state} (${data.screen.brightnessLevel}%)`);
text("ledState", data.led.state);
text("uptime", duration(data.app.uptimeMs));
if (data.system) {
text("cpu", `${data.system.cpuUsagePercent}% loop load`);
text("heap", `${bytes(data.system.heapUsed)} used / ${bytes(data.system.heapTotal)} total, ${bytes(data.system.heapFree)} free`);
text("storage", `${bytes(data.system.otaFree)} OTA free / ${bytes(data.system.otaTotal)} OTA total`);
text("sketch", `${bytes(data.system.sketchSize)} sketch / ${bytes(data.system.flashSize)} flash`);
}
setInput("brightness", String(data.settings.screenBrightnessPercent));
setInput("offTimeout", String(data.settings.screenOffTimeoutMs));
setInput("sleepTimeout", String(data.settings.deviceSleepTimeoutMs));
setInput("speakerVolume", String(data.settings.speakerVolumePercent));
setInput("language", currentLanguage);
setInput("theme", data.settings.theme || "dark");
setInput("logLevel", data.settings.logLevel || "info");
setInput("wakeWordEnabled", data.settings.wakeWordEnabled ? "1" : "0");
}
async function refresh() {
const response = await fetch("/api/status", { cache: "no-store" });
render(await response.json());
if (Date.now() - pairingInfoLoadedAt > 5000) loadPairingInfo();
if (spotifyControlsEnabled && soundOutputsVisible && Date.now() - soundOutputLoadedAt > 15000) loadSoundOutputs();
if (spotifyControlsEnabled && queueVisible && Date.now() - queueLoadedAt > 15000) loadQueue();
if (spotifyControlsEnabled && playlistsVisible && Date.now() - playlistsLoadedAt > 30000) loadPlaylists();
}
async function loadPairingInfo() {
pairingInfoLoadedAt = Date.now();
try {
const infoResponse = await fetch("/api/device/info", { cache: "no-store" });
const info = await infoResponse.json();
text("haDeviceId", info.device_id || "-");
text("haMdnsUrl", info.device_id ? `http://${info.device_id}.local` : "-");
text("haFirmware", info.firmware || "-");
text("haModel", info.model || "-");
text("haUrl", info.ha_local_url || "-");
text("haStatus", "");
if (homeAssistantRuntimePaired) {
$("haPairBanner").classList.remove("visible");
text("haPairCode", "-");
return;
}
const pairResponse = await fetch("/api/device/pairing-info", { cache: "no-store" });
const pair = await pairResponse.json();
text("haPairCode", pair.pair_code || "-");
text("haPairBannerCode", pair.pair_code || "------");
$("haPairBanner").classList.add("visible");
text("haMdnsUrl", pair.local_url || (pair.device_id ? `http://${pair.device_id}.local` : "-"));
} catch (error) {
$("haPairBanner").classList.remove("visible");
setStatusDot("haHeaderStatus", false);
text("haStatus", tr("pairingUnavailable"));
}
}
async function loadSoundOutputs() {
if (!soundOutputsVisible) return;
const select = $("soundOutputSelect");
soundOutputLoadedAt = Date.now();
try {
const data = await fetchPlaybackJson("/api/devices");
select.innerHTML = "";
const none = document.createElement("option");
none.value = "none";
none.textContent = tr("none") || "None";
select.appendChild(none);
for (const device of (data.devices || [])) {
if ((device.name || "").toLowerCase().includes("iphone")) {
continue;
}
const option = document.createElement("option");
option.value = device.id;
option.textContent = device.active ? `${device.name} *` : device.name;
option.selected = !!device.active;
select.appendChild(option);
}
} catch (error) {
select.innerHTML = `<option value="">${tr("outputsFailed")}</option>`;
}
}
async function loadQueue() {
if (!queueVisible) return;
const list = $("queueList");
queueLoadedAt = Date.now();
try {
const data = await fetchPlaybackJson("/api/queue");
list.innerHTML = "";
if (!data.items || data.items.length === 0) {
list.innerHTML = `<div class="fine">${data.error || tr("noQueuedSongs")}</div>`;
return;
}
for (const item of data.items) {
const row = document.createElement("div");
row.className = "queue-item";
if (item.imageUrl) {
const art = document.createElement("img");
art.className = "queue-art";
art.alt = "";
art.loading = "lazy";
art.decoding = "async";
art.referrerPolicy = "no-referrer";
art.src = item.imageUrl;
row.appendChild(art);
} else {
const art = document.createElement("div");
art.className = "queue-art empty";
art.textContent = "♪";
row.appendChild(art);
}
const meta = document.createElement("div");
const title = document.createElement("div");
title.className = "queue-title";
title.textContent = item.title || "-";
const subtitle = document.createElement("div");
subtitle.className = "queue-subtitle";
subtitle.textContent = item.subtitle || "-";
meta.appendChild(title);
meta.appendChild(subtitle);
row.appendChild(meta);
const button = document.createElement("button");
button.className = "icon-button queue-play";
button.type = "button";
button.title = tr("play");
button.setAttribute("aria-label", tr("play"));
button.disabled = !spotifyControlsEnabled || !item.uri;
button.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14l11-7z"></path></svg>';
button.addEventListener("click", () => playQueueItem(item.uri, item.title, item.subtitle, item.index));
row.appendChild(button);
list.appendChild(row);
}
$("queueStatus").textContent = "";
} catch (error) {
list.innerHTML = '<div class="fine"></div>';
}
}
async function loadPlaylists() {
if (!playlistsVisible) return;
const list = $("playlistList");
playlistsLoadedAt = Date.now();
try {
const data = await fetchPlaybackJson("/api/playlists");
list.innerHTML = "";
if (!data.items || data.items.length === 0) {
list.innerHTML = `<div class="fine">${data.error || tr("noPlaylists")}</div>`;
return;
}
for (const playlist of data.items) {
const row = document.createElement("div");
row.className = "queue-item";
if (playlist.imageUrl) {
const art = document.createElement("img");
art.className = "queue-art";
art.alt = "";
art.loading = "lazy";
art.decoding = "async";
art.referrerPolicy = "no-referrer";
art.src = playlist.imageUrl;
row.appendChild(art);
} else {
const art = document.createElement("div");
art.className = "queue-art empty";
art.textContent = "♪";
row.appendChild(art);
}
const meta = document.createElement("div");
const title = document.createElement("div");
title.className = "queue-title";
title.textContent = playlist.name || "-";
const subtitle = document.createElement("div");
subtitle.className = "queue-subtitle";
subtitle.textContent = playlist.owner || "-";
meta.appendChild(title);
meta.appendChild(subtitle);
row.appendChild(meta);
const button = document.createElement("button");
button.className = "icon-button queue-play";
button.type = "button";
button.title = tr("play");
button.setAttribute("aria-label", tr("play"));
button.disabled = !spotifyControlsEnabled || !playlist.uri;
button.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 5v14l11-7z"></path></svg>';
button.addEventListener("click", () => playPlaylist(playlist.uri));
row.appendChild(button);
list.appendChild(row);
}
$("playlistStatus").textContent = "";
} catch (error) {
list.innerHTML = `<div class="fine">${tr("playlistsFailed")}</div>`;
}
}
async function refreshLogs() {
if (logsPaused || !logsVisible) return;
const response = await fetch("/api/logs", { cache: "no-store" });
const value = await response.text();
const logs = $("logs");
logs.textContent = value || tr("noLogs");
logs.scrollTop = logs.scrollHeight;
}
function queueVolumeUpdate() {
if (!spotifyControlsEnabled) return;
clearTimeout(volumeTimer);
const value = $("volumeSlider").value;
text("volume", `${value}%`);
$("volumeStatus").textContent = `Volume ${value}%`;
volumeTimer = setTimeout(async () => {
const body = new URLSearchParams({ volume: value });
const response = await fetch("/api/volume", { method:"POST", body });
$("volumeStatus").textContent = await response.text();
}, 250);
}
$("volumeSlider").addEventListener("input", queueVolumeUpdate);
$("albumArt").addEventListener("click", openAlbumArtPopover);
$("albumArt").addEventListener("keydown", event => {
if (event.key !== "Enter" && event.key !== " ") return;
event.preventDefault();
openAlbumArtPopover();
});
$("albumArtClose").addEventListener("click", closeAlbumArtPopover);
$("albumArtPopover").addEventListener("click", event => {
if (event.target === $("albumArtPopover")) closeAlbumArtPopover();
});
window.addEventListener("keydown", event => {
if (event.key === "Escape" && $("albumArtPopover").classList.contains("open")) {
closeAlbumArtPopover();
}
});
$("soundOutputSelect").addEventListener("change", async () => {
if (!spotifyControlsEnabled) return;
const deviceId = $("soundOutputSelect").value;
if (!deviceId) return;
$("soundOutputStatus").textContent = tr("switchingOutput");
const body = new URLSearchParams({ deviceId });
const response = await fetch("/api/transfer", { method:"POST", body });
$("soundOutputStatus").textContent = await response.text();
await refresh();
await loadSoundOutputs();
});
async function sendPlaybackCommand(action) {
if (!spotifyControlsEnabled) return;
$("playbackCommandStatus").textContent = action === "next" ? tr("skipping") : action === "previous" ? tr("goingBack") : action === "play" ? tr("playing") : action === "pause" ? tr("paused") : tr("startingLiked");
const body = new URLSearchParams({ action });
const response = await fetch("/api/playback", { method:"POST", body });
$("playbackCommandStatus").textContent = await response.text();
await refresh();
await loadQueue();
}
async function playQueueItem(uri, title, artist, index) {
if (!spotifyControlsEnabled || !uri) return;
$("queueStatus").textContent = tr("startingQueueItem");
const body = new URLSearchParams({ action:"queue_item", uri });
if (title) body.set("title", title);
if (artist) body.set("artist", artist);
if (Number.isInteger(index)) body.set("index", String(index));
const response = await fetch("/api/playback", { method:"POST", body });
$("queueStatus").textContent = await response.text();
await refresh();
await loadQueue();
}
async function playPlaylist(uri) {
if (!spotifyControlsEnabled || !uri) return;
$("playlistStatus").textContent = tr("startingPlaylist");
const body = new URLSearchParams({ action:"playlist", uri });
const response = await fetch("/api/playback", { method:"POST", body });
$("playlistStatus").textContent = await response.text();
await refresh();
await loadQueue();
}
$("refreshQueueButton").addEventListener("click", async () => {
if (!spotifyControlsEnabled) return;
queueLoadedAt = 0;
$("queueStatus").textContent = tr("refreshing");
await loadQueue();
});
$("refreshPlaylistsButton").addEventListener("click", async () => {
if (!spotifyControlsEnabled) return;
playlistsLoadedAt = 0;
$("playlistStatus").textContent = tr("refreshing");
await loadPlaylists();
});
$("previousButton").addEventListener("click", () => sendPlaybackCommand("previous"));
$("nextButton").addEventListener("click", () => sendPlaybackCommand("next"));
$("playPauseButton").addEventListener("click", () => sendPlaybackCommand(playbackIsPlaying ? "pause" : "play"));
$("startLikedProxyButton").addEventListener("click", () => sendPlaybackCommand("likedProxy"));
$("webPttButton").addEventListener("click", event => {
event.preventDefault();
startWebPtt();
});
$("pauseLogsButton").addEventListener("click", () => {
logsPaused = !logsPaused;
$("pauseLogsButton").textContent = logsPaused ? tr("resumeLogs") : tr("pauseLogs");
$("logsStatus").textContent = "";
if (!logsPaused) refreshLogs();
});
$("copyLogsButton").addEventListener("click", async () => {
logsPaused = true;
$("pauseLogsButton").textContent = tr("resumeLogs");
const range = document.createRange();
range.selectNodeContents($("logs"));
const selection = window.getSelection();
selection.removeAllRanges();
selection.addRange(range);
$("logsStatus").textContent = tr("logsPausedSelected");
});
$("settingsForm").addEventListener("submit", async event => {
event.preventDefault();
$("settingsStatus").textContent = "";
const body = new URLSearchParams({
brightness: $("brightness").value,
offTimeoutMs: $("offTimeout").value,
sleepTimeoutMs: $("sleepTimeout").value,
speakerVolume: $("speakerVolume").value,
language: $("language").value,
theme: $("theme").value,
logLevel: $("logLevel").value,
wakeWordEnabled: $("wakeWordEnabled").value
});
const response = await fetch("/api/settings", { method:"POST", body });
$("settingsStatus").textContent = await response.text();
clearDirtyInputs(settingsInputIds);
await refresh();
});
$("shuffleButton").addEventListener("click", async () => {
if (!spotifyControlsEnabled) return;
$("shuffleStatus").textContent = tr("refreshing");
const nextShuffle = shuffleEnabled ? "off" : "on";
const body = new URLSearchParams({ enabled: nextShuffle });
const response = await fetch("/api/shuffle", { method:"POST", body });
$("shuffleStatus").textContent = await response.text();
await refresh();
});
$("repeatButton").addEventListener("click", async () => {
if (!spotifyControlsEnabled) return;
$("repeatStatus").textContent = tr("refreshing");
const nextRepeat = repeatState === "off" ? "context" : repeatState === "context" ? "track" : "off";
const body = new URLSearchParams({ repeat: nextRepeat });
const response = await fetch("/api/repeat", { method:"POST", body });
$("repeatStatus").textContent = await response.text();
await refresh();
});
$("wifiForm").addEventListener("submit", async event => {
event.preventDefault();
if (!confirm(tr("testWifiConfirm"))) return;
$("wifiSettingsStatus").textContent = tr("startingWifiTest");
const body = new URLSearchParams({
ssid: $("wifiNewSsid").value,
password: $("wifiNewPassword").value
});
const response = await fetch("/api/wifi", { method:"POST", body });
$("wifiSettingsStatus").textContent = await response.text();
clearDirtyInputs(["wifiNewSsid", "wifiNewPassword"]);
});
$("refreshButton").addEventListener("click", async () => {
$("refreshStatus").textContent = tr("refreshing");
const response = await fetch("/api/refresh", { method:"POST" });
$("refreshStatus").textContent = await response.text();
refresh();
});
$("rebootButton").addEventListener("click", async () => {
if (!confirm(tr("restartConfirm"))) return;
$("otaStatus").textContent = "";
await fetch("/api/reboot", { method:"POST" });
});
$("resetPairingButton").addEventListener("click", async () => {
if (!confirm(tr("resetPairingConfirm"))) return;
$("otaStatus").textContent = await (await fetch("/api/reset-pairing", { method:"POST" })).text();
});
$("hardResetButton").addEventListener("click", async () => {
if (!confirm(tr("factoryResetConfirm"))) return;
$("otaStatus").textContent = await (await fetch("/api/hard-reset", { method:"POST" })).text();
});
$("otaForm").addEventListener("submit", async event => {
event.preventDefault();
const file = $("firmware").files[0];
if (!file) return;
$("otaStatus").textContent = "";
const form = new FormData();
form.append("firmware", file, file.name);
const response = await fetch("/ota", { method:"POST", body:form });
$("otaStatus").textContent = await response.text();
});
setupStatusStyling();
setupVisibilityObservers();
setupDirtyInputTracking();
setupGames();
refresh();
loadPairingInfo();
setInterval(refresh, 3000);
setInterval(refreshLogs, 1000);
</script></body></html>
)rawliteral";

void WebPortal::begin(
    const SpotifyState &playback,
    const BatteryState &battery,
    const RuntimeDiagnostics &diagnostics,
    const VisualState &visualState,
    SpotifyClient &spotify,
    LedRing &ledRing,
    DisplayManager &display,
    SoundManager &sound,
    const uint8_t &screenBrightnessPercent,
    const uint8_t &speakerVolumePercent,
    const bool &homeAssistantPaired,
    const String &languageCode,
    const String &themeCode,
    const String &logLevel,
    const bool &wakeWordEnabled,
    const uint32_t &screenOffTimeoutMs,
    const uint32_t &deviceSleepTimeoutMs,
    void *callbackContext,
    SettingsCallback settingsCallback,
    WifiSettingsCallback wifiSettingsCallback,
    VoiceTextCallback voiceTextCallback,
    SimpleCallback refreshCallback,
    SimpleCallback resetPairingCallback,
    SimpleCallback hardResetCallback) {
  playback_ = &playback;
  battery_ = &battery;
  diagnostics_ = &diagnostics;
  visualState_ = &visualState;
  spotify_ = &spotify;
  ledRing_ = &ledRing;
  display_ = &display;
  sound_ = &sound;
  screenBrightnessPercent_ = &screenBrightnessPercent;
  speakerVolumePercent_ = &speakerVolumePercent;
  homeAssistantPaired_ = &homeAssistantPaired;
  languageCode_ = &languageCode;
  themeCode_ = &themeCode;
  logLevel_ = &logLevel;
  wakeWordEnabled_ = &wakeWordEnabled;
  screenOffTimeoutMs_ = &screenOffTimeoutMs;
  deviceSleepTimeoutMs_ = &deviceSleepTimeoutMs;
  callbackContext_ = callbackContext;
  settingsCallback_ = settingsCallback;
  wifiSettingsCallback_ = wifiSettingsCallback;
  voiceTextCallback_ = voiceTextCallback;
  refreshCallback_ = refreshCallback;
  resetPairingCallback_ = resetPairingCallback;
  hardResetCallback_ = hardResetCallback;

  if (running_) {
    return;
  }

  configureRoutes();
  server_.begin();
  running_ = true;
  AppLog.print("Web portal: http://");
  AppLog.println(WiFi.localIP());
}

void WebPortal::handle() {
  if (running_) {
    server_.handleClient();
  }
}

bool WebPortal::isRunning() const {
  return running_;
}

WebServer &WebPortal::server() {
  return server_;
}

void WebPortal::configureRoutes() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/favicon.ico", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/x-icon", DJCONNECT_FAVICON_ICO, DJCONNECT_FAVICON_ICO_LEN);
  });
  server_.on("/icon-192.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/apple-touch-icon.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/apple-touch-icon-precomposed.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/icons/favicon.ico", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/x-icon", DJCONNECT_FAVICON_ICO, DJCONNECT_FAVICON_ICO_LEN);
  });
  server_.on("/icons/icon-192.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/icons/icon-512.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/icons/maskable-icon-192.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/icons/maskable-icon-512.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", DJCONNECT_ICON_192_PNG, DJCONNECT_ICON_192_PNG_LEN);
  });
  server_.on("/site.webmanifest", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "application/manifest+json", DJCONNECT_SITE_WEBMANIFEST, DJCONNECT_SITE_WEBMANIFEST_LEN, 86400UL);
  });
  server_.on("/icons/site.webmanifest", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "application/manifest+json", DJCONNECT_SITE_WEBMANIFEST, DJCONNECT_SITE_WEBMANIFEST_LEN, 86400UL);
  });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatusJson(); });
  server_.on("/api/logs", HTTP_GET, [this]() { handleLogsText(); });
  server_.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_.on("/api/shuffle", HTTP_POST, [this]() { handleShufflePost(); });
  server_.on("/api/repeat", HTTP_POST, [this]() { handleRepeatPost(); });
  server_.on("/api/wifi", HTTP_POST, [this]() { handleWifiPost(); });
  server_.on("/api/volume", HTTP_POST, [this]() { handleVolumePost(); });
  server_.on("/api/devices", HTTP_GET, [this]() { handleDevicesJson(); });
  server_.on("/api/playlists", HTTP_GET, [this]() { handlePlaylistsJson(); });
  server_.on("/api/queue", HTTP_GET, [this]() { handleQueueJson(); });
  server_.on("/api/transfer", HTTP_POST, [this]() { handleTransferPost(); });
  server_.on("/api/playback", HTTP_POST, [this]() { handlePlaybackCommandPost(); });
  server_.on("/api/voice-text", HTTP_POST, [this]() { handleVoiceTextPost(); });
  server_.on("/api/refresh", HTTP_POST, [this]() { handleRefreshPost(); });
  server_.on("/api/reset-pairing", HTTP_POST, [this]() { handleResetPairingPost(); });
  server_.on("/api/reboot", HTTP_POST, [this]() { handleRebootPost(); });
  server_.on("/api/hard-reset", HTTP_POST, [this]() { handleHardResetPost(); });
  server_.on(
      "/ota",
      HTTP_POST,
      [this]() { handleOtaFinished(); },
      [this]() { handleOtaUpload(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

void WebPortal::handleRoot() {
  sendNoStore(server_);
  server_.send_P(200, "text/html", IndexHtml);
}

void WebPortal::handleStatusJson() {
  if (playback_ == nullptr || battery_ == nullptr || diagnostics_ == nullptr || spotify_ == nullptr) {
    sendNoStore(server_);
    server_.send(503, "application/json", "{\"error\":\"portal not ready\"}");
    return;
  }

  JsonDocument doc;
  JsonObject app = doc["app"].to<JsonObject>();
  app["name"] = "DJConnect";
  app["version"] = appVersionLabel();
  app["model"] = Config::DeviceModel;
  app["uptimeMs"] = millis();

  JsonObject playback = doc["playback"].to<JsonObject>();
  playback["hasPlayback"] = playback_->hasPlayback;
  playback["isPlaying"] = playback_->isPlaying;
  playback["track"] = playback_->trackName;
  playback["artist"] = playback_->artistName;
  playback["type"] = playback_->currentType;
  playback["albumImageUrl"] = playback_->albumImageUrl;
  playback["shuffle"] = playback_->shuffle;
  playback["repeatState"] = playback_->repeatState;
  playback["progressMs"] = estimatedProgressMs();
  playback["durationMs"] = playback_->durationMs;
  playback["progressPercent"] = playback_->durationMs > 0
                                     ? (estimatedProgressMs() * 100) / playback_->durationMs
                                     : 0;

  JsonObject device = doc["device"].to<JsonObject>();
  device["name"] = playback_->deviceName;
  device["id"] = playback_->deviceId;
  device["type"] = playback_->deviceType;
  device["supportsVolume"] = playback_->supportsVolume;
  device["volume"] = playback_->volume;

  JsonObject battery = doc["battery"].to<JsonObject>();
  battery["available"] = battery_->available;
  battery["label"] = batteryLabel();
  battery["percent"] = battery_->percent;
  battery["gaugePercent"] = battery_->gaugePercent;
  battery["estimated"] = battery_->percentEstimated;
  battery["voltageMv"] = battery_->voltageMv;
  battery["currentMa"] = battery_->currentMa;
  battery["charging"] = battery_->charging;
  battery["discharging"] = battery_->discharging;
  battery["full"] = battery_->full;

  JsonObject settings = doc["settings"].to<JsonObject>();
  settings["screenBrightnessPercent"] = *screenBrightnessPercent_;
  settings["speakerVolumePercent"] = speakerVolumePercent_ == nullptr ? 100 : *speakerVolumePercent_;
  settings["language"] = languageCode_ == nullptr ? "en" : *languageCode_;
  settings["theme"] = themeCode_ == nullptr ? "dark" : *themeCode_;
  settings["logLevel"] = logLevel_ == nullptr ? "info" : *logLevel_;
  settings["wakeWordEnabled"] = wakeWordEnabled_ != nullptr && *wakeWordEnabled_;
  settings["screenOffTimeoutMs"] = *screenOffTimeoutMs_;
  settings["screenDimStartAfterMs"] = Config::DisplayDimStartAfterMs;
  settings["deviceSleepTimeoutMs"] = deviceSleepTimeoutMs_ == nullptr ? Config::DeviceSleepAfterMs : *deviceSleepTimeoutMs_;
  settings["screenDimAfterMs"] = Config::DisplayDimAfterMs;
  settings["screenDimBrightnessPercent"] = Config::DisplayDimBrightnessPercent;

  JsonObject screen = doc["screen"].to<JsonObject>();
  const bool screenOn = visualState_ != nullptr && visualState_->screenOn;
  screen["state"] = screenOn ? "on" : "off";
  screen["brightnessLevel"] = visualState_ == nullptr ? 0 : visualState_->screenBrightnessLevel;

  JsonObject led = doc["led"].to<JsonObject>();
  const bool ledOn = visualState_ != nullptr && visualState_->ledOn;
  led["state"] = ledOn ? "on" : "off";

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  wifi["connected"] = wifiConnected;
  wifi["status"] = wifiStatusText();
  wifi["ip"] = wifiConnected ? WiFi.localIP().toString() : "";
  wifi["ssid"] = wifiConnected ? WiFi.SSID() : "";
  wifi["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
  wifi["mac"] = WiFi.macAddress();

  JsonObject spotify = doc["spotify"].to<JsonObject>();
  spotify["authorized"] = spotify_->isAuthorized();
  const bool playbackError = playback_->error.startsWith("HA playback HTTP") ||
                             playback_->error == "HA playback cooling down" ||
                             playback_->error == "Playback proxy busy";
  spotify["status"] = !spotify_->isAuthorized() || playbackError
                          ? "error"
                          : !playback_->hasPlayback ? "idle" : "ok";
  spotify["error"] = playback_->error;

  JsonObject ha = doc["ha"].to<JsonObject>();
  ha["paired"] = homeAssistantPaired_ != nullptr && *homeAssistantPaired_;

  JsonObject voice = doc["voice"].to<JsonObject>();
  voice["available"] = voiceTextCallback_ != nullptr &&
                       wifiConnected &&
                       homeAssistantPaired_ != nullptr &&
                       *homeAssistantPaired_;

  JsonObject system = doc["system"].to<JsonObject>();
  const uint32_t heapTotal = ESP.getHeapSize();
  const uint32_t heapFree = ESP.getFreeHeap();
  const uint32_t sketchSize = ESP.getSketchSize();
  const uint32_t otaFree = ESP.getFreeSketchSpace();
  system["heapTotal"] = heapTotal;
  system["heapFree"] = heapFree;
  system["heapUsed"] = heapTotal > heapFree ? heapTotal - heapFree : 0;
  system["heapMinFree"] = ESP.getMinFreeHeap();
  system["heapMaxAlloc"] = ESP.getMaxAllocHeap();
  system["cpuUsagePercent"] = diagnostics_->cpuUsagePercent;
  system["lastLoopDurationMs"] = diagnostics_->lastLoopDurationMs;
  system["maxLoopDurationMs"] = diagnostics_->maxLoopDurationMs;
  system["loopCount"] = diagnostics_->loopCount;
  system["flashSize"] = ESP.getFlashChipSize();
  system["sketchSize"] = sketchSize;
  system["otaFree"] = otaFree;
  system["otaTotal"] = sketchSize + otaFree;

  JsonObject dj = doc["dj"].to<JsonObject>();
  dj["last_dj_text"] = diagnostics_->lastDjText;

  String body;
  serializeJson(doc, body);
  sendNoStore(server_);
  server_.send(200, "application/json", body);
}

void WebPortal::handleLogsText() {
  sendNoStore(server_);
  server_.send(200, "text/plain", AppLog.text());
}

void WebPortal::handleSettingsPost() {
  if (!server_.hasArg("brightness") || !server_.hasArg("offTimeoutMs") || !server_.hasArg("sleepTimeoutMs")) {
    server_.send(400, "text/plain", "Missing brightness, offTimeoutMs or sleepTimeoutMs");
    return;
  }

  const uint8_t brightness = constrain(server_.arg("brightness").toInt(), 25, 100);
  const uint32_t offTimeoutMs = constrain(server_.arg("offTimeoutMs").toInt(), 30000, 240000);
  const uint32_t sleepTimeoutMs = constrain(server_.arg("sleepTimeoutMs").toInt(), 300000, 3600000);
  const uint8_t speakerVolume = constrain(server_.hasArg("speakerVolume") ? server_.arg("speakerVolume").toInt() : 100, 25, 100);
  String language = server_.hasArg("language") ? server_.arg("language") : "en";
  language.toLowerCase();
  if (language != "nl") {
    language = "en";
  }
  String theme = server_.hasArg("theme") ? server_.arg("theme") : "dark";
  theme.toLowerCase();
  if (theme != "auto" && theme != "light") {
    theme = "dark";
  }
  String logLevel = server_.hasArg("logLevel") ? server_.arg("logLevel") : "info";
  logLevel.toLowerCase();
  if (logLevel != "debug" && logLevel != "warning" && logLevel != "error") {
    logLevel = "info";
  }
  const bool wakeWordEnabled = server_.hasArg("wakeWordEnabled") && server_.arg("wakeWordEnabled") == "1";
  AppLog.print("Web settings: brightness=");
  AppLog.print(brightness);
  AppLog.print("% dim=");
  AppLog.print(offTimeoutMs / 1000UL);
  AppLog.print("s sleep=");
  AppLog.print(sleepTimeoutMs / 60000UL);
  AppLog.print("m speaker=");
  AppLog.print(speakerVolume);
  AppLog.print("% log_level=");
  AppLog.print(logLevel);
  AppLog.print(" wake_word=");
  AppLog.print(wakeWordEnabled ? "on" : "off");
  AppLog.println();
  if (settingsCallback_ != nullptr) {
    settingsCallback_(callbackContext_, brightness, offTimeoutMs, sleepTimeoutMs, speakerVolume, language, theme, logLevel, wakeWordEnabled);
  }

  server_.send(200, "text/plain", "Settings saved");
}

void WebPortal::handleShufflePost() {
  if (!server_.hasArg("enabled")) {
    server_.send(400, "text/plain", "Missing shuffle value");
    return;
  }
  if (spotify_ == nullptr || !spotify_->isAuthorized()) {
    server_.send(409, "text/plain", localizedText("Playback not connected", "Afspelen niet verbonden"));
    return;
  }

  const String value = server_.arg("enabled");
  const bool enabled = value == "on" || value == "true" || value == "1";
  AppLog.print("Web playback: shuffle ");
  AppLog.println(enabled ? "on" : "off");
  if (!spotify_->setShuffle(enabled)) {
    AppLog.print("Web playback: shuffle failed: ");
    AppLog.println(playback_ == nullptr || playback_->error.isEmpty() ? "unknown" : playback_->error);
    server_.send(500, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Shuffle failed" : playback_->error);
    return;
  }
  server_.send(200, "text/plain", localizedText(enabled ? "Shuffle on" : "Shuffle off", enabled ? "Shuffle aan" : "Shuffle uit"));
}

void WebPortal::handleRepeatPost() {
  if (!server_.hasArg("repeat")) {
    server_.send(400, "text/plain", "Missing repeat value");
    return;
  }
  if (spotify_ == nullptr || !spotify_->isAuthorized()) {
    server_.send(409, "text/plain", localizedText("Playback not connected", "Afspelen niet verbonden"));
    return;
  }

  const String repeat = server_.arg("repeat");
  AppLog.print("Web playback: repeat ");
  AppLog.println(repeat);
  if (!spotify_->setRepeatMode(repeat)) {
    AppLog.print("Web playback: repeat failed: ");
    AppLog.println(playback_ == nullptr || playback_->error.isEmpty() ? "unknown" : playback_->error);
    server_.send(500, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Repeat failed" : playback_->error);
    return;
  }
  if (repeat == "track") {
    server_.send(200, "text/plain", localizedText("Repeat once", "Eenmaal herhalen"));
  } else if (repeat == "context") {
    server_.send(200, "text/plain", localizedText("Repeat infinite", "Oneindig herhalen"));
  } else {
    server_.send(200, "text/plain", localizedText("Repeat off", "Herhalen uit"));
  }
}

void WebPortal::handleWifiPost() {
  if (wifiSettingsCallback_ == nullptr || !server_.hasArg("ssid")) {
    server_.send(400, "text/plain", "Missing WiFi SSID");
    return;
  }

  String ssid = server_.arg("ssid");
  ssid.trim();
  if (ssid.isEmpty()) {
    server_.send(400, "text/plain", "WiFi SSID is required");
    return;
  }

  wifiSettingsCallback_(callbackContext_, ssid, server_.arg("password"));
  server_.send(202, "text/plain", localizedText(
                                     "WiFi test started. Device will restart automatically if the connection succeeds.",
                                     "WiFi-test gestart. Het device herstart automatisch als de verbinding lukt."));
}

void WebPortal::handleVolumePost() {
  if (spotify_ == nullptr || !server_.hasArg("volume")) {
    server_.send(400, "text/plain", "Missing volume");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableText();
    return;
  }

  const int volume = constrain(server_.arg("volume").toInt(), 0, Config::MaxSpotifyVolumePercent);
  if (!spotify_->queueVolume(volume)) {
    server_.send(503, "text/plain", "Volume queue unavailable");
    return;
  }

  server_.send(202, "text/plain", "Volume queued " + String(volume) + "%");
}

void WebPortal::handleDevicesJson() {
  if (spotify_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"playback not ready\"}");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableJson("devices");
    return;
  }

  DeviceListState *devices = new (std::nothrow) DeviceListState();
  if (devices == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"devices memory unavailable\"}");
    return;
  }
  spotify_->refreshDevices(*devices);

  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"available\":");
  server_.sendContent(devices->available ? "true" : "false");
  server_.sendContent(",\"error\":\"");
  sendJsonEscapedContent(server_, devices->error);
  server_.sendContent("\",\"devices\":[");
  for (size_t index = 0; index < devices->count; index++) {
    const SpotifyDeviceState &device = devices->devices[index];
    if (index > 0) {
      server_.sendContent(",");
    }
    server_.sendContent("{\"id\":\"");
    sendJsonEscapedContent(server_, device.id);
    server_.sendContent("\",\"name\":\"");
    sendJsonEscapedContent(server_, device.name);
    server_.sendContent("\",\"active\":");
    server_.sendContent(device.active ? "true" : "false");
    server_.sendContent(",\"supportsVolume\":");
    server_.sendContent(device.supportsVolume ? "true" : "false");
    server_.sendContent("}");
    yield();
  }
  server_.sendContent("]}");
  delete devices;
}

void WebPortal::handleQueueJson() {
  if (spotify_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"playback not ready\"}");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableJson("items");
    return;
  }

  QueueState *queue = new (std::nothrow) QueueState();
  if (queue == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"queue memory unavailable\"}");
    return;
  }
  spotify_->refreshQueue(*queue);

  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"available\":");
  server_.sendContent(queue->available ? "true" : "false");
  server_.sendContent(",\"error\":\"");
  sendJsonEscapedContent(server_, queue->error);
  server_.sendContent("\",\"items\":[");
  for (size_t index = 0; index < queue->count; index++) {
    const QueueItemState &item = queue->items[index];
    if (index > 0) {
      server_.sendContent(",");
    }
    server_.sendContent("{\"title\":\"");
    sendJsonEscapedContent(server_, item.title);
    server_.sendContent("\",\"subtitle\":\"");
    sendJsonEscapedContent(server_, item.subtitle);
    server_.sendContent("\",\"uri\":\"");
    sendJsonEscapedContent(server_, item.uri);
    server_.sendContent("\",\"imageUrl\":\"");
    sendJsonEscapedContent(server_, item.imageUrl);
    server_.sendContent("\",\"index\":");
    server_.sendContent(String(index));
    server_.sendContent("}");
    yield();
  }
  server_.sendContent("]}");
  delete queue;
}

void WebPortal::handlePlaylistsJson() {
  if (spotify_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"playback not ready\"}");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableJson("items");
    return;
  }

  PlaylistListState *playlists = new (std::nothrow) PlaylistListState();
  if (playlists == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"playlists memory unavailable\"}");
    return;
  }
  spotify_->refreshPlaylists(*playlists);

  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"available\":");
  server_.sendContent(playlists->available ? "true" : "false");
  server_.sendContent(",\"error\":\"");
  sendJsonEscapedContent(server_, playlists->error);
  server_.sendContent("\",\"items\":[");
  for (size_t index = 0; index < playlists->count; index++) {
    const PlaylistItemState &playlist = playlists->items[index];
    if (index > 0) {
      server_.sendContent(",");
    }
    server_.sendContent("{\"name\":\"");
    sendJsonEscapedContent(server_, playlist.name);
    server_.sendContent("\",\"owner\":\"");
    sendJsonEscapedContent(server_, playlist.owner);
    server_.sendContent("\",\"uri\":\"");
    sendJsonEscapedContent(server_, playlist.uri);
    server_.sendContent("\",\"imageUrl\":\"");
    sendJsonEscapedContent(server_, playlist.imageUrl);
    server_.sendContent("\"}");
    yield();
  }
  server_.sendContent("]}");
  delete playlists;
}

void WebPortal::handleTransferPost() {
  if (spotify_ == nullptr || !server_.hasArg("deviceId")) {
    server_.send(400, "text/plain", "Missing output");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableText();
    return;
  }

  String deviceId = server_.arg("deviceId");
  if (deviceId.isEmpty()) {
    server_.send(400, "text/plain", "Missing output");
    return;
  }

  if (deviceId == "none") {
    AppLog.println("Web playback: stopping output via none selection");
    if (!spotify_->pausePlayback()) {
      AppLog.print("Web playback: stop failed: ");
      AppLog.println(playback_ == nullptr || playback_->error.isEmpty() ? "unknown" : playback_->error);
      server_.send(502, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Playback stop failed" : playback_->error);
      return;
    }
    spotify_->refreshPlayback();
    server_.send(200, "text/plain", localizedText("Playback stopped", "Playback gestopt"));
    return;
  }

  DeviceListState devices;
  spotify_->refreshDevices(devices);
  String lowerRequested = deviceId;
  lowerRequested.toLowerCase();
  for (size_t index = 0; index < devices.count; index++) {
    String lowerName = devices.devices[index].name;
    lowerName.toLowerCase();
    const bool iphoneAlias = lowerRequested == "iphone" && lowerName.indexOf("iphone") >= 0;
    if (devices.devices[index].id == deviceId || devices.devices[index].name == deviceId || iphoneAlias) {
      deviceId = devices.devices[index].id;
      break;
    }
  }

  AppLog.println("Web playback: transfer output requested");
  if (!spotify_->transferPlayback(deviceId, true)) {
    AppLog.print("Web playback: transfer failed: ");
    AppLog.println(playback_ == nullptr || playback_->error.isEmpty() ? "unknown" : playback_->error);
    server_.send(502, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Output switch failed" : playback_->error);
    return;
  }

  AppLog.println("Web playback: output switched");
  spotify_->refreshPlayback();
  server_.send(200, "text/plain", localizedText("Output switched", "Output gewisseld"));
}

void WebPortal::handlePlaybackCommandPost() {
  if (spotify_ == nullptr || !server_.hasArg("action")) {
    server_.send(400, "text/plain", "Missing playback action");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableText();
    return;
  }

  const String action = server_.arg("action");
  AppLog.print("Web playback: action ");
  AppLog.println(action);
  bool ok = false;
  if (action == "next") {
    ok = spotify_->nextTrack();
  } else if (action == "previous") {
    ok = spotify_->previousTrack();
  } else if (action == "play") {
    ok = spotify_->resumePlayback();
  } else if (action == "pause") {
    ok = spotify_->pausePlayback();
  } else if (action == "likedProxy") {
    ok = spotify_->startLikedProxyPlaylist();
  } else if (action == "playlist") {
    const String playlistUri = server_.arg("uri");
    if (playlistUri.isEmpty()) {
      server_.send(400, "text/plain", "Missing playlist");
      return;
    }
    ok = spotify_->startPlaylist(playlistUri);
  } else if (action == "queue_item") {
    const String itemUri = server_.arg("uri");
    const String title = server_.arg("title");
    const String artist = server_.arg("artist");
    const int index = server_.hasArg("index") ? server_.arg("index").toInt() : -1;
    String contextUri = playback_ == nullptr ? "" : playback_->contextUri;
    if (itemUri.isEmpty()) {
      server_.send(400, "text/plain", "Missing queue item");
      return;
    }
    if (contextUri.isEmpty()) {
      QueueState queue;
      if (spotify_->refreshQueue(queue)) {
        contextUri = queue.contextUri;
      }
    }
    ok = spotify_->playQueueItem(itemUri, contextUri, title, artist, index);
  } else {
    server_.send(400, "text/plain", "Unknown playback action");
    return;
  }

  if (!ok) {
    AppLog.print("Web playback: action failed: ");
    AppLog.println(playback_ == nullptr || playback_->error.isEmpty() ? "unknown" : playback_->error);
    server_.send(502, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Playback command failed" : playback_->error);
    return;
  }

  AppLog.print("Web playback: action completed ");
  AppLog.println(action);
  spotify_->refreshPlayback();
  String message = localizedText("Default playlist started", "Standaard playlist gestart");
  if (action == "next") {
    message = localizedText("Next song", "Volgend nummer");
  } else if (action == "previous") {
    message = localizedText("Previous song", "Vorig nummer");
  } else if (action == "play") {
    message = localizedText("Playing", "Speelt");
  } else if (action == "pause") {
    message = localizedText("Paused", "Gepauzeerd");
  } else if (action == "playlist") {
    message = localizedText("Playlist started", "Afspeellijst gestart");
  } else if (action == "queue_item") {
    message = localizedText("Selected song started", "Gekozen nummer gestart");
  }
  server_.send(200, "text/plain", message);
}

void WebPortal::handleVoiceTextPost() {
  if (voiceTextCallback_ == nullptr || !server_.hasArg("text")) {
    server_.send(400, "application/json", "{\"success\":false,\"message\":\"Missing voice text\"}");
    return;
  }
  String voiceText = server_.arg("text");
  voiceText.trim();
  if (voiceText.isEmpty()) {
    server_.send(400, "application/json", "{\"success\":false,\"message\":\"Missing voice text\"}");
    return;
  }

  AppLog.print("Web voice: text chars=");
  AppLog.println(voiceText.length());
  String message;
  String audioUrl;
  bool ok = false;
  {
    ScopedWatchdogPause watchdogPause;
    ok = voiceTextCallback_(callbackContext_, voiceText, message, audioUrl);
  }
  ScopedWatchdogPause::resetIfAttached();

  JsonDocument doc;
  doc["success"] = ok;
  doc["message"] = message;
  if (!audioUrl.isEmpty()) {
    doc["audio_url"] = audioUrl;
  }
  String payload;
  serializeJson(doc, payload);
  server_.send(ok ? 200 : 502, "application/json", payload);
}

void WebPortal::handleRefreshPost() {
  if (refreshCallback_ != nullptr) {
    refreshCallback_(callbackContext_);
  }
  server_.send(200, "text/plain", localizedText("Refresh requested", "Verversen aangevraagd"));
}

void WebPortal::handleResetPairingPost() {
  AppLog.println("Web action: reset Home Assistant pairing requested");
  server_.send(200, "text/plain", localizedText(
                                      "Reset pairing requested. Restarting to pairing screen...",
                                      "Koppeling resetten aangevraagd. Herstarten naar koppelscherm..."));
  delay(250);
  if (resetPairingCallback_ != nullptr) {
    resetPairingCallback_(callbackContext_);
  }
  ESP.restart();
}

void WebPortal::handleRebootPost() {
  AppLog.println("Web action: restart requested");
  server_.send(200, "text/plain", localizedText("Restarting device...", "Device herstarten..."));
  delay(250);
  ESP.restart();
}

void WebPortal::handleHardResetPost() {
  AppLog.println("Web action: factory reset requested");
  server_.send(200, "text/plain", localizedText(
                                      "Factory reset requested. Restarting into setup mode...",
                                      "Fabrieksreset wordt uitgevoerd..."));
  delay(250);
  if (hardResetCallback_ != nullptr) {
    hardResetCallback_(callbackContext_);
  }
}

void WebPortal::handleOtaFinished() {
  if (otaOk_ && !Update.hasError()) {
    if (sound_ != nullptr) {
      sound_->playOtaComplete();
      delay(320);
    }
    server_.send(200, "text/plain", localizedText("Firmware uploaded. Restarting...", "Firmware geupload. Herstarten..."));
    delay(500);
    ESP.restart();
    return;
  }

  if (sound_ != nullptr) {
    sound_->playOtaFailed();
  }
  server_.send(500, "text/plain", "Firmware update failed");
}

void WebPortal::handleOtaUpload() {
  HTTPUpload &upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaUploadedBytes_ = 0;
    otaLastProgressCue_ = 0;
    if (display_ != nullptr) {
      display_->forceBacklightPercent(100);
      if (battery_ != nullptr) {
        display_->showBootMessage(I18n::text("firmware_update_progress"), *battery_);
      } else {
        display_->showBootMessage(I18n::text("firmware_update_progress"));
      }
    }
    serviceWebOtaLoop(ledRing_);
    if (sound_ != nullptr) {
      sound_->playOtaStart();
    }
    otaOk_ = Update.begin(UPDATE_SIZE_UNKNOWN);
    AppLog.print("OTA upload: ");
    AppLog.println(upload.filename);
    if (!otaOk_) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    serviceWebOtaLoop(ledRing_);
    if (otaOk_ && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaOk_ = false;
      Update.printError(Serial);
    }
    serviceWebOtaLoop(ledRing_);
    otaUploadedBytes_ += upload.currentSize;
    if (sound_ != nullptr && otaUploadedBytes_ - otaLastProgressCue_ >= 196608) {
      sound_->playOtaProgress();
      otaLastProgressCue_ = otaUploadedBytes_;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    serviceWebOtaLoop(ledRing_);
    if (otaOk_) {
      otaOk_ = Update.end(true);
      AppLog.println(otaOk_ ? "OTA upload complete" : "OTA upload failed");
      if (!otaOk_) {
        Update.printError(Serial);
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    otaOk_ = false;
    Update.abort();
    if (sound_ != nullptr) {
      sound_->playOtaFailed();
    }
    AppLog.println("OTA upload aborted");
  }
}

void WebPortal::handleNotFound() {
  server_.send(404, "text/plain", "Not found");
}

String WebPortal::maskedSecret(const char *value) const {
#if WEB_SHOW_WIFI_PASSWORD
  return String(value);
#else
  const size_t length = strlen(value);
  if (length == 0) {
    return "";
  }
  String masked;
  for (size_t index = 0; index < length; index++) {
    masked += '*';
  }
  return masked;
#endif
}

String WebPortal::wifiStatusText() const {
  switch (WiFi.status()) {
    case WL_CONNECTED:
      return "Connected";
    case WL_IDLE_STATUS:
      return "Idle";
    case WL_NO_SSID_AVAIL:
      return "SSID unavailable";
    case WL_CONNECT_FAILED:
      return "Connect failed";
    case WL_CONNECTION_LOST:
      return "Connection lost";
    case WL_DISCONNECTED:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

String WebPortal::batteryLabel() const {
  if (battery_ == nullptr || !battery_->available || battery_->percent < 0) {
    return "--%";
  }
  return String(battery_->percent) + "%";
}

String WebPortal::appVersionLabel() const {
  if (strcmp(Config::AppVersion, "vdev") != 0) {
    return Config::AppVersion;
  }
  if (strcmp(Config::AppVersionNumber, "dev") != 0) {
    return String("v") + Config::AppVersionNumber;
  }
  return Config::AppVersion;
}

String WebPortal::formatBytes(uint32_t bytes) const {
  if (bytes >= 1024 * 1024) {
    return String(bytes / 1024.0f / 1024.0f, 1) + " MB";
  }
  if (bytes >= 1024) {
    return String(bytes / 1024.0f, 1) + " KB";
  }
  return String(bytes) + " B";
}

String WebPortal::localizedText(const char *en, const char *nl) const {
  return languageCode_ != nullptr && *languageCode_ == "nl" ? String(nl) : String(en);
}

void WebPortal::sendSpotifyUnavailableText() {
  server_.send(503, "text/plain", localizedText("Playback not connected", "Afspelen niet verbonden"));
}

void WebPortal::sendSpotifyUnavailableJson(const char *arrayKey) {
  JsonDocument doc;
  doc["available"] = false;
  doc["error"] = localizedText("Playback not connected", "Afspelen niet verbonden");
  doc[arrayKey].to<JsonArray>();
  String body;
  serializeJson(doc, body);
  server_.send(503, "application/json", body);
}

uint32_t WebPortal::estimatedProgressMs() const {
  if (playback_ == nullptr || playback_->durationMs <= 0) {
    return 0;
  }

  return Logic::estimatedProgressMs(
      playback_->progressMs,
      playback_->durationMs,
      playback_->isPlaying,
      playback_->progressSyncedAt,
      millis());
}
