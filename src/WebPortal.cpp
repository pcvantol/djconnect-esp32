// HTTP dashboard for inspecting and updating the Spotify remote from a phone.
#include "WebPortal.h"

#include "AppLog.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>

#include "Config.h"
#include "I18n.h"
#include "LogicHelpers.h"
#include "TextHelpers.h"
#include "assets/spotifydj_favicon_ico.h"
#include "assets/spotifydj_icon_192_png.h"
#include "assets/spotifydj_site_webmanifest.h"

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
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta name="theme-color" content="#080b0c">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-title" content="SpotifyDJ">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="application-name" content="SpotifyDJ">
  <link rel="shortcut icon" href="/favicon.ico?v=2" sizes="any">
  <link rel="icon" href="/favicon.ico?v=2" sizes="any">
  <link rel="icon" type="image/png" sizes="192x192" href="/icon-192.png?v=2">
  <link rel="apple-touch-icon" sizes="180x180" href="/apple-touch-icon.png">
  <link rel="apple-touch-icon" sizes="192x192" href="/apple-touch-icon.png?v=2">
  <link rel="apple-touch-icon-precomposed" sizes="180x180" href="/apple-touch-icon-precomposed.png">
  <link rel="apple-touch-icon-precomposed" sizes="192x192" href="/apple-touch-icon-precomposed.png?v=2">
  <link rel="manifest" href="/site.webmanifest?v=2">
  <title>SpotifyDJ</title>
  <style>
    :root { color-scheme: light dark; --bg:#080b0c; --panel:#111718; --muted:#8a969a; --line:#233033; --green:#1ed760; --yellow:#caa42b; --orange:#ff9f1a; --red:#ff735d; --text:#f3f7f5; --header:rgba(8,11,12,.94); --field:#0c1112; --bar-bg:#0b1112; --art-bg:#050707; --row-line:rgba(255,255,255,.06); --log-bg:#050707; --log-text:#c7d2cf; }
    html[data-theme="light"] { --bg:#f5f7f2; --panel:#ffffff; --muted:#65716d; --line:#d9e0dc; --green:#148a3c; --yellow:#8f710f; --orange:#c86f00; --red:#b23a31; --text:#13201a; --header:rgba(245,247,242,.96); --field:#ffffff; --bar-bg:#e8eee9; --art-bg:#eef3ef; --row-line:rgba(19,32,26,.09); --log-bg:#f1f4f1; --log-text:#21302a; }
    html[data-theme="dark"] { --bg:#080b0c; --panel:#111718; --muted:#8a969a; --line:#233033; --green:#1ed760; --yellow:#caa42b; --orange:#ff9f1a; --red:#ff735d; --text:#f3f7f5; --header:rgba(8,11,12,.94); --field:#0c1112; --bar-bg:#0b1112; --art-bg:#050707; --row-line:rgba(255,255,255,.06); --log-bg:#050707; --log-text:#c7d2cf; }
    @media (prefers-color-scheme: light) { html[data-theme="auto"] { --bg:#f5f7f2; --panel:#ffffff; --muted:#65716d; --line:#d9e0dc; --green:#148a3c; --yellow:#8f710f; --orange:#c86f00; --red:#b23a31; --text:#13201a; --header:rgba(245,247,242,.96); --field:#ffffff; --bar-bg:#e8eee9; --art-bg:#eef3ef; --row-line:rgba(19,32,26,.09); --log-bg:#f1f4f1; --log-text:#21302a; } }
    * { box-sizing:border-box; }
    body { margin:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; background:var(--bg); color:var(--text); }
    header { position:sticky; top:0; z-index:2; background:var(--header); border-bottom:1px solid var(--line); padding:16px; }
    h1 { margin:0; font-size:22px; letter-spacing:0; display:flex; align-items:center; gap:8px; }
    .brand-icon { width:28px; height:28px; border-radius:6px; }
    .sub { color:var(--muted); font-size:13px; margin-top:4px; }
    .header-status { display:flex; justify-content:flex-end; align-items:center; gap:8px; }
    main { padding:12px; display:grid; gap:12px; max-width:960px; margin:0 auto; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:14px; }
    h2 { margin:0 0 10px; font-size:15px; color:var(--yellow); font-weight:700; }
    .hero-title { font-size:26px; line-height:1.08; font-weight:750; margin:2px 0 8px; overflow-wrap:anywhere; }
    .artist { color:#d8e3df; font-size:18px; margin-bottom:12px; overflow-wrap:anywhere; }
    .now { display:grid; grid-template-columns:96px 1fr; gap:12px; align-items:start; margin-bottom:12px; }
    .now.no-art { grid-template-columns:1fr; }
    .album-art { width:96px; height:96px; border-radius:8px; border:1px solid var(--line); object-fit:cover; background:var(--art-bg); display:none; }
    .grid { display:grid; gap:8px; grid-template-columns:1fr; }
    .row { display:flex; justify-content:space-between; gap:12px; border-top:1px solid var(--row-line); padding-top:8px; font-size:14px; }
    .row:first-child { border-top:0; padding-top:0; }
    .key { color:var(--muted); min-width:110px; }
    .value { text-align:right; overflow-wrap:anywhere; }
    .signal { display:inline-flex; align-items:flex-end; gap:2px; min-width:22px; height:16px; vertical-align:middle; }
    .signal i { display:block; width:4px; border-radius:2px 2px 0 0; background:#293436; }
    .signal i:nth-child(1) { height:5px; }
    .signal i:nth-child(2) { height:8px; }
    .signal i:nth-child(3) { height:11px; }
    .signal i:nth-child(4) { height:14px; }
    .signal.level-1 i:nth-child(-n+1), .signal.level-2 i:nth-child(-n+2) { background:#ff6f61; }
    .signal.level-3 i:nth-child(-n+3) { background:#f3d37b; }
    .signal.level-4 i:nth-child(-n+4) { background:var(--green); }
    .status-icons { display:inline-flex; gap:5px; vertical-align:middle; }
    .status-dot { display:inline-flex; align-items:center; justify-content:center; width:18px; height:18px; border-radius:50%; border:1px solid var(--red); color:var(--red); font-size:11px; font-weight:800; line-height:1; }
    .status-dot.ok { border-color:var(--green); color:var(--green); }
    .pill { display:inline-flex; align-items:center; min-height:24px; border-radius:999px; padding:2px 10px; background:#173721; color:#9df2b9; font-size:13px; }
    .pill.warn { background:#3b2d14; color:#f3d37b; }
    .pill.bad { background:#421b17; color:#ffb4aa; }
    .bar { height:8px; border:1px solid #39484a; border-radius:999px; overflow:hidden; background:var(--bar-bg); }
    .bar > i { display:block; height:100%; width:0; background:var(--green); }
    .controls { display:grid; gap:10px; }
    label { display:grid; gap:5px; color:var(--muted); font-size:13px; }
    select, button, input:not([type]), input[type=text], input[type=password], input[type=file] { width:100%; min-height:42px; border-radius:8px; border:1px solid var(--line); background:var(--field); color:var(--text); padding:8px 10px; font-size:15px; }
    input[type=range] { width:100%; accent-color:var(--green); }
    input.volume-slider { accent-color:var(--orange); }
    .volume-value, .volume-label { color:var(--orange); }
    button { background:#1f8c46; border-color:#31c36a; color:#f3fff7; font-weight:700; cursor:pointer; box-shadow:inset 0 -1px 0 rgba(0,0,0,.25); }
    .playback-actions button::before { display:inline-block; min-width:16px; margin-right:7px; font-weight:900; }
    #previousButton::before { content:"⏮"; }
    #nextButton::before { content:"⏭"; }
    #playButton::before { content:"▶"; }
    #pauseButton::before { content:"⏸"; }
    button.secondary { background:#243238; border-color:#3d5660; color:#f0f6f4; }
    button.warning { background:#a57912; border-color:#d6a329; color:#fff3c4; }
    button.firmware { background:#6f3bd8; border-color:#9b72ff; color:#f4edff; }
    button.ptt { width:auto; min-width:160px; min-height:40px; justify-self:start; background:#1f6fd1; border-color:#2f8cff; color:#f2f8ff; font-size:14px; }
    button.ptt.recording { background:#1559b0; border-color:#4aa3ff; color:#eef6ff; }
    button:disabled, select:disabled, input:disabled { opacity:.45; cursor:not-allowed; }
    .section-action { margin-top:10px; }
    button.danger { background:#3a1714; border-color:#632b25; color:#ffd1c9; }
    .two { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
    .playback-actions { margin-top:12px; }
    .queue { display:grid; gap:8px; }
    .queue-item { border-top:1px solid var(--row-line); padding-top:8px; }
    .queue-item:first-child { border-top:0; padding-top:0; }
    .queue-title { font-size:14px; color:var(--text); overflow-wrap:anywhere; }
    .queue-subtitle { margin-top:2px; font-size:12px; color:var(--muted); overflow-wrap:anywhere; }
    .fine { color:var(--muted); font-size:12px; line-height:1.35; }
    .fine + .fine, button + .fine, form + .fine { margin-top:10px; }
    .mono { font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; font-size:13px; }
    .status { margin-top:8px; color:#b7c5c1; font-size:13px; min-height:18px; }
    .status.alert { color:#ffdf5d; font-weight:750; }
    .status.error { color:#ff8a78; font-weight:750; }
    .wifi-grid { margin-bottom:14px; }
    .header-battery { display:inline-flex; align-items:center; justify-content:center; position:relative; width:58px; height:22px; border:1px solid currentColor; border-radius:4px; color:var(--green); font-size:11px; font-weight:800; line-height:1; vertical-align:middle; overflow:visible; }
    .header-battery::after { content:""; position:absolute; right:-5px; top:6px; width:3px; height:9px; border-radius:0 2px 2px 0; background:currentColor; }
    .header-battery .battery-fill { position:absolute; left:2px; top:2px; bottom:2px; width:0; border-radius:2px; background:currentColor; opacity:.24; transition:width .25s ease, color .25s ease; }
    .header-battery .battery-text { position:relative; z-index:1; color:var(--text); text-shadow:0 1px 2px rgba(0,0,0,.65); }
    .header-battery .battery-flash { display:none; position:absolute; right:4px; top:2px; z-index:1; color:var(--yellow); font-size:12px; text-shadow:0 1px 2px rgba(0,0,0,.65); }
    .header-battery.charging .battery-flash { display:block; animation:batteryPulse 1s ease-in-out infinite; }
    .header-battery.charging .battery-text { padding-right:12px; }
    .header-battery.low { color:var(--red); }
    .header-battery.medium { color:var(--yellow); }
    .header-battery.high { color:var(--green); }
    @keyframes batteryPulse { 0%,100% { opacity:.45; transform:scale(.92); } 50% { opacity:1; transform:scale(1.08); } }
    .pair-banner { display:none; margin:10px; padding:14px; border:1px solid rgba(255,204,51,.45); border-radius:8px; background:linear-gradient(135deg,rgba(255,204,51,.18),rgba(29,185,84,.12)); color:var(--text); }
    .pair-banner strong { display:block; font-size:18px; margin-bottom:4px; color:#ffdf5d; }
    .pair-banner a { color:#fff; font-weight:800; text-decoration:none; }
    .pair-banner .pair-code { display:inline-block; margin-left:4px; padding:2px 7px; border:1px solid rgba(255,255,255,.18); border-radius:6px; background:rgba(0,0,0,.22); font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; letter-spacing:.08em; }
    pre.logs { min-height:220px; max-height:360px; overflow:auto; margin:0; padding:10px; border:1px solid var(--line); border-radius:8px; background:var(--log-bg); color:var(--log-text); font:12px/1.35 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; white-space:pre-wrap; overflow-wrap:anywhere; }
    @media (min-width:720px) { main { grid-template-columns:1fr 1fr; } .wide { grid-column:1 / -1; } }
    @media (max-width:420px) { button.ptt { width:100%; } }
  </style>
</head>
<body>
  <header>
    <h1><img class="brand-icon" src="/icon-192.png" alt="">SpotifyDJ <span id="appVersion" class="sub">-</span></h1>
    <div class="sub header-status"><span class="status-icons"><span id="haHeaderStatus" class="status-dot" title="Home Assistant">H</span><span id="mqttHeaderStatus" class="status-dot" title="MQTT">M</span><span id="spotifyHeaderStatus" class="status-dot" title="Spotify">S</span></span><span id="wifiHeaderSignal" class="signal level-0"><i></i><i></i><i></i><i></i></span><span id="batteryHeader" class="header-battery high" title="Battery"><span id="batteryHeaderFill" class="battery-fill"></span><span id="batteryHeaderText" class="battery-text">--%</span><span class="battery-flash">⚡</span></span></div>
  </header>
  <div id="haPairBanner" class="pair-banner">
    <strong data-i18n="deviceNotPaired">Device not paired with Home Assistant</strong>
    <a data-i18n="setup" href="https://my.home-assistant.io/redirect/config_flow_start?domain=spotify_dj" target="_blank" rel="noopener noreferrer">Click here to setup</a>
    <span data-i18n="providePair">and provide pairing code:</span>
    <span id="haPairBannerCode" class="pair-code">------</span>
  </div>
  <main>
    <section class="panel wide">
      <h2 data-i18n="nowPlaying">Now Playing</h2>
      <div id="nowContent" class="now no-art">
        <img id="albumArt" class="album-art" alt="Album art">
        <div>
          <div id="playbackPill" class="pill">Loading</div>
          <div id="track" class="hero-title">-</div>
          <div id="artist" class="artist">-</div>
        </div>
      </div>
      <div class="bar"><i id="progressBar"></i></div>
      <div class="row"><span class="key" data-i18n="time">Time</span><span id="time" class="value">-</span></div>
      <div class="two playback-actions">
        <button id="previousButton" type="button">Previous song</button>
        <button id="nextButton" type="button">Next song</button>
      </div>
      <div class="two playback-actions">
        <button id="playButton" type="button">Play</button>
        <button id="pauseButton" type="button">Pause</button>
      </div>
      <button id="startLikedProxyButton" class="section-action" type="button" style="display:none">Start SpotifyDJ Liked Proxy</button>
      <div id="playbackCommandStatus" class="status"></div>
      <div class="row"><span class="key" data-i18n="output">Sound output</span><span id="device" class="value">-</span></div>
      <select id="soundOutputSelect" aria-label="Sound output"><option value="" data-i18n="loadingOutputs">Loading outputs...</option></select>
      <div id="soundOutputStatus" class="status"></div>
      <div class="row"><span class="key volume-label" data-i18n="volume">Volume</span><span id="volume" class="value volume-value">-</span></div>
      <input id="volumeSlider" class="volume-slider" type="range" min="0" max="60" value="0" aria-label="Volume">
      <div id="volumeStatus" class="status"></div>
      <button id="webPttButton" class="ptt section-action" type="button" data-i18n="webPttHold">Test DJ response</button>
      <div id="webPttTranscript" class="fine"></div>
      <div id="webPttStatus" class="status"></div>
    </section>

    <section id="queuePanel" class="panel">
      <h2 data-i18n="upNext">Up Next</h2>
      <div id="queueList" class="queue"><div class="fine" data-i18n="loadingQueue">Loading queue...</div></div>
      <div id="queueStatus" class="status"></div>
    </section>

    <section id="playlistsPanel" class="panel">
      <h2 data-i18n="playlists">Playlists</h2>
      <select id="playlistSelect" aria-label="Playlist"><option value="">Loading playlists...</option></select>
      <button id="startPlaylistButton" class="section-action" type="button">Start playlist</button>
      <div id="playlistStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2 data-i18n="settings">Settings</h2>
      <form id="settingsForm" class="controls">
        <label data-i18n-label="brightness">Screen brightness
          <select id="brightness">
            <option value="25">25%</option><option value="50">50%</option><option value="75">75%</option><option value="100">100%</option>
          </select>
        </label>
        <label data-i18n-label="dimTimeout">Screen dim timeout
          <select id="offTimeout">
            <option value="30000" data-i18n="timeout30s">30 seconds</option><option value="60000" data-i18n="timeout1m">1 minute</option><option value="120000" data-i18n="timeout2m">2 minutes</option><option value="240000" data-i18n="timeout4m">4 minutes</option>
          </select>
          <span class="fine" data-i18n="settingsFine">Screen turns off after the selected idle timeout. LED ring follows the screen power state.</span>
        </label>
        <label data-i18n-label="deepSleep">Turn off after
          <select id="sleepTimeout">
            <option value="300000" data-i18n="timeout5m">5 minutes</option><option value="900000" data-i18n="timeout15m">15 minutes</option><option value="1800000" data-i18n="timeout30m">30 minutes</option><option value="3600000" data-i18n="timeout60m">60 minutes</option>
          </select>
        </label>
        <label data-i18n-label="speakerVolume">Speaker volume
          <select id="speakerVolume">
            <option value="25">25%</option><option value="50">50%</option><option value="75">75%</option><option value="100">100%</option>
          </select>
        </label>
        <label data-i18n-label="language">Language
          <select id="language">
            <option value="en" data-i18n="languageEnglish">English</option><option value="nl" data-i18n="languageDutch">Dutch</option>
          </select>
        </label>
        <label data-i18n-label="theme">Theme
          <select id="theme">
            <option value="dark" data-i18n="themeDark">Dark</option><option value="light" data-i18n="themeLight">Light</option><option value="auto" data-i18n="themeAuto">Auto</option>
          </select>
        </label>
        <label data-i18n-label="playMode">Spotify play mode
          <select id="playMode">
            <option value="normal" data-i18n="noShuffle">No shuffle</option><option value="shuffle" data-i18n="shuffle">Shuffle</option><option value="repeat_once" data-i18n="repeatOnce">Repeat once</option><option value="repeat_infinite" data-i18n="repeatInfinite">Repeat infinite</option>
          </select>
        </label>
        <label data-i18n-label="mqttHost">MQTT host
          <input id="mqttHost" name="mqttHost" placeholder="192.168.1.10">
        </label>
        <label data-i18n-label="mqttPort">MQTT port
          <input id="mqttPort" name="mqttPort" inputmode="numeric" placeholder="1883">
        </label>
        <label data-i18n-label="mqttUsername">MQTT username
          <input id="mqttUser" name="mqttUser" autocomplete="off">
        </label>
        <label data-i18n-label="mqttPassword">MQTT password
          <input id="mqttPass" name="mqttPass" type="password" autocomplete="off">
        </label>
        <button data-i18n="saveSettings" type="submit">Save settings</button>
      </form>
      <div id="settingsStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2 data-i18n="wifi">WiFi</h2>
      <div class="grid wifi-grid">
        <div class="row"><span class="key" data-i18n="state">State</span><span id="wifiConnected" class="value">-</span></div>
        <div class="row"><span class="key">IP</span><span id="wifiIp" class="value mono">-</span></div>
        <div class="row"><span class="key">SSID</span><span id="wifiSsid" class="value">-</span></div>
        <div class="row"><span class="key">RSSI</span><span class="value"><span id="wifiSignal" class="signal level-0"><i></i><i></i><i></i><i></i></span><span id="wifiRssi">-</span></span></div>
        <div class="row"><span class="key">MAC</span><span id="wifiMac" class="value">-</span></div>
      </div>
      <form id="wifiForm" class="controls">
        <label data-i18n-label="newWifiSsid">New WiFi SSID
          <input id="wifiNewSsid" name="ssid" autocomplete="off" required>
        </label>
        <label data-i18n-label="newWifiPassword">New WiFi password
          <input id="wifiNewPassword" name="password" type="password" autocomplete="new-password" data-i18n-placeholder="wifiPasswordPlaceholder" placeholder="leave blank to keep current">
        </label>
        <button data-i18n="wifiButton" type="submit">Test WiFi &amp; restart device</button>
      </form>
      <div class="fine" data-i18n="wifiFine">The device tests the new WiFi after this page responds. If it connects, credentials are saved and the device restarts automatically.</div>
      <div id="wifiSettingsStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2 data-i18n="ha">Home Assistant</h2>
      <div class="grid">
        <div class="row"><span class="key" data-i18n="pairing">Pairing</span><span id="haPaired" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="pairCode">Pair code</span><span id="haPairCode" class="value mono">-</span></div>
        <div class="row"><span class="key">Device ID</span><span id="haDeviceId" class="value mono">-</span></div>
        <div class="row"><span class="key">mDNS URL</span><span id="haMdnsUrl" class="value mono">-</span></div>
        <div class="row"><span class="key">mDNS service</span><span id="haMdnsService" class="value mono">_spotifydj._tcp</span></div>
        <div class="row"><span class="key" data-i18n="firmware">Firmware</span><span id="haFirmware" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="model">Model</span><span id="haModel" class="value">-</span></div>
        <div class="row"><span class="key">URL</span><span id="haUrl" class="value mono">-</span></div>
      </div>
      <button id="resetPairingButton" data-i18n="resetPairing" class="warning section-action" type="button">Reset pairing</button>
      <div id="haStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2 data-i18n="spotify">Spotify</h2>
      <div class="grid">
        <div class="row"><span class="key" data-i18n="connection">Connection</span><span id="spotifyState" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="token">Token</span><span id="spotifyToken" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="error">Error</span><span id="spotifyError" class="value">-</span></div>
      </div>
      <button id="refreshButton" data-i18n="refreshSpotify" class="section-action" type="button">Refresh Spotify status</button>
      <div id="refreshStatus" class="status"></div>
      <form id="spotifyCredentialForm" class="controls section-action">
        <label data-i18n-label="spotifyClientIdRepair">Spotify client ID (optional)
          <input id="spotifyRepairClientId" name="clientId" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" data-i18n-placeholder="spotifyClientIdRepairPlaceholder" placeholder="leave blank to keep current">
        </label>
        <label data-i18n-label="spotifyRefreshRepair">New Spotify refresh token
          <input id="spotifyRepairRefreshToken" name="refreshToken" type="password" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" required>
        </label>
        <label data-i18n-label="spotifyMarketRepair">Spotify market
          <input id="spotifyRepairMarket" name="market" autocomplete="off" autocapitalize="none" autocorrect="off" spellcheck="false" placeholder="NL">
        </label>
        <button data-i18n="saveSpotifyToken" class="warning" type="submit">Save token &amp; test</button>
      </form>
      <div class="fine" data-i18n="spotifyTokenFine">The refresh token is saved and tested immediately. It is never shown after submission.</div>
      <div id="spotifyCredentialStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2>MQTT</h2>
      <div class="grid">
        <div class="row"><span class="key" data-i18n="state">State</span><span id="mqttState" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="broker">Broker</span><span id="mqttBroker" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="username">Username</span><span id="mqttUsername" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="discovery">HA discovery</span><span id="mqttDiscovery" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="lastPublished">Last published</span><span id="mqttLastPublished" class="value">-</span></div>
      </div>
    </section>

    <section class="panel">
      <h2 data-i18n="diagnostics">Diagnostics</h2>
      <div class="grid">
        <div class="row"><span class="key" data-i18n="screen">Screen</span><span id="screenState" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="ledRing">LED ring</span><span id="ledState" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="uptime">Uptime</span><span id="uptime" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="loopLoad">Loop load</span><span id="cpu" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="heap">Heap</span><span id="heap" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="storage">Storage</span><span id="storage" class="value">-</span></div>
        <div class="row"><span class="key" data-i18n="sketch">Sketch</span><span id="sketch" class="value">-</span></div>
      </div>
      <button id="rebootButton" data-i18n="restart" class="warning section-action" type="button">Restart device</button>
    </section>

    <section id="logsPanel" class="panel wide">
      <h2 data-i18n="logs">Logs</h2>
      <div class="two">
        <button id="pauseLogsButton" data-i18n="pauseLogs" class="secondary" type="button">Pause logs</button>
        <button id="copyLogsButton" data-i18n="selectAll" class="secondary" type="button">Select all</button>
      </div>
      <div id="logsStatus" class="status"></div>
      <pre id="logs" class="logs">Loading logs...</pre>
    </section>

    <section class="panel">
      <h2 data-i18n="firmwareOta">Firmware OTA</h2>
      <form id="otaForm" class="controls">
        <input id="firmware" name="firmware" type="file" accept=".bin" required>
        <button data-i18n="uploadFirmware" class="firmware" type="submit">Upload firmware</button>
      </form>
      <div class="fine" data-i18n="firmwareFine">Firmware updates run automatically when SpotifyDJ is paired with Home Assistant.</div>
      <div id="otaStatus" class="status"></div>
      <button id="hardResetButton" data-i18n="factoryReset" class="danger" type="button">Factory reset</button>
    </section>

    <section class="panel">
      <h2 data-i18n="legal">Legal</h2>
      <div class="fine" data-i18n="copyrightNotice">Copyright (c) 2026 Peter van Tol. All rights reserved. SpotifyDJ firmware is proprietary software.</div>
      <div class="fine" data-i18n="trademarkNotice">Spotify is a trademark of Spotify AB. SpotifyDJ is not affiliated with, endorsed by, or sponsored by Spotify AB.</div>
      <div class="fine" data-i18n="ossNotice">This firmware includes open-source software components. Their licenses remain with their respective authors.</div>
    </section>
  </main>

  <script>
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
      el.className = "pill" + (state === "bad" ? " bad" : state === "warn" ? " warn" : "");
    }
    let currentLanguage = "en";
    const translations = {
      en: {
        deviceNotPaired:"Device not paired with Home Assistant", setup:"Click here to setup", providePair:"and provide pairing code:",
        nowPlaying:"Now Playing", time:"Time", previous:"Previous song", next:"Next song", play:"Play", pause:"Pause", liked:"Start SpotifyDJ Liked Proxy",
        webPttHold:"Test DJ response", webPttListening:"Testing DJ response...", webPttProcessing:"Sending test command...", webPttUnsupported:"Voice test is unavailable.", webPttNoSpeech:"No test command",
        webPttFailed:"Voice command failed", webPttTestCommand:"Test the SpotifyDJ response flow", spotifyUnavailable:"Spotify not connected",
        output:"Sound output", loadingOutputs:"Loading outputs...", volume:"Volume", upNext:"Up Next", loadingQueue:"Loading queue...",
        playlists:"Playlists", loadingPlaylists:"Loading playlists...", startPlaylist:"Start playlist", settings:"Settings",
        brightness:"Screen brightness", dimTimeout:"Screen dim timeout", deepSleep:"Turn off after", speakerVolume:"Speaker volume",
        language:"Language", languageEnglish:"English", languageDutch:"Dutch", theme:"Theme", themeAuto:"Auto", themeDark:"Dark", themeLight:"Light", playMode:"Spotify play mode", noShuffle:"No shuffle",
        timeout30s:"30 seconds", timeout1m:"1 minute", timeout2m:"2 minutes", timeout4m:"4 minutes", timeout5m:"5 minutes", timeout15m:"15 minutes", timeout30m:"30 minutes", timeout60m:"60 minutes",
        shuffle:"Shuffle", repeatOnce:"Repeat once", repeatInfinite:"Repeat infinite", mqttHost:"MQTT host", mqttPort:"MQTT port",
        mqttUsername:"MQTT username", mqttPassword:"MQTT password", saveSettings:"Save settings", settingsFine:"Screen turns off after the selected idle timeout. LED ring follows the screen power state.",
        wifi:"WiFi", state:"State", newWifiSsid:"New WiFi SSID", newWifiPassword:"New WiFi password", wifiButton:"Test WiFi & restart device",
        wifiFine:"The device tests the new WiFi after this page responds. If it connects, credentials are saved and the device restarts automatically.",
        wifiPasswordPlaceholder:"leave blank to keep current",
        ha:"Home Assistant", pairing:"Pairing", pairCode:"Pair code", firmware:"Firmware", model:"Model", resetPairing:"Reset pairing",
        spotify:"Spotify", connection:"Connection", token:"Token", error:"Error", refreshSpotify:"Refresh Spotify status", spotifyClientIdRepair:"Spotify client ID (optional)",
        spotifyClientIdRepairPlaceholder:"leave blank to keep current", spotifyRefreshRepair:"New Spotify refresh token", spotifyMarketRepair:"Spotify market",
        saveSpotifyToken:"Save token & test", spotifyTokenFine:"The refresh token is saved and tested immediately. Fill the client ID too if this device does not have one stored yet. The token is never shown after submission.", broker:"Broker",
        username:"Username", discovery:"HA discovery", lastPublished:"Last published", diagnostics:"Diagnostics", screen:"Screen",
        ledRing:"LED ring", uptime:"Uptime", loopLoad:"Loop load", heap:"Heap", storage:"Storage", sketch:"Sketch", restart:"Restart device",
        logs:"Logs", pauseLogs:"Pause logs", selectAll:"Select all", firmwareOta:"Firmware OTA", uploadFirmware:"Upload firmware",
        firmwareFine:"Firmware updates run automatically when SpotifyDJ is paired with Home Assistant.", factoryReset:"Factory reset",
        loading:"Loading", playing:"Playing", paused:"Paused", noPlayback:"No playback", connected:"Connected", disconnected:"Disconnected",
        authorized:"Authorized", notAuthorized:"Not authorized", tokenSecondsLeft:"s left", disabled:"Disabled", charging:"charging", full:"full",
        discharging:"discharging", paired:"Paired", pairingMode:"Pairing mode", pairingUnavailable:"Pairing info unavailable",
        none:"None", noOutputs:"No outputs", outputsFailed:"Outputs failed", noQueuedSongs:"No queued songs", noPlaylists:"No playlists",
        playlistsFailed:"Playlists failed", noLogs:"No logs yet", switchingOutput:"Switching output...", skipping:"Skipping...",
        goingBack:"Going back...", startingLiked:"Starting Liked Proxy...", selectPlaylist:"Select a playlist",
        startingPlaylist:"Starting playlist...", resumeLogs:"Resume logs", logsPaused:"Logs paused", logsLive:"Logs live",
        logsPausedSelected:"Logs paused and selected", saving:"Saving...", testWifiConfirm:"Test these WiFi credentials? The web page may disconnect during the test.",
        startingWifiTest:"Starting WiFi test...", refreshing:"Refreshing...", restartConfirm:"Restart SpotifyDJ?",
        resetPairingConfirm:"Reset Home Assistant pairing and restart to the pairing screen?", factoryResetConfirm:"Factory reset SpotifyDJ and open setup mode?",
        noIp:"No IP", wifiSignal:"WiFi signal", wifiDisconnected:"WiFi disconnected",
        publishedAfterConnect:"Published after connect", waitingForBroker:"Waiting for broker", ago:"ago",
        legal:"Legal", copyrightNotice:"Copyright (c) 2026 Peter van Tol. All rights reserved. SpotifyDJ firmware is proprietary software.",
        trademarkNotice:"Spotify is a trademark of Spotify AB. SpotifyDJ is not affiliated with, endorsed by, or sponsored by Spotify AB.",
        ossNotice:"This firmware includes open-source software components. Their licenses remain with their respective authors."
      },
      nl: {
        deviceNotPaired:"Device niet gekoppeld met Home Assistant", setup:"Klik hier om te koppelen", providePair:"en vul koppelcode in:",
        nowPlaying:"Speelt nu", time:"Tijd", previous:"Vorig nummer", next:"Volgend nummer", play:"Afspelen", pause:"Pauzeren", liked:"Start SpotifyDJ Liked Proxy",
        webPttHold:"Test DJ-response", webPttListening:"DJ-response testen...", webPttProcessing:"Testcommando versturen...", webPttUnsupported:"Voice test is niet beschikbaar.", webPttNoSpeech:"Geen testcommando",
        webPttFailed:"Voice command mislukt", webPttTestCommand:"Test de SpotifyDJ response flow", spotifyUnavailable:"Spotify niet verbonden",
        output:"Geluidsuitgang", loadingOutputs:"Outputs laden...", volume:"Volume", upNext:"Volgende nummer", loadingQueue:"Wachtrij laden...",
        playlists:"Afspeellijsten", loadingPlaylists:"Afspeellijsten laden...", startPlaylist:"Start afspeellijst", settings:"Instellingen",
        brightness:"Schermhelderheid", dimTimeout:"Scherm uit na", deepSleep:"Uitzetten na", speakerVolume:"Speakervolume",
        language:"Taal", languageEnglish:"Engels", languageDutch:"Nederlands", theme:"Thema", themeAuto:"Auto", themeDark:"Donker", themeLight:"Licht", playMode:"Spotify speelmodus", noShuffle:"Geen shuffle",
        timeout30s:"30 seconden", timeout1m:"1 minuut", timeout2m:"2 minuten", timeout4m:"4 minuten", timeout5m:"5 minuten", timeout15m:"15 minuten", timeout30m:"30 minuten", timeout60m:"60 minuten",
        shuffle:"Shuffle", repeatOnce:"Eenmaal herhalen", repeatInfinite:"Oneindig herhalen", mqttHost:"MQTT host", mqttPort:"MQTT poort",
        mqttUsername:"MQTT gebruikersnaam", mqttPassword:"MQTT wachtwoord", saveSettings:"Instellingen opslaan", settingsFine:"Scherm gaat uit na de ingestelde inactiviteit. LED-ring volgt de schermstatus.",
        wifi:"WiFi", state:"Status", newWifiSsid:"Nieuwe WiFi SSID", newWifiPassword:"Nieuw WiFi wachtwoord", wifiButton:"Test WiFi & herstart device",
        wifiFine:"Het device test de nieuwe WiFi nadat deze pagina antwoord krijgt. Bij succes worden credentials opgeslagen en herstart het device.",
        wifiPasswordPlaceholder:"leeg laten om huidige te behouden",
        ha:"Home Assistant", pairing:"Koppeling", pairCode:"Koppelcode", firmware:"Firmware", model:"Model", resetPairing:"Home Assistant koppeling resetten",
        spotify:"Spotify", connection:"Verbinding", token:"Token", error:"Fout", refreshSpotify:"Spotify status verversen", spotifyClientIdRepair:"Spotify client ID (optioneel)",
        spotifyClientIdRepairPlaceholder:"leeg laten om huidige te behouden", spotifyRefreshRepair:"Nieuwe Spotify refresh token", spotifyMarketRepair:"Spotify market",
        saveSpotifyToken:"Token opslaan & testen", spotifyTokenFine:"De refresh token wordt opgeslagen en direct getest. Vul ook de client ID in als dit device er nog geen heeft opgeslagen. De token wordt na verzenden nooit getoond.", broker:"Broker",
        username:"Gebruikersnaam", discovery:"HA discovery", lastPublished:"Laatst gepubliceerd", diagnostics:"Diagnostiek", screen:"Scherm",
        ledRing:"LED-ring", uptime:"Uptime", loopLoad:"Loop load", heap:"Heap", storage:"Opslag", sketch:"Sketch", restart:"Device herstarten",
        logs:"Logs", pauseLogs:"Pauzeer logs", selectAll:"Selecteer alles", firmwareOta:"Firmware OTA", uploadFirmware:"Upload firmware",
        firmwareFine:"Firmware update wordt automatisch uitgevoerd indien SpotifyDJ is gekoppeld aan Home Assistant.", factoryReset:"Fabrieksreset",
        loading:"Laden", playing:"Speelt", paused:"Gepauzeerd", noPlayback:"Geen playback", connected:"Verbonden", disconnected:"Niet verbonden",
        authorized:"Geautoriseerd", notAuthorized:"Niet geautoriseerd", tokenSecondsLeft:"s over", disabled:"Uitgeschakeld", charging:"laden", full:"vol",
        discharging:"ontladen", paired:"Gekoppeld", pairingMode:"Koppelmodus", pairingUnavailable:"Koppelinformatie niet beschikbaar",
        none:"Geen", noOutputs:"Geen outputs", outputsFailed:"Outputs mislukt", noQueuedSongs:"Geen nummers in wachtrij", noPlaylists:"Geen afspeellijsten",
        playlistsFailed:"Afspeellijsten mislukt", noLogs:"Nog geen logs", switchingOutput:"Output wisselen...", skipping:"Overslaan...",
        goingBack:"Teruggaan...", startingLiked:"Liked Proxy starten...", selectPlaylist:"Selecteer een afspeellijst",
        startingPlaylist:"Afspeellijst starten...", resumeLogs:"Logs hervatten", logsPaused:"Logs gepauzeerd", logsLive:"Logs live",
        logsPausedSelected:"Logs gepauzeerd en geselecteerd", saving:"Opslaan...", testWifiConfirm:"Deze WiFi-gegevens testen? De webpagina kan tijdens de test loskoppelen.",
        startingWifiTest:"WiFi-test starten...", refreshing:"Verversen...", restartConfirm:"SpotifyDJ herstarten?",
        resetPairingConfirm:"Home Assistant koppeling resetten en herstarten naar het koppelscherm?", factoryResetConfirm:"Device resetten naar fabrieksinstellingen?",
        noIp:"Geen IP", wifiSignal:"WiFi signaal", wifiDisconnected:"WiFi niet verbonden",
        publishedAfterConnect:"Gepubliceerd na verbinden", waitingForBroker:"Wachten op broker", ago:"geleden",
        legal:"Juridisch", copyrightNotice:"Copyright (c) 2026 Peter van Tol. Alle rechten voorbehouden. SpotifyDJ firmware is proprietary software.",
        trademarkNotice:"Spotify is een handelsmerk van Spotify AB. SpotifyDJ is niet verbonden aan, goedgekeurd door of gesponsord door Spotify AB.",
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
      $("previousButton").textContent = tr("previous");
      $("nextButton").textContent = tr("next");
      $("playButton").textContent = tr("play");
      $("pauseButton").textContent = tr("pause");
      $("startLikedProxyButton").textContent = tr("liked");
      if (!webPttRunning) $("webPttButton").textContent = tr("webPttHold");
      $("pauseLogsButton").textContent = logsPaused ? tr("resumeLogs") : tr("pauseLogs");
      const outputOption = $("soundOutputSelect").querySelector("option[data-i18n='loadingOutputs']");
      if (spotifyControlsEnabled && outputOption) outputOption.textContent = tr("loadingOutputs");
      const playlistOption = $("playlistSelect").querySelector("option");
      if (spotifyControlsEnabled && playlistOption && playlistOption.value === "") playlistOption.textContent = tr("loadingPlaylists");
    }
    const dirtyInputs = new Set();
    function setInput(id, value) {
      const el = $(id);
      if (document.activeElement !== el && !dirtyInputs.has(id)) el.value = value ?? "";
    }
    for (const id of ["mqttHost", "mqttPort", "mqttUser", "mqttPass"]) {
      $(id).addEventListener("input", () => dirtyInputs.add(id));
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
    function setStatusDot(id, ok) {
      const el = $(id);
      el.className = "status-dot" + (ok ? " ok" : "");
    }
    function setBatteryHeader(battery) {
      const percent = Math.max(0, Math.min(100, Number(battery.percent ?? 0)));
      const charging = !!battery.charging;
      const full = !!battery.full;
      const el = $("batteryHeader");
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
    let webPttRunning = false;
    let homeAssistantRuntimePaired = false;
    let albumArtUrl = "";
    let logsVisible = false;
    let queueVisible = false;
    let playlistsVisible = false;
    let soundOutputsVisible = false;
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
    async function startWebPtt() {
      if ($("webPttButton").disabled) return;
      if (webPttRunning) return;
      webPttRunning = true;
      $("webPttTranscript").textContent = tr("webPttTestCommand");
      $("webPttStatus").textContent = tr("webPttProcessing");
      $("webPttButton").textContent = tr("webPttListening");
      $("webPttButton").classList.add("recording");
      try {
        const body = new URLSearchParams({ text: tr("webPttTestCommand") });
        const response = await fetch("/api/voice-text", { method:"POST", body });
        const payload = await response.json();
        $("webPttStatus").textContent = payload.message || (payload.success ? "OK" : tr("webPttFailed"));
        refresh();
      } catch (error) {
        $("webPttStatus").textContent = tr("webPttFailed");
      } finally {
        webPttRunning = false;
        $("webPttButton").classList.remove("recording");
        $("webPttButton").textContent = tr("webPttHold");
      }
    }
    function setSpotifyControlsEnabled(enabled) {
      spotifyControlsEnabled = !!enabled;
      for (const id of ["previousButton", "nextButton", "playButton", "pauseButton", "volumeSlider", "soundOutputSelect", "startLikedProxyButton", "playlistSelect", "startPlaylistButton"]) {
        $(id).disabled = !spotifyControlsEnabled;
      }
      if (!spotifyControlsEnabled) {
        $("soundOutputStatus").textContent = "";
        $("playbackCommandStatus").textContent = "";
        $("volumeStatus").textContent = "";
        $("soundOutputSelect").innerHTML = `<option value="none">${tr("none")}</option><option value="iPhone">iPhone</option>`;
        $("playlistSelect").innerHTML = `<option value="">${tr("spotifyUnavailable")}</option>`;
        $("queueList").innerHTML = '<div class="fine"></div>';
      }
    }
    function render(data) {
      currentLanguage = data.settings.language || "en";
      document.documentElement.dataset.theme = data.settings.theme || "dark";
      applyTranslations();
      text("appVersion", data.app.version);
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
        nowContent.classList.add("no-art");
      }
      const state = data.playback.isPlaying ? tr("playing") : data.playback.hasPlayback ? tr("paused") : tr("noPlayback");
      text("playbackPill", state);
      pill($("playbackPill"), data.playback.isPlaying ? "ok" : data.playback.hasPlayback ? "warn" : "bad");
      $("startLikedProxyButton").style.display = data.spotify.authorized && !data.playback.hasPlayback ? "block" : "none";
      $("playButton").disabled = !data.spotify.authorized || !data.playback.hasPlayback || data.playback.isPlaying;
      $("pauseButton").disabled = !data.spotify.authorized || !data.playback.hasPlayback || !data.playback.isPlaying;
      text("time", `${duration(data.playback.progressMs)} / ${duration(data.playback.durationMs)}`);
      $("progressBar").style.width = `${data.playback.progressPercent || 0}%`;
      text("device", data.device.name || "-");
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
      text("spotifyState", data.spotify.authorized ? tr("authorized") : tr("notAuthorized"));
      setStatusDot("spotifyHeaderStatus", !!data.spotify.authorized);
      setSpotifyControlsEnabled(!!data.spotify.authorized);
      text("spotifyToken", data.spotify.authorized ? `${data.spotify.tokenExpiresInSec} ${tr("tokenSecondsLeft")}` : "-");
      text("spotifyError", data.spotify.error || "-");
      homeAssistantRuntimePaired = !!(data.ha && data.ha.paired);
      setStatusDot("haHeaderStatus", homeAssistantRuntimePaired);
      text("haPaired", homeAssistantRuntimePaired ? tr("paired") : tr("pairingMode"));
      $("webPttButton").disabled = !(data.voice && data.voice.available);
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
      setInput("playMode", data.playback.playMode || "normal");
      setInput("mqttHost", data.mqtt.host || "");
      setInput("mqttPort", String(data.mqtt.port || 1883));
      setInput("mqttUser", data.mqtt.username || "");
      text("mqttState", data.mqtt.state || tr("disabled"));
      setStatusDot("mqttHeaderStatus", !!data.mqtt.connected);
      text("mqttBroker", data.mqtt.host ? `${data.mqtt.host}:${data.mqtt.port}` : "-");
      text("mqttUsername", data.mqtt.username || "-");
      text("mqttDiscovery", data.mqtt.connected ? tr("publishedAfterConnect") : data.mqtt.enabled ? (data.mqtt.state || tr("waitingForBroker")) : tr("disabled"));
      text("mqttLastPublished", data.mqtt.lastPublishedMs ? `${duration(data.mqtt.lastPublishedMs)} uptime, ${duration(Math.max(0, data.app.uptimeMs - data.mqtt.lastPublishedMs))} ${tr("ago")}` : "-");
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
        text("haUrl", info.ha_url || "-");
        text("haStatus", "");
        if (homeAssistantRuntimePaired) {
          $("haPairBanner").style.display = "none";
          text("haPairCode", "-");
          return;
        }
        const pairResponse = await fetch("/api/device/pairing-info", { cache: "no-store" });
        const pair = await pairResponse.json();
        text("haPairCode", pair.pair_code || "-");
        text("haPairBannerCode", pair.pair_code || "------");
        $("haPairBanner").style.display = "block";
        text("haMdnsUrl", pair.local_url || (pair.device_id ? `http://${pair.device_id}.local` : "-"));
      } catch (error) {
        $("haPairBanner").style.display = "none";
        setStatusDot("haHeaderStatus", false);
        text("haStatus", tr("pairingUnavailable"));
      }
    }
    async function loadSoundOutputs() {
      if (!soundOutputsVisible) return;
      const select = $("soundOutputSelect");
      soundOutputLoadedAt = Date.now();
      try {
        const response = await fetch("/api/devices", { cache: "no-store" });
        const data = await response.json();
        select.innerHTML = "";
        const none = document.createElement("option");
        none.value = "none";
        none.textContent = tr("none") || "None";
        select.appendChild(none);
        const iphone = document.createElement("option");
        iphone.value = "iPhone";
        iphone.textContent = "iPhone";
        select.appendChild(iphone);
        for (const device of (data.devices || [])) {
          if ((device.name || "").toLowerCase().includes("iphone")) {
            iphone.value = device.id || "iPhone";
            iphone.selected = !!device.active;
            iphone.textContent = device.active ? "iPhone *" : "iPhone";
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
        const response = await fetch("/api/queue", { cache: "no-store" });
        const data = await response.json();
        list.innerHTML = "";
        if (!data.items || data.items.length === 0) {
          list.innerHTML = `<div class="fine">${data.error || tr("noQueuedSongs")}</div>`;
          return;
        }
        for (const item of data.items) {
          const row = document.createElement("div");
          row.className = "queue-item";
          const title = document.createElement("div");
          title.className = "queue-title";
          title.textContent = item.title || "-";
          const subtitle = document.createElement("div");
          subtitle.className = "queue-subtitle";
          subtitle.textContent = item.subtitle || "-";
          row.appendChild(title);
          row.appendChild(subtitle);
          list.appendChild(row);
        }
        $("queueStatus").textContent = "";
      } catch (error) {
        list.innerHTML = '<div class="fine"></div>';
      }
    }
    async function loadPlaylists() {
      if (!playlistsVisible) return;
      const select = $("playlistSelect");
      playlistsLoadedAt = Date.now();
      try {
        const response = await fetch("/api/playlists", { cache: "no-store" });
        const data = await response.json();
        select.innerHTML = "";
        if (!data.items || data.items.length === 0) {
          select.innerHTML = `<option value="">${data.error || tr("noPlaylists")}</option>`;
          return;
        }
        for (const playlist of data.items) {
          const option = document.createElement("option");
          option.value = playlist.uri;
          option.textContent = playlist.owner ? `${playlist.name} - ${playlist.owner}` : playlist.name;
          select.appendChild(option);
        }
        $("playlistStatus").textContent = "";
      } catch (error) {
        select.innerHTML = `<option value="">${tr("playlistsFailed")}</option>`;
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
    $("previousButton").addEventListener("click", () => sendPlaybackCommand("previous"));
    $("nextButton").addEventListener("click", () => sendPlaybackCommand("next"));
    $("playButton").addEventListener("click", () => sendPlaybackCommand("play"));
    $("pauseButton").addEventListener("click", () => sendPlaybackCommand("pause"));
    $("startLikedProxyButton").addEventListener("click", () => sendPlaybackCommand("likedProxy"));
    $("webPttButton").addEventListener("click", event => {
      event.preventDefault();
      startWebPtt();
    });
    $("startPlaylistButton").addEventListener("click", async () => {
      if (!spotifyControlsEnabled) return;
      const playlistUri = $("playlistSelect").value;
      if (!playlistUri) {
        $("playlistStatus").textContent = tr("selectPlaylist");
        return;
      }
      $("playlistStatus").textContent = tr("startingPlaylist");
      const body = new URLSearchParams({ action:"playlist", uri:playlistUri });
      const response = await fetch("/api/playback", { method:"POST", body });
      $("playlistStatus").textContent = await response.text();
      await refresh();
      await loadQueue();
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
        playMode: $("playMode").value,
        mqttHost: $("mqttHost").value,
        mqttPort: $("mqttPort").value,
        mqttUser: $("mqttUser").value,
        mqttPass: $("mqttPass").value
      });
      const response = await fetch("/api/settings", { method:"POST", body });
      $("settingsStatus").textContent = await response.text();
      for (const id of ["mqttHost", "mqttPort", "mqttUser", "mqttPass"]) dirtyInputs.delete(id);
      refresh();
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
    });
    $("refreshButton").addEventListener("click", async () => {
      $("refreshStatus").textContent = tr("refreshing");
      const response = await fetch("/api/refresh", { method:"POST" });
      $("refreshStatus").textContent = await response.text();
      refresh();
    });
    $("spotifyCredentialForm").addEventListener("submit", async event => {
      event.preventDefault();
      $("spotifyCredentialStatus").textContent = tr("saving");
      const body = JSON.stringify({
        clientId: $("spotifyRepairClientId").value.trim(),
        refreshToken: $("spotifyRepairRefreshToken").value.trim(),
        market: $("spotifyRepairMarket").value.trim()
      });
      try {
        const response = await fetch("/api/spotify-credentials", {
          method:"POST",
          headers: { "Content-Type":"application/json" },
          body
        });
        $("spotifyCredentialStatus").textContent = await response.text();
      } finally {
        $("spotifyRepairClientId").value = "";
        $("spotifyRepairRefreshToken").value = "";
      }
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
    refresh();
    loadPairingInfo();
    setInterval(refresh, 3000);
    setInterval(refreshLogs, 1000);
  </script>
</body>
</html>
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
    MqttPublisher &mqttPublisher,
    const MqttSettings &mqttSettings,
    const uint8_t &screenBrightnessPercent,
    const uint8_t &speakerVolumePercent,
    const bool &homeAssistantPaired,
    const String &languageCode,
    const String &themeCode,
    const uint32_t &screenOffTimeoutMs,
    const uint32_t &deviceSleepTimeoutMs,
    void *callbackContext,
    SettingsCallback settingsCallback,
    MqttSettingsCallback mqttSettingsCallback,
    WifiSettingsCallback wifiSettingsCallback,
    VoiceTextCallback voiceTextCallback,
    SpotifyCredentialsCallback spotifyCredentialsCallback,
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
  mqttPublisher_ = &mqttPublisher;
  mqttSettings_ = &mqttSettings;
  screenBrightnessPercent_ = &screenBrightnessPercent;
  speakerVolumePercent_ = &speakerVolumePercent;
  homeAssistantPaired_ = &homeAssistantPaired;
  languageCode_ = &languageCode;
  themeCode_ = &themeCode;
  screenOffTimeoutMs_ = &screenOffTimeoutMs;
  deviceSleepTimeoutMs_ = &deviceSleepTimeoutMs;
  callbackContext_ = callbackContext;
  settingsCallback_ = settingsCallback;
  mqttSettingsCallback_ = mqttSettingsCallback;
  wifiSettingsCallback_ = wifiSettingsCallback;
  voiceTextCallback_ = voiceTextCallback;
  spotifyCredentialsCallback_ = spotifyCredentialsCallback;
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
    sendProgmemAsset(server_, "image/x-icon", SPOTIFYDJ_FAVICON_ICO, SPOTIFYDJ_FAVICON_ICO_LEN);
  });
  server_.on("/icon-192.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/apple-touch-icon.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/apple-touch-icon-precomposed.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/icons/favicon.ico", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/x-icon", SPOTIFYDJ_FAVICON_ICO, SPOTIFYDJ_FAVICON_ICO_LEN);
  });
  server_.on("/icons/icon-192.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/icons/icon-512.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/icons/maskable-icon-192.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/icons/maskable-icon-512.png", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "image/png", SPOTIFYDJ_ICON_192_PNG, SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/site.webmanifest", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "application/manifest+json", SPOTIFYDJ_SITE_WEBMANIFEST, SPOTIFYDJ_SITE_WEBMANIFEST_LEN, 86400UL);
  });
  server_.on("/icons/site.webmanifest", HTTP_GET, [this]() {
    sendProgmemAsset(server_, "application/manifest+json", SPOTIFYDJ_SITE_WEBMANIFEST, SPOTIFYDJ_SITE_WEBMANIFEST_LEN, 86400UL);
  });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatusJson(); });
  server_.on("/api/logs", HTTP_GET, [this]() { handleLogsText(); });
  server_.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_.on("/api/wifi", HTTP_POST, [this]() { handleWifiPost(); });
  server_.on("/api/volume", HTTP_POST, [this]() { handleVolumePost(); });
  server_.on("/api/devices", HTTP_GET, [this]() { handleDevicesJson(); });
  server_.on("/api/playlists", HTTP_GET, [this]() { handlePlaylistsJson(); });
  server_.on("/api/queue", HTTP_GET, [this]() { handleQueueJson(); });
  server_.on("/api/transfer", HTTP_POST, [this]() { handleTransferPost(); });
  server_.on("/api/playback", HTTP_POST, [this]() { handlePlaybackCommandPost(); });
  server_.on("/api/voice-text", HTTP_POST, [this]() { handleVoiceTextPost(); });
  server_.on("/api/spotify-credentials", HTTP_POST, [this]() { handleSpotifyCredentialsPost(); });
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
  app["name"] = "SpotifyDJ";
  app["version"] = Config::AppVersion;
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
  playback["playMode"] = Logic::playModeFromSpotifyState(playback_->shuffle, playback_->repeatState.c_str());
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
  spotify["tokenExpiresInSec"] = spotify_->accessTokenExpiresInSeconds();
  spotify["error"] = playback_->error;

  JsonObject ha = doc["ha"].to<JsonObject>();
  ha["paired"] = homeAssistantPaired_ != nullptr && *homeAssistantPaired_;

  JsonObject voice = doc["voice"].to<JsonObject>();
  voice["available"] = voiceTextCallback_ != nullptr &&
                       wifiConnected &&
                       homeAssistantPaired_ != nullptr &&
                       *homeAssistantPaired_;

  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["enabled"] = mqttSettings_ != nullptr && mqttSettings_->enabled;
  mqtt["state"] = mqttPublisher_ == nullptr ? "Unknown" : mqttPublisher_->connectionState();
  mqtt["connected"] = mqttPublisher_ != nullptr && mqttPublisher_->connected();
  mqtt["host"] = mqttSettings_ == nullptr ? "" : mqttSettings_->host;
  mqtt["port"] = mqttSettings_ == nullptr ? 1883 : mqttSettings_->port;
  mqtt["username"] = mqttSettings_ == nullptr ? "" : mqttSettings_->username;
  mqtt["password"] = mqttSettings_ == nullptr ? "" : maskedSecret(mqttSettings_->password.c_str());
  mqtt["lastPublishedMs"] = mqttPublisher_ == nullptr ? 0 : mqttPublisher_->lastPublishAtMs();

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
  AppLog.print("Web settings: brightness=");
  AppLog.print(brightness);
  AppLog.print("% dim=");
  AppLog.print(offTimeoutMs / 1000UL);
  AppLog.print("s sleep=");
  AppLog.print(sleepTimeoutMs / 60000UL);
  AppLog.print("m speaker=");
  AppLog.print(speakerVolume);
  AppLog.println("%");
  if (settingsCallback_ != nullptr) {
    settingsCallback_(callbackContext_, brightness, offTimeoutMs, sleepTimeoutMs, speakerVolume, language, theme);
  }

  String responseText = "Settings saved";
  if (server_.hasArg("playMode") && spotify_ != nullptr && spotify_->isAuthorized()) {
    const String playMode = server_.arg("playMode");
    AppLog.print("Web settings: Spotify play mode ");
    AppLog.println(playMode);
    if (!spotify_->setPlayMode(playMode)) {
      AppLog.print("Web settings: Spotify play mode failed: ");
      AppLog.println(playback_ == nullptr || playback_->error.isEmpty() ? "unknown" : playback_->error);
      responseText = playback_ == nullptr || playback_->error.isEmpty() ? "Settings saved, play mode failed" : playback_->error;
    }
  }

  if (mqttSettingsCallback_ != nullptr) {
    MqttSettings mqttSettings;
    mqttSettings.host = server_.arg("mqttHost");
    mqttSettings.host.trim();
    mqttSettings.port = server_.arg("mqttPort").toInt() > 0 ? server_.arg("mqttPort").toInt() : 1883;
    mqttSettings.username = server_.arg("mqttUser");
    mqttSettings.password = server_.arg("mqttPass");
    mqttSettings.enabled = !mqttSettings.host.isEmpty();
    AppLog.print("Web settings: MQTT ");
    AppLog.println(mqttSettings.enabled ? "enabled/updated" : "disabled");
    mqttSettingsCallback_(callbackContext_, mqttSettings);
  }
  server_.send(200, "text/plain", responseText);
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
    server_.send(503, "application/json", "{\"error\":\"spotify not ready\"}");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableJson("devices");
    return;
  }

  DeviceListState devices;
  spotify_->refreshDevices(devices);

  JsonDocument doc;
  doc["available"] = devices.available;
  doc["error"] = devices.error;
  JsonArray items = doc["devices"].to<JsonArray>();
  for (size_t index = 0; index < devices.count; index++) {
    JsonObject item = items.add<JsonObject>();
    item["id"] = devices.devices[index].id;
    item["name"] = devices.devices[index].name;
    item["active"] = devices.devices[index].active;
    item["supportsVolume"] = devices.devices[index].supportsVolume;
  }

  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebPortal::handleQueueJson() {
  if (spotify_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"spotify not ready\"}");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableJson("items");
    return;
  }

  QueueState queue;
  spotify_->refreshQueue(queue);

  JsonDocument doc;
  doc["available"] = queue.available;
  doc["error"] = queue.error;
  JsonArray items = doc["items"].to<JsonArray>();
  for (size_t index = 0; index < queue.count; index++) {
    JsonObject item = items.add<JsonObject>();
    item["title"] = queue.items[index].title;
    item["subtitle"] = queue.items[index].subtitle;
  }

  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebPortal::handlePlaylistsJson() {
  if (spotify_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"spotify not ready\"}");
    return;
  }
  if (!spotify_->isAuthorized()) {
    sendSpotifyUnavailableJson("items");
    return;
  }

  PlaylistListState playlists;
  spotify_->refreshPlaylists(playlists);

  JsonDocument doc;
  doc["available"] = playlists.available;
  doc["error"] = playlists.error;
  JsonArray items = doc["items"].to<JsonArray>();
  for (size_t index = 0; index < playlists.count; index++) {
    JsonObject item = items.add<JsonObject>();
    item["name"] = playlists.items[index].name;
    item["owner"] = playlists.items[index].owner;
    item["uri"] = playlists.items[index].uri;
  }

  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
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
  String message = localizedText("Liked Proxy started", "Liked Proxy gestart");
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
  const bool ok = voiceTextCallback_(callbackContext_, voiceText, message, audioUrl);

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

void WebPortal::handleSpotifyCredentialsPost() {
  if (spotifyCredentialsCallback_ == nullptr) {
    server_.send(400, "text/plain", localizedText("Missing refresh token", "Refresh token ontbreekt"));
    return;
  }

  const String rawBody = server_.arg("plain");
  String clientId = postedValue(server_, rawBody, "clientId", "client_id");
  String refreshToken = postedValue(server_, rawBody, "refreshToken", "refresh_token");
  String market = postedValue(server_, rawBody, "market", "spotify_market");
  clientId.trim();
  refreshToken.trim();
  market.trim();
  AppLog.print("Web Spotify repair: refresh_token=");
  AppLog.print(refreshToken.isEmpty() ? "missing" : "present");
  AppLog.print(" raw_len=");
  AppLog.println(rawBody.length());
  if (refreshToken.isEmpty()) {
    server_.send(400, "text/plain", localizedText("Missing refresh token", "Refresh token ontbreekt"));
    return;
  }

  AppLog.print("Web Spotify repair: submitted client_id=");
  AppLog.println(clientId.isEmpty() ? "keep-current" : "present");
  String message;
  const bool ok = spotifyCredentialsCallback_(callbackContext_, clientId, refreshToken, market, message);
  server_.send(ok ? 200 : 502, "text/plain", message);
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
    if (ledRing_ != nullptr) {
      ledRing_->showFirmwareUpdateAnimation();
    }
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
    if (ledRing_ != nullptr) {
      ledRing_->showFirmwareUpdateAnimation();
    }
    if (otaOk_ && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaOk_ = false;
      Update.printError(Serial);
    }
    otaUploadedBytes_ += upload.currentSize;
    if (sound_ != nullptr && otaUploadedBytes_ - otaLastProgressCue_ >= 196608) {
      sound_->playOtaProgress();
      otaLastProgressCue_ = otaUploadedBytes_;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (ledRing_ != nullptr) {
      ledRing_->showFirmwareUpdateAnimation();
    }
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
  server_.send(503, "text/plain", localizedText("Spotify not connected", "Spotify niet verbonden"));
}

void WebPortal::sendSpotifyUnavailableJson(const char *arrayKey) {
  JsonDocument doc;
  doc["available"] = false;
  doc["error"] = localizedText("Spotify not connected", "Spotify niet verbonden");
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
