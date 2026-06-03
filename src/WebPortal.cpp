// HTTP dashboard for inspecting and updating the Spotify remote from a phone.
#include "WebPortal.h"

#include "AppLog.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>

#include "Config.h"
#include "LogicHelpers.h"
#include "TextHelpers.h"
#include "assets/spotifydj_favicon_ico.h"
#include "assets/spotifydj_icon_192_png.h"
#include "assets/spotifydj_site_webmanifest.h"

#ifndef WEB_SHOW_WIFI_PASSWORD
#define WEB_SHOW_WIFI_PASSWORD 0
#endif

static const char IndexHtml[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="/favicon.ico">
  <link rel="apple-touch-icon" href="/icon-192.png">
  <link rel="manifest" href="/site.webmanifest">
  <title>SpotifyDJ</title>
  <style>
    :root { color-scheme: dark; --bg:#080b0c; --panel:#111718; --muted:#8a969a; --line:#233033; --green:#1ed760; --yellow:#caa42b; --red:#ff735d; --text:#f3f7f5; }
    * { box-sizing:border-box; }
    body { margin:0; font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif; background:var(--bg); color:var(--text); }
    header { position:sticky; top:0; z-index:2; background:rgba(8,11,12,.94); border-bottom:1px solid var(--line); padding:16px; }
    h1 { margin:0; font-size:22px; letter-spacing:0; display:flex; align-items:center; gap:8px; }
    .brand-icon { width:28px; height:28px; border-radius:6px; }
    .sub { color:var(--muted); font-size:13px; margin-top:4px; }
    main { padding:12px; display:grid; gap:12px; max-width:960px; margin:0 auto; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:14px; }
    h2 { margin:0 0 10px; font-size:15px; color:var(--yellow); font-weight:700; }
    .hero-title { font-size:26px; line-height:1.08; font-weight:750; margin:2px 0 8px; overflow-wrap:anywhere; }
    .artist { color:#d8e3df; font-size:18px; margin-bottom:12px; overflow-wrap:anywhere; }
    .now { display:grid; grid-template-columns:96px 1fr; gap:12px; align-items:start; margin-bottom:12px; }
    .album-art { width:96px; height:96px; border-radius:8px; border:1px solid var(--line); object-fit:cover; background:#050707; display:none; }
    .grid { display:grid; gap:8px; grid-template-columns:1fr; }
    .row { display:flex; justify-content:space-between; gap:12px; border-top:1px solid rgba(255,255,255,.06); padding-top:8px; font-size:14px; }
    .row:first-child { border-top:0; padding-top:0; }
    .key { color:var(--muted); min-width:110px; }
    .value { text-align:right; overflow-wrap:anywhere; }
    .signal { display:inline-flex; align-items:flex-end; gap:2px; min-width:22px; height:16px; vertical-align:middle; margin-right:6px; }
    .signal i { display:block; width:4px; border-radius:2px 2px 0 0; background:#293436; }
    .signal i:nth-child(1) { height:5px; }
    .signal i:nth-child(2) { height:8px; }
    .signal i:nth-child(3) { height:11px; }
    .signal i:nth-child(4) { height:14px; }
    .signal.level-1 i:nth-child(-n+1), .signal.level-2 i:nth-child(-n+2) { background:#ff6f61; }
    .signal.level-3 i:nth-child(-n+3) { background:#f3d37b; }
    .signal.level-4 i:nth-child(-n+4) { background:var(--green); }
    .pill { display:inline-flex; align-items:center; min-height:24px; border-radius:999px; padding:2px 10px; background:#173721; color:#9df2b9; font-size:13px; }
    .pill.warn { background:#3b2d14; color:#f3d37b; }
    .pill.bad { background:#421b17; color:#ffb4aa; }
    .bar { height:8px; border:1px solid #39484a; border-radius:999px; overflow:hidden; background:#0b1112; }
    .bar > i { display:block; height:100%; width:0; background:var(--green); }
    .controls { display:grid; gap:10px; }
    label { display:grid; gap:5px; color:var(--muted); font-size:13px; }
    select, button, input:not([type]), input[type=text], input[type=password], input[type=file] { width:100%; min-height:42px; border-radius:8px; border:1px solid var(--line); background:#0c1112; color:var(--text); padding:8px 10px; font-size:15px; }
    input[type=range] { width:100%; accent-color:var(--green); }
    button { background:#173721; border-color:#25593a; color:#baf7ca; font-weight:700; cursor:pointer; }
    button.secondary { background:#12191a; color:#d6dfdc; }
    .section-action { margin-top:10px; }
    button.danger { background:#3a1714; border-color:#632b25; color:#ffd1c9; }
    .two { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
    .playback-actions { margin-top:12px; }
    .queue { display:grid; gap:8px; }
    .queue-item { border-top:1px solid rgba(255,255,255,.06); padding-top:8px; }
    .queue-item:first-child { border-top:0; padding-top:0; }
    .queue-title { font-size:14px; color:var(--text); overflow-wrap:anywhere; }
    .queue-subtitle { margin-top:2px; font-size:12px; color:var(--muted); overflow-wrap:anywhere; }
    .fine { color:var(--muted); font-size:12px; line-height:1.35; }
    .mono { font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; font-size:13px; }
    .status { margin-top:8px; color:#b7c5c1; font-size:13px; min-height:18px; }
    pre.logs { min-height:220px; max-height:360px; overflow:auto; margin:0; padding:10px; border:1px solid var(--line); border-radius:8px; background:#050707; color:#c7d2cf; font:12px/1.35 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; white-space:pre-wrap; overflow-wrap:anywhere; }
    @media (min-width:720px) { main { grid-template-columns:1fr 1fr; } .wide { grid-column:1 / -1; } }
  </style>
</head>
<body>
  <header>
    <h1><img class="brand-icon" src="/icon-192.png" alt="">SpotifyDJ <span id="appVersion" class="sub">v1.1.0</span></h1>
    <div class="sub"><span id="wifiHeaderSignal" class="signal level-0"><i></i><i></i><i></i><i></i></span><span id="ip">-</span> <span id="wifiState">-</span></div>
  </header>
  <main>
    <section class="panel wide">
      <h2>Now Playing</h2>
      <div class="now">
        <img id="albumArt" class="album-art" alt="Album art">
        <div>
          <div id="playbackPill" class="pill">Loading</div>
          <div id="track" class="hero-title">-</div>
          <div id="artist" class="artist">-</div>
        </div>
      </div>
      <div class="bar"><i id="progressBar"></i></div>
      <div class="row"><span class="key">Time</span><span id="time" class="value">-</span></div>
      <div class="two playback-actions">
        <button id="previousButton" class="secondary" type="button">Previous song</button>
        <button id="nextButton" class="secondary" type="button">Next song</button>
      </div>
      <div id="playbackCommandStatus" class="status"></div>
      <div class="row"><span class="key">Output</span><span id="device" class="value">-</span></div>
      <label>Sound output
        <select id="soundOutputSelect"><option value="">Loading outputs...</option></select>
      </label>
      <div id="soundOutputStatus" class="status"></div>
      <div class="row"><span class="key">Volume</span><span id="volume" class="value">-</span></div>
      <label>Volume
        <input id="volumeSlider" type="range" min="0" max="60" value="0">
      </label>
      <div id="volumeStatus" class="status"></div>
      <div class="row"><span class="key">Battery</span><span id="battery" class="value">-</span></div>
    </section>

    <section class="panel">
      <h2>Up Next</h2>
      <div id="queueList" class="queue"><div class="fine">Loading queue...</div></div>
      <div id="queueStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2>Settings</h2>
      <form id="settingsForm" class="controls">
        <label>Screen brightness
          <select id="brightness">
            <option value="25">25%</option><option value="50">50%</option><option value="75">75%</option><option value="100">100%</option>
          </select>
        </label>
        <label>Screen dim timeout
          <select id="offTimeout">
            <option value="30000">30 seconds</option><option value="60000">1 minute</option><option value="120000">2 minutes</option><option value="240000">4 minutes</option>
          </select>
        </label>
        <label>Deep sleep after
          <select id="sleepTimeout">
            <option value="300000">5 min</option><option value="900000">15 min</option><option value="1800000">30 min</option><option value="3600000">60 min</option>
          </select>
        </label>
        <label>MQTT host
          <input id="mqttHost" name="mqttHost" placeholder="192.168.1.10">
        </label>
        <label>MQTT port
          <input id="mqttPort" name="mqttPort" inputmode="numeric" placeholder="1883">
        </label>
        <label>MQTT username
          <input id="mqttUser" name="mqttUser" autocomplete="off">
        </label>
        <label>MQTT password
          <input id="mqttPass" name="mqttPass" type="password" autocomplete="off" placeholder="leave blank to keep">
        </label>
        <button type="submit">Save settings</button>
      </form>
      <div class="fine">Idle dim starts after 10s, reaches 50% after 20s. LED ring follows the screen power state.</div>
      <div id="settingsStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2>WiFi</h2>
      <div class="grid">
        <div class="row"><span class="key">State</span><span id="wifiConnected" class="value">-</span></div>
        <div class="row"><span class="key">SSID</span><span id="wifiSsid" class="value">-</span></div>
        <div class="row"><span class="key">RSSI</span><span class="value"><span id="wifiSignal" class="signal level-0"><i></i><i></i><i></i><i></i></span><span id="wifiRssi">-</span></span></div>
        <div class="row"><span class="key">MAC</span><span id="wifiMac" class="value">-</span></div>
      </div>
      <form id="wifiForm" class="controls">
        <label>New WiFi SSID
          <input id="wifiNewSsid" name="ssid" autocomplete="off" required>
        </label>
        <label>New WiFi password
          <input id="wifiNewPassword" name="password" type="password" autocomplete="new-password" placeholder="leave blank to keep current">
        </label>
        <button type="submit">Test WiFi and reboot</button>
      </form>
      <div class="fine">The device tests the new WiFi after this page responds. If it connects, credentials are saved and the device restarts automatically.</div>
      <div id="wifiSettingsStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2>Home Assistant</h2>
      <div class="grid">
        <div class="row"><span class="key">Pairing</span><span id="haPaired" class="value">-</span></div>
        <div class="row"><span class="key">Pair code</span><span id="haPairCode" class="value mono">-</span></div>
        <div class="row"><span class="key">Device ID</span><span id="haDeviceId" class="value mono">-</span></div>
        <div class="row"><span class="key">mDNS URL</span><span id="haMdnsUrl" class="value mono">-</span></div>
        <div class="row"><span class="key">mDNS service</span><span id="haMdnsService" class="value mono">_spotifydj._tcp</span></div>
        <div class="row"><span class="key">Firmware</span><span id="haFirmware" class="value">-</span></div>
        <div class="row"><span class="key">Model</span><span id="haModel" class="value">-</span></div>
        <div class="row"><span class="key">HA URL</span><span id="haUrl" class="value mono">-</span></div>
      </div>
      <div class="fine">Discovery advertises <span class="mono">_spotifydj._tcp</span> with TXT records including device_id, paired, version, api and model.</div>
      <div id="haStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2>Spotify</h2>
      <div class="grid">
        <div class="row"><span class="key">Connection</span><span id="spotifyState" class="value">-</span></div>
        <div class="row"><span class="key">Token</span><span id="spotifyToken" class="value">-</span></div>
        <div class="row"><span class="key">Refresh token</span><span id="spotifyRefresh" class="value">-</span></div>
        <div class="row"><span class="key">Error</span><span id="spotifyError" class="value">-</span></div>
      </div>
      <button id="refreshButton" class="secondary section-action" type="button">Refresh Spotify</button>
      <div id="refreshStatus" class="status"></div>
    </section>

    <section class="panel">
      <h2>MQTT</h2>
      <div class="grid">
        <div class="row"><span class="key">State</span><span id="mqttState" class="value">-</span></div>
        <div class="row"><span class="key">Broker</span><span id="mqttBroker" class="value">-</span></div>
        <div class="row"><span class="key">Username</span><span id="mqttUsername" class="value">-</span></div>
        <div class="row"><span class="key">HA discovery</span><span id="mqttDiscovery" class="value">-</span></div>
        <div class="row"><span class="key">Last published</span><span id="mqttLastPublished" class="value">-</span></div>
      </div>
    </section>

    <section class="panel">
      <h2>Diagnostics</h2>
      <div class="grid">
        <div class="row"><span class="key">Screen</span><span id="screenState" class="value">-</span></div>
        <div class="row"><span class="key">LED ring</span><span id="ledState" class="value">-</span></div>
        <div class="row"><span class="key">Uptime</span><span id="uptime" class="value">-</span></div>
        <div class="row"><span class="key">Loop load</span><span id="cpu" class="value">-</span></div>
        <div class="row"><span class="key">Heap</span><span id="heap" class="value">-</span></div>
        <div class="row"><span class="key">Storage</span><span id="storage" class="value">-</span></div>
        <div class="row"><span class="key">Sketch</span><span id="sketch" class="value">-</span></div>
      </div>
    </section>

    <section class="panel wide">
      <h2>Logs</h2>
      <div class="two">
        <button id="pauseLogsButton" class="secondary" type="button">Pause logs</button>
        <button id="copyLogsButton" class="secondary" type="button">Copy all</button>
      </div>
      <div id="logsStatus" class="status"></div>
      <pre id="logs" class="logs">Loading logs...</pre>
    </section>

    <section class="panel">
      <h2>Firmware OTA</h2>
      <form id="otaForm" class="controls">
        <input id="firmware" name="firmware" type="file" accept=".bin" required>
        <button type="submit">Upload Arduino firmware</button>
      </form>
      <div class="fine">Use the PlatformIO firmware.bin from .pio/build/t_embed_cc1101/firmware.bin.</div>
      <div id="otaStatus" class="status"></div>
      <button id="rebootButton" class="danger" type="button">Restart device</button>
      <button id="hardResetButton" class="danger" type="button">Hard reset to setup portal</button>
    </section>
  </main>

  <script>
    const $ = id => document.getElementById(id);
    const text = (id, value) => { $(id).textContent = value ?? "-"; };
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
    function setInput(id, value) {
      const el = $(id);
      if (document.activeElement !== el) el.value = value ?? "";
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
      $(id).title = connected ? `WiFi signal ${rssi} dBm` : "WiFi disconnected";
    }
    let volumeTimer = 0;
    let logsPaused = false;
    let soundOutputLoadedAt = 0;
    let queueLoadedAt = 0;
    let pairingInfoLoadedAt = 0;
    function render(data) {
      text("appVersion", data.app.version);
      text("ip", data.wifi.ip || "No IP");
      text("wifiState", data.wifi.status);
      text("track", data.playback.track || "Nothing playing");
      text("artist", data.playback.artist || data.playback.type || "-");
      const albumArt = $("albumArt");
      if (data.playback.albumImageUrl) {
        albumArt.src = data.playback.albumImageUrl;
        albumArt.style.display = "block";
      } else {
        albumArt.removeAttribute("src");
        albumArt.style.display = "none";
      }
      const state = data.playback.isPlaying ? "Playing" : data.playback.hasPlayback ? "Paused" : "No playback";
      text("playbackPill", state);
      pill($("playbackPill"), data.playback.isPlaying ? "ok" : data.playback.hasPlayback ? "warn" : "bad");
      text("time", `${duration(data.playback.progressMs)} / ${duration(data.playback.durationMs)}`);
      $("progressBar").style.width = `${data.playback.progressPercent || 0}%`;
      text("device", data.device.name || "-");
      text("volume", data.device.volume >= 0 ? `${data.device.volume}%` : "-");
      setInput("volumeSlider", data.device.volume >= 0 ? String(data.device.volume) : "0");
      text("battery", `${data.battery.label} ${data.battery.charging ? "charging" : data.battery.full ? "full" : data.battery.discharging ? "discharging" : ""}`);
      text("wifiConnected", data.wifi.connected ? "Connected" : "Disconnected");
      text("wifiSsid", data.wifi.ssid || "-");
      setInput("wifiNewSsid", data.wifi.ssid || "");
      text("wifiRssi", data.wifi.rssi ? `${data.wifi.rssi} dBm` : "-");
      setWifiSignal("wifiSignal", data.wifi.connected, data.wifi.rssi);
      setWifiSignal("wifiHeaderSignal", data.wifi.connected, data.wifi.rssi);
      text("wifiMac", data.wifi.mac);
      text("spotifyState", data.spotify.authorized ? "Authorized" : "Not authorized");
      text("spotifyToken", data.spotify.authorized ? `${data.spotify.tokenExpiresInSec}s left` : "-");
      text("spotifyRefresh", data.spotify.refreshTokenSource);
      text("spotifyError", data.spotify.error || "-");
      text("screenState", `${data.screen.state} (${data.screen.brightnessLevel}%)`);
      text("ledState", data.led.state);
      text("uptime", duration(data.app.uptimeMs));
      text("cpu", `${data.system.cpuUsagePercent}% loop load`);
      text("heap", `${bytes(data.system.heapUsed)} used / ${bytes(data.system.heapTotal)} total, ${bytes(data.system.heapFree)} free`);
      text("storage", `${bytes(data.system.otaFree)} OTA free / ${bytes(data.system.otaTotal)} OTA total`);
      text("sketch", `${bytes(data.system.sketchSize)} sketch / ${bytes(data.system.flashSize)} flash`);
      setInput("brightness", String(data.settings.screenBrightnessPercent));
      setInput("offTimeout", String(data.settings.screenOffTimeoutMs));
      setInput("sleepTimeout", String(data.settings.deviceSleepTimeoutMs));
      setInput("mqttHost", data.mqtt.host || "");
      setInput("mqttPort", String(data.mqtt.port || 1883));
      setInput("mqttUser", data.mqtt.username || "");
      text("mqttState", data.mqtt.state || "Disabled");
      text("mqttBroker", data.mqtt.host ? `${data.mqtt.host}:${data.mqtt.port}` : "-");
      text("mqttUsername", data.mqtt.username || "-");
      text("mqttDiscovery", data.mqtt.connected ? "Published after connect" : data.mqtt.enabled ? (data.mqtt.state || "Waiting for broker") : "Disabled");
      text("mqttLastPublished", data.mqtt.lastPublishedMs ? `${duration(data.mqtt.lastPublishedMs)} uptime, ${duration(Math.max(0, data.app.uptimeMs - data.mqtt.lastPublishedMs))} ago` : "-");
    }
    async function refresh() {
      const response = await fetch("/api/status", { cache: "no-store" });
      render(await response.json());
      if (Date.now() - pairingInfoLoadedAt > 5000) loadPairingInfo();
      if (Date.now() - soundOutputLoadedAt > 15000) loadSoundOutputs();
      if (Date.now() - queueLoadedAt > 15000) loadQueue();
    }
    async function loadPairingInfo() {
      pairingInfoLoadedAt = Date.now();
      try {
        const infoResponse = await fetch("/api/device/info", { cache: "no-store" });
        const info = await infoResponse.json();
        text("haPaired", info.paired ? "Paired" : "Pairing mode");
        text("haDeviceId", info.device_id || "-");
        text("haMdnsUrl", info.device_id ? `http://${info.device_id}.local` : "-");
        text("haFirmware", info.firmware || "-");
        text("haModel", info.model || "-");
        text("haUrl", info.ha_url || "-");
        text("haStatus", info.spotify_configured ? "Spotify credentials stored" : "Spotify credentials missing");
        if (info.paired) {
          text("haPairCode", "-");
          return;
        }
        const pairResponse = await fetch("/api/device/pairing-info", { cache: "no-store" });
        const pair = await pairResponse.json();
        text("haPairCode", pair.pair_code || "-");
        text("haMdnsUrl", pair.local_url || (pair.device_id ? `http://${pair.device_id}.local` : "-"));
      } catch (error) {
        text("haStatus", "Pairing info unavailable");
      }
    }
    async function loadSoundOutputs() {
      const select = $("soundOutputSelect");
      soundOutputLoadedAt = Date.now();
      try {
        const response = await fetch("/api/devices", { cache: "no-store" });
        const data = await response.json();
        select.innerHTML = "";
        if (!data.devices || data.devices.length === 0) {
          select.innerHTML = '<option value="">No outputs</option>';
          return;
        }
        for (const device of data.devices) {
          const option = document.createElement("option");
          option.value = device.id;
          option.textContent = device.active ? `${device.name} *` : device.name;
          option.selected = !!device.active;
          select.appendChild(option);
        }
      } catch (error) {
        select.innerHTML = '<option value="">Outputs failed</option>';
      }
    }
    async function loadQueue() {
      const list = $("queueList");
      queueLoadedAt = Date.now();
      try {
        const response = await fetch("/api/queue", { cache: "no-store" });
        const data = await response.json();
        list.innerHTML = "";
        if (!data.items || data.items.length === 0) {
          list.innerHTML = `<div class="fine">${data.error || "No queued songs"}</div>`;
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
        list.innerHTML = '<div class="fine">Queue failed</div>';
      }
    }
    async function refreshLogs() {
      if (logsPaused) return;
      const response = await fetch("/api/logs", { cache: "no-store" });
      const value = await response.text();
      const logs = $("logs");
      logs.textContent = value || "No logs yet";
      logs.scrollTop = logs.scrollHeight;
    }
    function queueVolumeUpdate() {
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
      const deviceId = $("soundOutputSelect").value;
      if (!deviceId) return;
      $("soundOutputStatus").textContent = "Switching output...";
      const body = new URLSearchParams({ deviceId });
      const response = await fetch("/api/transfer", { method:"POST", body });
      $("soundOutputStatus").textContent = await response.text();
      await refresh();
      await loadSoundOutputs();
    });
    async function sendPlaybackCommand(action) {
      $("playbackCommandStatus").textContent = action === "next" ? "Skipping..." : "Going back...";
      const body = new URLSearchParams({ action });
      const response = await fetch("/api/playback", { method:"POST", body });
      $("playbackCommandStatus").textContent = await response.text();
      await refresh();
      await loadQueue();
    }
    $("previousButton").addEventListener("click", () => sendPlaybackCommand("previous"));
    $("nextButton").addEventListener("click", () => sendPlaybackCommand("next"));
    $("pauseLogsButton").addEventListener("click", () => {
      logsPaused = !logsPaused;
      $("pauseLogsButton").textContent = logsPaused ? "Resume logs" : "Pause logs";
      $("logsStatus").textContent = logsPaused ? "Logs paused" : "Logs live";
      if (!logsPaused) refreshLogs();
    });
    $("copyLogsButton").addEventListener("click", async () => {
      const logs = $("logs").textContent || "";
      try {
        await navigator.clipboard.writeText(logs);
        $("logsStatus").textContent = "Logs copied";
      } catch (error) {
        const range = document.createRange();
        range.selectNodeContents($("logs"));
        const selection = window.getSelection();
        selection.removeAllRanges();
        selection.addRange(range);
        $("logsStatus").textContent = "Logs selected";
      }
    });
    $("settingsForm").addEventListener("submit", async event => {
      event.preventDefault();
      $("settingsStatus").textContent = "Saving...";
      const body = new URLSearchParams({
        brightness: $("brightness").value,
        offTimeoutMs: $("offTimeout").value,
        sleepTimeoutMs: $("sleepTimeout").value,
        mqttHost: $("mqttHost").value,
        mqttPort: $("mqttPort").value,
        mqttUser: $("mqttUser").value,
        mqttPass: $("mqttPass").value
      });
      const response = await fetch("/api/settings", { method:"POST", body });
      $("settingsStatus").textContent = await response.text();
      refresh();
    });
    $("wifiForm").addEventListener("submit", async event => {
      event.preventDefault();
      if (!confirm("Test these WiFi credentials? The web page may disconnect during the test.")) return;
      $("wifiSettingsStatus").textContent = "Starting WiFi test...";
      const body = new URLSearchParams({
        ssid: $("wifiNewSsid").value,
        password: $("wifiNewPassword").value
      });
      const response = await fetch("/api/wifi", { method:"POST", body });
      $("wifiSettingsStatus").textContent = await response.text();
    });
    $("refreshButton").addEventListener("click", async () => {
      $("refreshStatus").textContent = "Refreshing...";
      const response = await fetch("/api/refresh", { method:"POST" });
      $("refreshStatus").textContent = await response.text();
      refresh();
    });
    $("rebootButton").addEventListener("click", async () => {
      if (!confirm("Restart SpotifyDJ?")) return;
      $("otaStatus").textContent = await (await fetch("/api/reboot", { method:"POST" })).text();
    });
    $("hardResetButton").addEventListener("click", async () => {
      if (!confirm("Delete local WiFi credentials, tokens and caches, then reboot into setup AP mode?")) return;
      $("otaStatus").textContent = await (await fetch("/api/hard-reset", { method:"POST" })).text();
    });
    $("otaForm").addEventListener("submit", async event => {
      event.preventDefault();
      const file = $("firmware").files[0];
      if (!file) return;
      $("otaStatus").textContent = "Uploading firmware...";
      const form = new FormData();
      form.append("firmware", file, file.name);
      const response = await fetch("/ota", { method:"POST", body:form });
      $("otaStatus").textContent = await response.text();
    });
    refresh();
    loadPairingInfo();
    loadSoundOutputs();
    loadQueue();
    refreshLogs();
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
    MqttPublisher &mqttPublisher,
    const MqttSettings &mqttSettings,
    const uint8_t &screenBrightnessPercent,
    const uint32_t &screenOffTimeoutMs,
    const uint32_t &deviceSleepTimeoutMs,
    void *callbackContext,
    SettingsCallback settingsCallback,
    MqttSettingsCallback mqttSettingsCallback,
    WifiSettingsCallback wifiSettingsCallback,
    SimpleCallback refreshCallback,
    SimpleCallback hardResetCallback) {
  playback_ = &playback;
  battery_ = &battery;
  diagnostics_ = &diagnostics;
  visualState_ = &visualState;
  spotify_ = &spotify;
  mqttPublisher_ = &mqttPublisher;
  mqttSettings_ = &mqttSettings;
  screenBrightnessPercent_ = &screenBrightnessPercent;
  screenOffTimeoutMs_ = &screenOffTimeoutMs;
  deviceSleepTimeoutMs_ = &deviceSleepTimeoutMs;
  callbackContext_ = callbackContext;
  settingsCallback_ = settingsCallback;
  mqttSettingsCallback_ = mqttSettingsCallback;
  wifiSettingsCallback_ = wifiSettingsCallback;
  refreshCallback_ = refreshCallback;
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
    server_.send_P(200, "image/x-icon", reinterpret_cast<const char *>(SPOTIFYDJ_FAVICON_ICO), SPOTIFYDJ_FAVICON_ICO_LEN);
  });
  server_.on("/icon-192.png", HTTP_GET, [this]() {
    server_.send_P(200, "image/png", reinterpret_cast<const char *>(SPOTIFYDJ_ICON_192_PNG), SPOTIFYDJ_ICON_192_PNG_LEN);
  });
  server_.on("/site.webmanifest", HTTP_GET, [this]() {
    server_.send_P(200, "application/manifest+json", reinterpret_cast<const char *>(SPOTIFYDJ_SITE_WEBMANIFEST), SPOTIFYDJ_SITE_WEBMANIFEST_LEN);
  });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatusJson(); });
  server_.on("/api/logs", HTTP_GET, [this]() { handleLogsText(); });
  server_.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  server_.on("/api/wifi", HTTP_POST, [this]() { handleWifiPost(); });
  server_.on("/api/volume", HTTP_POST, [this]() { handleVolumePost(); });
  server_.on("/api/devices", HTTP_GET, [this]() { handleDevicesJson(); });
  server_.on("/api/queue", HTTP_GET, [this]() { handleQueueJson(); });
  server_.on("/api/transfer", HTTP_POST, [this]() { handleTransferPost(); });
  server_.on("/api/playback", HTTP_POST, [this]() { handlePlaybackCommandPost(); });
  server_.on("/api/refresh", HTTP_POST, [this]() { handleRefreshPost(); });
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
  server_.send_P(200, "text/html", IndexHtml);
}

void WebPortal::handleStatusJson() {
  if (playback_ == nullptr || battery_ == nullptr || diagnostics_ == nullptr || spotify_ == nullptr) {
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
  spotify["refreshTokenSource"] = spotify_->refreshTokenSource();
  spotify["error"] = playback_->error;

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

  String body;
  serializeJson(doc, body);
  server_.send(200, "application/json", body);
}

void WebPortal::handleLogsText() {
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
  if (settingsCallback_ != nullptr) {
    settingsCallback_(callbackContext_, brightness, offTimeoutMs, sleepTimeoutMs);
  }

  if (mqttSettingsCallback_ != nullptr) {
    MqttSettings mqttSettings;
    mqttSettings.host = server_.arg("mqttHost");
    mqttSettings.host.trim();
    mqttSettings.port = server_.arg("mqttPort").toInt() > 0 ? server_.arg("mqttPort").toInt() : 1883;
    mqttSettings.username = server_.arg("mqttUser");
    mqttSettings.password = server_.arg("mqttPass");
    if (mqttSettings.password.isEmpty() && mqttSettings_ != nullptr) {
      mqttSettings.password = mqttSettings_->password;
    }
    mqttSettings.enabled = !mqttSettings.host.isEmpty();
    mqttSettingsCallback_(callbackContext_, mqttSettings);
  }
  server_.send(200, "text/plain", "Settings saved");
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
  server_.send(202, "text/plain", "WiFi test started. Device will reboot automatically if the connection succeeds.");
}

void WebPortal::handleVolumePost() {
  if (spotify_ == nullptr || !server_.hasArg("volume")) {
    server_.send(400, "text/plain", "Missing volume");
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

void WebPortal::handleTransferPost() {
  if (spotify_ == nullptr || !server_.hasArg("deviceId")) {
    server_.send(400, "text/plain", "Missing output");
    return;
  }

  const String deviceId = server_.arg("deviceId");
  if (deviceId.isEmpty()) {
    server_.send(400, "text/plain", "Missing output");
    return;
  }

  if (!spotify_->transferPlayback(deviceId, true)) {
    server_.send(502, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Output switch failed" : playback_->error);
    return;
  }

  spotify_->refreshPlayback();
  server_.send(200, "text/plain", "Output switched");
}

void WebPortal::handlePlaybackCommandPost() {
  if (spotify_ == nullptr || !server_.hasArg("action")) {
    server_.send(400, "text/plain", "Missing playback action");
    return;
  }

  const String action = server_.arg("action");
  bool ok = false;
  if (action == "next") {
    ok = spotify_->nextTrack();
  } else if (action == "previous") {
    ok = spotify_->previousTrack();
  } else {
    server_.send(400, "text/plain", "Unknown playback action");
    return;
  }

  if (!ok) {
    server_.send(502, "text/plain", playback_ == nullptr || playback_->error.isEmpty() ? "Playback command failed" : playback_->error);
    return;
  }

  spotify_->refreshPlayback();
  server_.send(200, "text/plain", action == "next" ? "Next song" : "Previous song");
}

void WebPortal::handleRefreshPost() {
  if (refreshCallback_ != nullptr) {
    refreshCallback_(callbackContext_);
  }
  server_.send(200, "text/plain", "Refresh requested");
}

void WebPortal::handleRebootPost() {
  server_.send(200, "text/plain", "Restarting...");
  delay(250);
  ESP.restart();
}

void WebPortal::handleHardResetPost() {
  server_.send(200, "text/plain", "Hard reset requested. Rebooting into setup portal...");
  delay(250);
  if (hardResetCallback_ != nullptr) {
    hardResetCallback_(callbackContext_);
  }
}

void WebPortal::handleOtaFinished() {
  if (otaOk_ && !Update.hasError()) {
    server_.send(200, "text/plain", "Firmware uploaded. Rebooting...");
    delay(500);
    ESP.restart();
    return;
  }

  server_.send(500, "text/plain", "Firmware update failed");
}

void WebPortal::handleOtaUpload() {
  HTTPUpload &upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaOk_ = Update.begin(UPDATE_SIZE_UNKNOWN);
    AppLog.print("OTA upload: ");
    AppLog.println(upload.filename);
    if (!otaOk_) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaOk_ && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaOk_ = false;
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
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
  return String(battery_->percentEstimated ? "~" : "") + String(battery_->percent) + "%";
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
