#!/usr/bin/env node
"use strict";

const { spawnSync } = require("child_process");
const { contractFixture } = require("./ha_contract_fixture");

const scripts = ["Tools/http_e2e_contract.js", "Tools/websocket_e2e_contract.js"];
const forbidden = [
  contractFixture.deviceToken,
  contractFixture.webSocketToken,
  "authorization:",
  "Bearer ",
  "home_assistant_token",
  "access_token",
  "RIFF....WAVE",
];

for (const script of scripts) {
  const result = spawnSync(process.execPath, [script], { encoding: "utf8" });
  const output = `${result.stdout || ""}${result.stderr || ""}`;
  if (result.status !== 0) {
    process.stdout.write(result.stdout || "");
    process.stderr.write(result.stderr || "");
    process.exit(result.status || 1);
  }
  for (const value of forbidden) {
    if (output.includes(value)) {
      console.error(`Contract redaction validation failed: ${script} printed a forbidden value.`);
      process.exit(1);
    }
  }
}

console.log("ESP32 contract redaction validation passed.");
