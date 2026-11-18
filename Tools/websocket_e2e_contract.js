#!/usr/bin/env node
"use strict";

const { RawWebSocketClient, assert, call, contractFixture, postJSON, startContractServer } = require("./ha_contract_fixture");

function authHeaders() {
  return { authorization: `Bearer ${contractFixture.deviceToken}` };
}

function identity(extra = {}) {
  return {
    device_id: contractFixture.deviceId,
    client_id: contractFixture.clientId,
    device_name: contractFixture.deviceName,
    client_type: contractFixture.clientType,
    device_token: contractFixture.deviceToken,
    ...extra,
  };
}

async function run() {
  const { baseURL, observed, server, sockets } = await startContractServer();
  let client;
  try {
    const session = await postJSON(`${baseURL}/api/djconnect/v1/websocket/session`, {
      ...identity(),
      requested_commands: contractFixture.webSocketRoutes,
    }, authHeaders());
    assert(session.statusCode === 200, "websocket session failed");
    assert(session.body.access_token === contractFixture.webSocketToken, "websocket session token mismatch");

    const url = new URL(baseURL);
    client = new RawWebSocketClient(url.hostname, Number(url.port), "/api/websocket");
    await client.connect();
    const authRequired = await client.receive();
    assert(authRequired.type === "auth_required", "missing auth_required");
    client.send({ type: "auth", access_token: session.body.access_token });
    const authOK = await client.receive();
    assert(authOK.type === "auth_ok", "missing auth_ok");

    let id = 1;
    const message = (type, payload = {}) => ({
      id: id++,
      type,
      ...identity(),
      payload: { ...identity(), ...payload },
    });

    const capabilities = await call(client, message("djconnect/capabilities"));
    assert(capabilities.commands.includes("djconnect/command"), "capabilities missing command route");
    assert(capabilities.fallbacks.voice === "/api/djconnect/v1/voice", "capabilities missing HTTP voice fallback");

    const command = await call(client, message("djconnect/command", { command: "status", payload_type: "command" }));
    assert(command.playback?.track_name === "Contract Track", "websocket command contract failed");

    const status = await call(client, message("djconnect/status", { state: "online" }));
    assert(status.music_backend_available === true, "websocket status contract failed");

    assert(observed.sessionCalls === 1, "expected one websocket session bootstrap");
    console.log(`ESP32 WebSocket contract e2e passed: ${observed.websocketMessages.length} websocket messages.`);
  } finally {
    if (client) client.close();
    for (const socket of sockets) socket.destroy();
    await new Promise((resolve) => server.close(resolve));
  }
}

if (require.main === module) {
  run().catch((error) => {
    console.error(`ESP32 WebSocket contract e2e failed: ${error.message}`);
    process.exit(1);
  });
}
