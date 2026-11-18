#!/usr/bin/env python3
"""Offline sanity checks for committed Postman collections.

These tests intentionally do not contact a real DJConnect device. They catch
broken collection JSON, malformed request bodies, missing auth placeholders on
protected device routes and accidentally committed secrets before release.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


ROOT = Path(__file__).resolve().parents[2]
COLLECTIONS = sorted((ROOT / "postman").glob("*.postman_collection.json"))
POSTMAN_SCHEMA = "https://schema.getpostman.com/json/collection/v2.1.0/collection.json"
PLACEHOLDER_RE = re.compile(r"\{\{[A-Za-z0-9_]+\}\}")
URL_RE = re.compile(r"https?://[^\"{}\s]+")
SECRET_VALUE_RE = re.compile(
    r"(?i)(?:"
    r"sk-[A-Za-z0-9_-]{16,}|"
    r"gh[pousr]_[A-Za-z0-9_]{20,}|"
    r"eyJ[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}|"
    r"(?:password|passwd|client_secret|refresh_token|device_token|wifi_password)"
    r'"\s*:\s*"(?!\{\{|\}|$)[^"]{8,}"'
    r")"
)


def fail(message: str) -> None:
    print(f"postman collection check failed: {message}", file=sys.stderr)
    sys.exit(1)


def iter_items(items: list[dict[str, Any]], prefix: str = ""):
    for item in items:
        name = item.get("name", "<unnamed>")
        path = f"{prefix}/{name}" if prefix else name
        if "item" in item:
            yield from iter_items(item["item"], path)
        else:
            yield path, item


def request_url(request: dict[str, Any]) -> str:
    url = request.get("url")
    if isinstance(url, str):
        return url
    if isinstance(url, dict):
        raw = url.get("raw")
        if isinstance(raw, str):
            return raw
    return ""


def collect_variables(collection: dict[str, Any]) -> dict[str, str]:
    variables: dict[str, str] = {}
    for variable in collection.get("variable", []):
        key = variable.get("key")
        value = variable.get("value", "")
        if not isinstance(key, str):
            fail("collection variable without string key")
        if not isinstance(value, str):
            fail(f"collection variable {key} must use a string value")
        variables[key] = value
    return variables


def validate_raw_json_body(collection_name: str, item_path: str, raw: str) -> None:
    substituted = PLACEHOLDER_RE.sub("placeholder", raw)
    try:
        json.loads(substituted)
    except json.JSONDecodeError as exc:
        fail(f"{collection_name} / {item_path} has invalid raw JSON body: {exc}")


def auth_uses_device_token(auth: dict[str, Any]) -> bool:
    if auth.get("type") != "bearer":
        return False
    for entry in auth.get("bearer", []):
        if entry.get("key") == "token" and entry.get("value") == "{{device_token}}":
            return True
    return False


def validate_collection(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    if SECRET_VALUE_RE.search(text):
        fail(f"{path.relative_to(ROOT)} appears to contain a committed secret")
    for match in URL_RE.finditer(text):
        url = match.group(0)
        parsed = urlparse(url)
        host = parsed.hostname or ""
        is_documented_placeholder = "<" in url or ">" in url or "XXXXXXXXXXXX" in url
        is_allowed_hint = host == "homeassistant.local" and parsed.port in {None, 8123}
        if host.endswith(".ui.nabu.casa") or (host.endswith(".local") and not is_documented_placeholder and not is_allowed_hint):
            fail(f"{path.relative_to(ROOT)} contains a private/local URL: {url}")

    try:
        collection = json.loads(text)
    except json.JSONDecodeError as exc:
        fail(f"{path.relative_to(ROOT)} is not valid JSON: {exc}")

    info = collection.get("info")
    if not isinstance(info, dict):
        fail(f"{path.relative_to(ROOT)} is missing info")
    if info.get("schema") != POSTMAN_SCHEMA:
        fail(f"{path.relative_to(ROOT)} must use Postman collection schema v2.1")
    name = info.get("name")
    if not isinstance(name, str) or not name.startswith("DJConnect "):
        fail(f"{path.relative_to(ROOT)} has an unexpected collection name")

    variables = collect_variables(collection)
    if variables.get("device_token", None) != "":
        fail(f"{path.relative_to(ROOT)} must keep device_token empty")
    if "base_url" not in variables:
        fail(f"{path.relative_to(ROOT)} must define base_url")

    leaf_count = 0
    for item_path, item in iter_items(collection.get("item", [])):
        leaf_count += 1
        request = item.get("request")
        if not isinstance(request, dict):
            fail(f"{name} / {item_path} is missing a request object")

        method = request.get("method")
        if method not in {"GET", "POST"}:
            fail(f"{name} / {item_path} has unsupported method {method!r}")

        url = request_url(request)
        if not url:
            fail(f"{name} / {item_path} is missing request.url")
        if "{{base_url}}" not in url:
            fail(f"{name} / {item_path} must use {{base_url}}")

        body = request.get("body")
        if body and body.get("mode") == "raw":
            raw = body.get("raw")
            if not isinstance(raw, str):
                fail(f"{name} / {item_path} raw body must be a string")
            if any(header.get("key", "").lower() == "content-type" and header.get("value") == "application/json" for header in request.get("header", [])):
                validate_raw_json_body(name, item_path, raw)

        is_open_device_route = url.endswith("/api/device/info") or url.endswith("/api/device/pairing-info")
        is_pair_callback = url.endswith("/api/device/pair")
        is_protected_device_route = "/api/device/" in url and not is_open_device_route and not is_pair_callback
        if is_protected_device_route and not auth_uses_device_token(request.get("auth", {})):
            fail(f"{name} / {item_path} must use bearer {{device_token}} auth")

    if leaf_count == 0:
        fail(f"{path.relative_to(ROOT)} has no executable requests")

    print(f"ok {path.relative_to(ROOT)} ({leaf_count} requests)")


def main() -> int:
    if not COLLECTIONS:
        fail("no Postman collections found")
    for collection in COLLECTIONS:
        validate_collection(collection)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
