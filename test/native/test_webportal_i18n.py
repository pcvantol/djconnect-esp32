#!/usr/bin/env python3
"""Offline sanity checks for the embedded web portal translation table."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WEB_PORTAL = ROOT / "src" / "WebPortal.cpp"
SUPPORTED_LANGUAGES = ["en", "nl", "de", "fr", "es"]


def fail(message: str) -> None:
    print(f"webportal i18n: {message}", file=sys.stderr)
    raise SystemExit(1)


def extract_object_keys(source: str, language: str) -> set[str]:
    match = re.search(rf"\b{language}:\s*\{{(?P<body>.*?)\n\}}", source, re.S)
    if not match:
        fail(f"missing translation object for {language}")
    body = match.group("body")
    keys: set[str] = set()
    index = 0
    in_string = False
    escaped = False
    while index < len(body):
        char = body[index]
        if escaped:
            escaped = False
            index += 1
            continue
        if char == "\\" and in_string:
            escaped = True
            index += 1
            continue
        if char == '"':
            in_string = not in_string
            index += 1
            continue
        if not in_string and (char.isalpha() or char == "_"):
            start = index
            index += 1
            while index < len(body) and (body[index].isalnum() or body[index] == "_"):
                index += 1
            key = body[start:index]
            probe = index
            while probe < len(body) and body[probe].isspace():
                probe += 1
            if probe < len(body) and body[probe] == ":":
                keys.add(key)
            continue
        index += 1
    return keys


def main() -> None:
    source = WEB_PORTAL.read_text()

    option_codes = re.findall(r'<option value="([a-z]{2})" data-i18n="language[A-Z][A-Za-z]+">', source)
    if option_codes != SUPPORTED_LANGUAGES:
        fail(f"language select options are {option_codes}, expected {SUPPORTED_LANGUAGES}")

    if '["de","fr","es"].forEach' not in source:
        fail("de/fr/es must be merged over English defaults to keep the portal table compact")

    english_keys = extract_object_keys(source, "en")
    if len(english_keys) < 100:
        fail("English translation table looks unexpectedly small")

    for language in SUPPORTED_LANGUAGES:
        keys = extract_object_keys(source, language)
        unknown = keys - english_keys
        if unknown:
            fail(f"{language} defines unknown keys: {sorted(unknown)}")
        if language in {"en", "nl"}:
            missing = english_keys - keys
            if missing:
                fail(f"{language} is missing keys: {sorted(missing)}")
        else:
            required_overrides = {
                "deviceNotPaired",
                "nowPlaying",
                "language",
                "languageEnglish",
                "languageDutch",
                "languageGerman",
                "languageFrench",
                "languageSpanish",
                "settings",
                "wifi",
                "pairing",
                "playback",
                "diagnostics",
                "restart",
                "factoryReset",
            }
            missing_required = required_overrides - keys
            if missing_required:
                fail(f"{language} is missing required overrides: {sorted(missing_required)}")

    print("webportal i18n ok")


if __name__ == "__main__":
    main()
