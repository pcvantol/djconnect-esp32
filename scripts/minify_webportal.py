#!/usr/bin/env python3
"""Minify the embedded WebPortal IndexHtml raw literal in src/WebPortal.cpp.

The firmware serves a self-contained HTML/CSS/JS portal from PROGMEM. This
script keeps the source structure intact while removing indentation-heavy
whitespace from the embedded asset. JavaScript newlines are preserved to avoid
changing automatic-semicolon-insertion behavior.
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WEB_PORTAL = ROOT / "src" / "WebPortal.cpp"
START = 'static const char IndexHtml[] PROGMEM = R"rawliteral('
END = ')rawliteral";'


def minify_css(css: str) -> str:
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    css = re.sub(r"\s+", " ", css)
    css = re.sub(r"\s*([{}:;,>+~])\s*", r"\1", css)
    css = css.replace(";}", "}")
    return css.strip()


def minify_js(js: str) -> str:
    lines: list[str] = []
    for line in js.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        lines.append(stripped)
    return "\n".join(lines)


def minify_html(html: str) -> str:
    def replace_style(match: re.Match[str]) -> str:
        return f"<style>{minify_css(match.group(1))}</style>"

    def replace_script(match: re.Match[str]) -> str:
        return f"<script>\n{minify_js(match.group(1))}\n</script>"

    html = re.sub(r"<style>\s*(.*?)\s*</style>", replace_style, html, flags=re.S)
    html = re.sub(r"<script>\s*(.*?)\s*</script>", replace_script, html, flags=re.S)
    lines = [line.strip() for line in html.splitlines()]
    html = "\n".join(line for line in lines if line)
    html = re.sub(r">\s+<", "><", html)
    return html.strip() + "\n"


def main() -> None:
    source = WEB_PORTAL.read_text()
    start = source.find(START)
    if start < 0:
        raise SystemExit(f"start marker not found in {WEB_PORTAL}")
    content_start = start + len(START)
    end = source.find(END, content_start)
    if end < 0:
        raise SystemExit(f"end marker not found in {WEB_PORTAL}")
    html = source[content_start:end]
    minified = minify_html(html)
    WEB_PORTAL.write_text(source[:content_start] + "\n" + minified + source[end:])


if __name__ == "__main__":
    main()
