#!/usr/bin/env node
"use strict";

const http = require("http");
const { assert, contractFixture, postJSON, startContractServer } = require("./ha_contract_fixture");

function getRaw(url, headers = {}) {
  return new Promise((resolve, reject) => {
    const request = http.request(url, { method: "GET", headers }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => resolve({ statusCode: response.statusCode, headers: response.headers, body: Buffer.concat(chunks) }));
    });
    request.on("error", reject);
    request.end();
  });
}

function postRaw(url, body, headers = {}) {
  return new Promise((resolve, reject) => {
    const data = Buffer.isBuffer(body) ? body : Buffer.from(String(body));
    const request = http.request(url, {
      method: "POST",
      headers: { "content-length": data.length, ...headers },
    }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => {
        resolve({ statusCode: response.statusCode, body: JSON.parse(Buffer.concat(chunks).toString("utf8")) });
      });
    });
    request.on("error", reject);
    request.end(data);
  });
}

function authHeaders() {
  return { authorization: `Bearer ${contractFixture.deviceToken}` };
}

function identity(extra = {}) {
  return {
    device_id: contractFixture.deviceId,
    client_id: contractFixture.clientId,
    device_name: contractFixture.deviceName,
    client_type: contractFixture.clientType,
    ...extra,
  };
}

async function post(baseURL, path, body) {
  const response = await postJSON(`${baseURL}${path}`, body, authHeaders());
  assert(response.statusCode === 200, `${path} failed with HTTP ${response.statusCode}`);
  return response.body;
}

async function run() {
  const { baseURL, observed, server, sockets } = await startContractServer();
  try {
    const pair = await postJSON(`${baseURL}/api/djconnect/v1/pair`, {
      ...identity(),
      pair_code: "123456",
      firmware: "3.2.10",
      model: "lilygo-t-embed-s3",
      local_url: "http://djconnect-lilygo-t-embed-s3-ci.local",
      language: "en",
    });
    assert(pair.statusCode === 200, "pair failed");
    assert(pair.body.device_token === contractFixture.deviceToken, "pair token mismatch");
    assert(pair.body.ha_local_url === baseURL, "pair response missing LAN HA URL");

    const status = await post(baseURL, "/api/djconnect/v1/status", {
      ...identity(),
      request_source: "device_status",
      ha_pairing_status: "paired",
      local_url: "http://djconnect-lilygo-t-embed-s3-ci.local",
      ha_local_url: baseURL,
      firmware: "3.2.10",
      battery_percent: 88,
      wifi_rssi: -42,
      screen_brightness: 80,
      screen_brightness_percent: 80,
      screen_dim_timeout_ms: 60000,
      turn_off_after_ms: 900000,
      speaker_volume: 50,
      speaker_volume_percent: 50,
      language: "en",
      theme: "dark",
      log_level: "info",
      wake_word_enabled: false,
      ota_state: "idle",
      update_state: "idle",
      screen_state: "on",
      led_state: "on",
      sound_output: "None",
      settings: { language: "en", theme: "dark", log_level: "info" },
      capabilities: {
        profiles: true,
        request_context: true,
        private_sessions: true,
      },
      contract_versions: {
        profile_context: 1,
        client_contract_fixtures: 1,
      },
    });
    assert(status.playback?.track_name === "Contract Track", "status response missing playback");

    const command = await post(baseURL, "/api/djconnect/v1/command", {
      ...identity(),
      payload_type: "command",
      request_source: "device_command",
      firmware: "3.2.10",
      command: "status",
      capabilities: { profiles: true, request_context: true, private_sessions: true },
    });
    assert(command.music_backend_available === true, "command missing backend summary");

    const queue = await post(baseURL, "/api/djconnect/v1/command", {
      ...identity(),
      payload_type: "command",
      request_source: "device_command",
      firmware: "3.2.10",
      command: "queue",
      limit: 100,
      capabilities: { profiles: true, request_context: true, private_sessions: true },
    });
    assert(queue.queue?.items?.length === 1, "queue contract failed");

    const playlists = await post(baseURL, "/api/djconnect/v1/command", {
      ...identity(),
      payload_type: "command",
      request_source: "device_command",
      firmware: "3.2.10",
      command: "playlists",
      limit: 20,
      capabilities: { profiles: true, request_context: true, private_sessions: true },
    });
    assert(playlists.playlists?.length === 1, "playlists contract failed");

    const event = await post(baseURL, "/api/djconnect/v1/event", { ...identity(), event: "boot" });
    assert(event.success === true, "event contract failed");

    const voice = await postRaw(`${baseURL}/api/djconnect/v1/voice`, Buffer.from("RIFF....WAVEfmt "), {
      ...authHeaders(),
      "content-type": "audio/wav",
      "x-djconnect-device-id": contractFixture.deviceId,
      "x-djconnect-client-id": contractFixture.clientId,
      "x-djconnect-device-name": contractFixture.deviceName,
      "x-djconnect-client-type": contractFixture.clientType,
      "x-djconnect-request-source": "voice",
    });
    assert(voice.statusCode === 200 && voice.body.text === "Contract DJ response", "voice contract failed");

    const tts = await getRaw(`${baseURL}/api/djconnect/v1/tts/contract.wav`, authHeaders());
    assert(tts.statusCode === 200 && tts.headers["content-type"] === "audio/wav", "tts contract failed");

    const paths = observed.httpRequests.map((request) => request.path);
    const expected = [
      "/api/djconnect/v1/pair",
      "/api/djconnect/v1/status",
      "/api/djconnect/v1/command",
      "/api/djconnect/v1/command",
      "/api/djconnect/v1/command",
      "/api/djconnect/v1/event",
      "/api/djconnect/v1/voice",
      "/api/djconnect/v1/tts/contract.wav",
    ];
    assert(JSON.stringify(paths) === JSON.stringify(expected), `unexpected HTTP route order: ${JSON.stringify(paths)}`);
    assert(observed.sessionCalls === 0, "HTTP e2e should not call websocket session");
    console.log(`ESP32 HTTP contract e2e passed: ${paths.length} HTTP routes.`);
  } finally {
    for (const socket of sockets) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
  }
}

if (require.main === module) {
  run().catch((error) => {
    console.error(`ESP32 HTTP contract e2e failed: ${error.message}`);
    process.exit(1);
  });
}
