#!/usr/bin/env node
"use strict";

const crypto = require("crypto");
const http = require("http");
const net = require("net");

const DEVICE_ID = "djconnect-lilygo-t-embed-s3-ci";
const DEVICE_NAME = "DJConnect LilyGO CI";
const CLIENT_TYPE = "esp32";
const DEVICE_TOKEN = "ci_djconnect_device_token";
const WS_TOKEN = "ci_ha_ws_session_token";
const WS_ROUTES = ["djconnect/capabilities", "djconnect/command", "djconnect/status"];

const contractFixture = {
  clientId: DEVICE_ID,
  clientType: CLIENT_TYPE,
  deviceId: DEVICE_ID,
  deviceName: DEVICE_NAME,
  deviceToken: DEVICE_TOKEN,
  webSocketToken: WS_TOKEN,
  webSocketRoutes: WS_ROUTES,
};

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function readRequestBody(request) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    request.on("data", (chunk) => chunks.push(chunk));
    request.on("end", () => resolve(Buffer.concat(chunks)));
    request.on("error", reject);
  });
}

function jsonResponse(response, statusCode, body) {
  const data = Buffer.from(JSON.stringify(body));
  response.writeHead(statusCode, {
    "content-type": "application/json",
    "content-length": data.length,
  });
  response.end(data);
}

function rawResponse(response, statusCode, body, contentType) {
  const data = Buffer.isBuffer(body) ? body : Buffer.from(String(body));
  response.writeHead(statusCode, {
    "content-type": contentType,
    "content-length": data.length,
  });
  response.end(data);
}

function parseJSON(buffer, route) {
  try {
    return JSON.parse(buffer.toString("utf8"));
  } catch {
    throw new Error(`${route} body is not valid JSON`);
  }
}

function assertBearer(request, route) {
  assert(request.headers.authorization === `Bearer ${DEVICE_TOKEN}`, `${route} missing bearer auth`);
}

function identityFrom(body) {
  return body.identity || body.payload?.identity || body;
}

function assertIdentity(body, route) {
  const identity = identityFrom(body);
  assert(identity.device_id === DEVICE_ID || body.device_id === DEVICE_ID, `${route} missing device_id`);
  assert(identity.client_id === DEVICE_ID || body.client_id === DEVICE_ID, `${route} missing client_id`);
  assert(identity.device_name === DEVICE_NAME || body.device_name === DEVICE_NAME, `${route} missing device_name`);
  assert(identity.client_type === CLIENT_TYPE || body.client_type === CLIENT_TYPE, `${route} missing client_type=esp32`);
  assert(body.device_type === undefined, `${route} must not send device_type`);
}

function backendSummary() {
  return {
    music_backend: "mock_backend",
    music_backend_name: "Mock Music Backend",
    music_backend_available: true,
    music_backend_revision: "ci-1",
    music_backend_capabilities: ["playback", "queue", "playlists", "outputs", "voice"],
  };
}

function playbackResponse(extra = {}) {
  return {
    success: true,
    backend_available: true,
    playback: {
      has_playback: true,
      is_playing: true,
      track_name: "Contract Track",
      artist_name: "Contract Artist",
      album_image_url: "http://127.0.0.1/album-art-not-fetched-in-ci.jpg",
      volume_percent: 35,
      supports_volume: true,
    },
    ...backendSummary(),
    ...extra,
  };
}

function websocketAcceptValue(key) {
  return crypto.createHash("sha1").update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest("base64");
}

function encodeFrame(payload, { masked = false } = {}) {
  const data = Buffer.from(payload);
  const header = [0x81];
  if (data.length < 126) {
    header.push((masked ? 0x80 : 0) | data.length);
  } else if (data.length < 65536) {
    header.push((masked ? 0x80 : 0) | 126, (data.length >> 8) & 0xff, data.length & 0xff);
  } else {
    throw new Error("Frame too large for contract test");
  }
  let frame = Buffer.concat([Buffer.from(header), data]);
  if (masked) {
    const mask = crypto.randomBytes(4);
    const start = header.length + 4;
    frame = Buffer.concat([Buffer.from(header), mask, data]);
    for (let index = 0; index < data.length; index += 1) {
      frame[start + index] = data[index] ^ mask[index % 4];
    }
  }
  return frame;
}

function decodeFrame(buffer) {
  if (buffer.length < 2) return null;
  const opcode = buffer[0] & 0x0f;
  const masked = (buffer[1] & 0x80) !== 0;
  let length = buffer[1] & 0x7f;
  let offset = 2;
  if (length === 126) {
    if (buffer.length < offset + 2) return null;
    length = buffer.readUInt16BE(offset);
    offset += 2;
  } else if (length === 127) {
    throw new Error("64-bit websocket frames are not supported");
  }
  let mask;
  if (masked) {
    if (buffer.length < offset + 4) return null;
    mask = buffer.subarray(offset, offset + 4);
    offset += 4;
  }
  if (buffer.length < offset + length) return null;
  const payload = Buffer.from(buffer.subarray(offset, offset + length));
  if (masked) {
    for (let index = 0; index < payload.length; index += 1) {
      payload[index] ^= mask[index % 4];
    }
  }
  return { opcode, text: payload.toString("utf8"), consumed: offset + length };
}

function sendJSON(socket, value) {
  socket.write(encodeFrame(JSON.stringify(value)));
}

function resultForMessage(message) {
  const body = { ...message, ...(message.payload || {}) };
  assertIdentity(body, message.type);

  if (message.type === "djconnect/capabilities") {
    return {
      id: message.id,
      type: "result",
      success: true,
      result: {
        success: true,
        websocket_supported: true,
        transports: { websocket: true, http: true },
        commands: WS_ROUTES,
        features: { playback: true, esp32_status: true },
        fallbacks: { voice: "/api/djconnect/v1/voice", tts: "/api/djconnect/v1/tts/{id}.wav" },
      },
    };
  }

  if (message.type === "djconnect/command") {
    assert(typeof body.command === "string" && body.command.length > 0, "djconnect/command missing command");
    return { id: message.id, type: "result", success: true, result: playbackResponse() };
  }

  if (message.type === "djconnect/status") {
    return {
      id: message.id,
      type: "result",
      success: true,
      result: { success: true, state: "online", ...backendSummary() },
    };
  }

  return {
    id: message.id,
    type: "result",
    success: false,
    error: { code: "unsupported", message: "Unsupported ESP32 contract route" },
  };
}

async function handleHTTPContractRoute(request, response, observed, baseURL) {
  const url = new URL(request.url, baseURL || "http://127.0.0.1");
  const path = url.pathname;

  if (request.method === "POST" && path === "/api/djconnect/v1/pair") {
    const body = parseJSON(await readRequestBody(request), path);
    assertIdentity(body, path);
    assert(typeof body.pair_code === "string" && body.pair_code.length === 6, "pair missing six-digit pair_code");
    observed.httpRequests.push({ method: request.method, path });
    jsonResponse(response, 200, {
      success: true,
      client_type: CLIENT_TYPE,
      client_id: DEVICE_ID,
      device_id: DEVICE_ID,
      device_name: DEVICE_NAME,
      device_token: DEVICE_TOKEN,
      ha_local_url: baseURL,
      assist_pipeline_id: "ci-assist-pipeline",
      api_base: "/api/djconnect/v1",
      voice_path: "/api/djconnect/v1/voice",
      status_path: "/api/djconnect/v1/status",
      event_path: "/api/djconnect/v1/event",
      ...backendSummary(),
    });
    return true;
  }

  if (request.method === "POST" && path === "/api/djconnect/v1/websocket/session") {
    assertBearer(request, path);
    const body = parseJSON(await readRequestBody(request), path);
    assertIdentity(body, path);
    assert(body.access_token === undefined, "session request leaked HA access_token");
    assert(body.home_assistant_token === undefined, "session request leaked HA token");
    assert(Array.isArray(body.requested_commands), "session missing requested_commands");
    observed.sessionCalls += 1;
    jsonResponse(response, 200, {
      success: true,
      access_token: WS_TOKEN,
      expires_in: 600,
      commands: WS_ROUTES,
    });
    return true;
  }

  if (!path.startsWith("/api/djconnect/v1/")) {
    return false;
  }

  assertBearer(request, path);
  observed.httpRequests.push({ method: request.method, path });

  if (request.method === "POST" && path === "/api/djconnect/v1/status") {
    const body = parseJSON(await readRequestBody(request), path);
    assertIdentity(body, path);
    assert(body.ha_pairing_status === "paired", "status missing paired state");
    jsonResponse(response, 200, playbackResponse({ state: "online" }));
    return true;
  }

  if (request.method === "POST" && path === "/api/djconnect/v1/command") {
    const body = parseJSON(await readRequestBody(request), path);
    assertIdentity(body, path);
    assert(body.payload_type === "command", "command missing payload_type=command");
    assert(typeof body.command === "string" && body.command.length > 0, "command missing command");
    const command = body.command;
    if (command === "queue") {
      assert(Number.isInteger(body.limit) && body.limit > 0, "queue missing positive limit");
      jsonResponse(response, 200, playbackResponse({ queue: { items: [{ title: "Contract Track", uri: "mock:track:1" }] } }));
      return true;
    }
    if (command === "playlists") {
      assert(Number.isInteger(body.limit) && body.limit > 0, "playlists missing positive limit");
      jsonResponse(response, 200, playbackResponse({ playlists: [{ name: "Contract Playlist", uri: "mock:playlist:1" }] }));
      return true;
    }
    jsonResponse(response, 200, playbackResponse());
    return true;
  }

  if (request.method === "POST" && path === "/api/djconnect/v1/event") {
    const body = parseJSON(await readRequestBody(request), path);
    assertIdentity(body, path);
    jsonResponse(response, 200, { success: true });
    return true;
  }

  if (request.method === "POST" && path === "/api/djconnect/v1/voice") {
    assert(request.headers["x-djconnect-device-id"] === DEVICE_ID, "voice missing X-DJConnect-Device-ID");
    assert(request.headers["x-djconnect-client-id"] === DEVICE_ID, "voice missing X-DJConnect-Client-ID");
    assert(request.headers["x-djconnect-device-name"] === DEVICE_NAME, "voice missing X-DJConnect-Device-Name");
    assert(request.headers["x-djconnect-client-type"] === CLIENT_TYPE, "voice missing X-DJConnect-Client-Type");
    const body = await readRequestBody(request);
    assert(body.length > 0, "voice missing body");
    jsonResponse(response, 200, {
      success: true,
      text: "Contract DJ response",
      dj_text: "Contract DJ response",
      audio_url: `${baseURL}/api/djconnect/v1/tts/contract.wav`,
      ...backendSummary(),
    });
    return true;
  }

  if (request.method === "GET" && /^\/api\/djconnect\/v1\/tts\/[^/]+\.wav$/.test(path)) {
    rawResponse(response, 200, Buffer.from("RIFF\x24\x00\x00\x00WAVEfmt "), "audio/wav");
    return true;
  }

  return false;
}

function startContractServer({ port = 0, host = "127.0.0.1" } = {}) {
  const observed = { httpRequests: [], sessionCalls: 0, websocketMessages: [] };
  const sockets = new Set();
  let baseURL = `http://${host}:${port}`;
  const server = http.createServer(async (request, response) => {
    try {
      if (await handleHTTPContractRoute(request, response, observed, baseURL)) {
        return;
      }
      jsonResponse(response, 404, { success: false, error: "not_found" });
    } catch (error) {
      jsonResponse(response, 500, { success: false, error: "contract_server_failed", message: error.message });
    }
  });

  server.on("upgrade", (request, socket) => {
    sockets.add(socket);
    socket.on("close", () => sockets.delete(socket));
    if (request.url !== "/api/websocket") {
      socket.destroy();
      return;
    }
    const key = request.headers["sec-websocket-key"];
    socket.write([
      "HTTP/1.1 101 Switching Protocols",
      "Upgrade: websocket",
      "Connection: Upgrade",
      `Sec-WebSocket-Accept: ${websocketAcceptValue(key)}`,
      "",
      "",
    ].join("\r\n"));
    sendJSON(socket, { type: "auth_required", ha_version: "2026.7.0" });

    let authenticated = false;
    let pending = Buffer.alloc(0);
    socket.on("data", (chunk) => {
      pending = Buffer.concat([pending, chunk]);
      for (;;) {
        const frame = decodeFrame(pending);
        if (!frame) return;
        pending = pending.subarray(frame.consumed);
        if (frame.opcode === 0x8) {
          socket.end();
          return;
        }
        const message = JSON.parse(frame.text);
        observed.websocketMessages.push({ type: message.type, id: message.id });
        if (!authenticated) {
          assert(message.type === "auth", "first websocket message must be auth");
          assert(message.access_token === WS_TOKEN, "websocket auth used wrong token");
          authenticated = true;
          sendJSON(socket, { type: "auth_ok", ha_version: "2026.7.0" });
          continue;
        }
        sendJSON(socket, resultForMessage(message));
      }
    });
  });

  return new Promise((resolve) => {
    server.listen(port, host, () => {
      const address = server.address();
      baseURL = `http://${host}:${address.port}`;
      resolve({ baseURL, observed, server, sockets });
    });
  });
}

function postJSON(url, body, headers = {}) {
  return new Promise((resolve, reject) => {
    const data = Buffer.from(JSON.stringify(body));
    const request = http.request(url, {
      method: "POST",
      headers: { "content-type": "application/json", "content-length": data.length, ...headers },
    }, (response) => {
      const chunks = [];
      response.on("data", (chunk) => chunks.push(chunk));
      response.on("end", () => {
        try {
          resolve({ statusCode: response.statusCode, body: JSON.parse(Buffer.concat(chunks).toString("utf8")) });
        } catch (error) {
          reject(error);
        }
      });
    });
    request.on("error", reject);
    request.end(data);
  });
}

class RawWebSocketClient {
  constructor(host, port, path) {
    this.host = host;
    this.port = port;
    this.path = path;
    this.pending = Buffer.alloc(0);
    this.messages = [];
    this.waiters = [];
  }

  connect() {
    return new Promise((resolve, reject) => {
      const key = crypto.randomBytes(16).toString("base64");
      this.socket = net.createConnection({ host: this.host, port: this.port }, () => {
        this.socket.write([
          `GET ${this.path} HTTP/1.1`,
          `Host: ${this.host}:${this.port}`,
          "Upgrade: websocket",
          "Connection: Upgrade",
          `Sec-WebSocket-Key: ${key}`,
          "Sec-WebSocket-Version: 13",
          "",
          "",
        ].join("\r\n"));
      });
      let handshake = Buffer.alloc(0);
      this.socket.on("data", (chunk) => {
        if (handshake !== null) {
          handshake = Buffer.concat([handshake, chunk]);
          const marker = handshake.indexOf("\r\n\r\n");
          if (marker === -1) return;
          assert(handshake.subarray(0, marker).toString("utf8").includes("101 Switching Protocols"), "websocket handshake failed");
          this.handleData(handshake.subarray(marker + 4));
          handshake = null;
          resolve();
          return;
        }
        this.handleData(chunk);
      });
      this.socket.on("error", reject);
    });
  }

  handleData(chunk) {
    this.pending = Buffer.concat([this.pending, chunk]);
    for (;;) {
      const frame = decodeFrame(this.pending);
      if (!frame) return;
      this.pending = this.pending.subarray(frame.consumed);
      const message = JSON.parse(frame.text);
      const waiter = this.waiters.shift();
      if (waiter) waiter(message);
      else this.messages.push(message);
    }
  }

  receive() {
    if (this.messages.length > 0) return Promise.resolve(this.messages.shift());
    return new Promise((resolve) => this.waiters.push(resolve));
  }

  send(value) {
    this.socket.write(encodeFrame(JSON.stringify(value), { masked: true }));
  }

  close() {
    if (this.socket) this.socket.end();
  }
}

async function call(client, message) {
  client.send(message);
  const response = await client.receive();
  assert(response.id === message.id, `response id mismatch for ${message.type}`);
  assert(response.success === true, `${message.type} failed`);
  return response.result;
}

module.exports = {
  RawWebSocketClient,
  assert,
  call,
  contractFixture,
  decodeFrame,
  encodeFrame,
  postJSON,
  resultForMessage,
  startContractServer,
};

if (require.main === module) {
  startContractServer()
    .then(({ baseURL }) => {
      console.log(`DJConnect ESP32 HA contract fixture listening at ${baseURL}`);
      console.log("Press Ctrl-C to stop.");
    })
    .catch((error) => {
      console.error(`DJConnect ESP32 HA contract fixture failed: ${error.message}`);
      process.exit(1);
    });
}
