#!/usr/bin/env python3
"""Get a Spotify refresh token using Authorization Code with PKCE."""

import argparse
import base64
import hashlib
import http.server
import json
import os
import secrets
import socketserver
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
import webbrowser


SCOPES = [
    "user-read-playback-state",
    "user-read-currently-playing",
    "user-modify-playback-state",
    "playlist-read-private",
]

DEFAULT_CA_FILES = [
    "/etc/ssl/cert.pem",
    "/opt/homebrew/etc/ca-certificates/cert.pem",
    "/opt/homebrew/etc/openssl@3/cert.pem",
    "/usr/local/etc/openssl@3/cert.pem",
]


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        self.server.auth_code = query.get("code", [None])[0]
        self.server.auth_error = query.get("error", [None])[0]
        self.server.auth_state = query.get("state", [None])[0]

        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(
            b"<html><body><h1>Spotify authorized</h1>"
            b"<p>You can close this browser tab and return to the terminal.</p>"
            b"</body></html>"
        )

    def log_message(self, *_args):
        return


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


def b64url(data):
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def build_authorize_url(client_id, redirect_uri, state, code_challenge):
    params = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": redirect_uri,
        "scope": " ".join(SCOPES),
        "state": state,
        "code_challenge_method": "S256",
        "code_challenge": code_challenge,
        "show_dialog": "true",
    }
    return "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode(params)


def create_ssl_context(ca_file):
    if ca_file:
        return ssl.create_default_context(cafile=ca_file)

    for candidate in DEFAULT_CA_FILES:
        if os.path.exists(candidate):
            return ssl.create_default_context(cafile=candidate)

    return ssl.create_default_context()


def exchange_code(client_id, redirect_uri, code, verifier, ca_file):
    body = urllib.parse.urlencode(
        {
            "client_id": client_id,
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": redirect_uri,
            "code_verifier": verifier,
        }
    ).encode("utf-8")
    request = urllib.request.Request(
        "https://accounts.spotify.com/api/token",
        data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )

    try:
        context = create_ssl_context(ca_file)
        with urllib.request.urlopen(request, timeout=30, context=context) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")
        raise SystemExit(f"Token exchange failed: HTTP {error.code}\n{detail}") from error
    except urllib.error.URLError as error:
        raise SystemExit(
            "Token exchange failed while connecting to Spotify.\n"
            f"{error}\n\n"
            "If this is a certificate error, try:\n"
            "  python3 scripts/spotify_pkce_refresh_token.py "
            f"--client-id {client_id} --ca-file /etc/ssl/cert.pem"
        ) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client-id", required=True, help="Spotify app client ID")
    parser.add_argument("--host", default="127.0.0.1", help="Callback host")
    parser.add_argument("--port", default=8888, type=int, help="Callback port")
    parser.add_argument("--ca-file", help="Optional CA bundle for HTTPS token exchange")
    args = parser.parse_args()

    redirect_uri = f"http://{args.host}:{args.port}/callback"
    verifier = b64url(secrets.token_bytes(64))
    challenge = b64url(hashlib.sha256(verifier.encode("ascii")).digest())
    state = b64url(secrets.token_bytes(16))
    authorize_url = build_authorize_url(args.client_id, redirect_uri, state, challenge)

    print("Add this redirect URI to your Spotify app first:")
    print(f"  {redirect_uri}")
    print()
    print("Opening Spotify authorization URL...")
    print(authorize_url)
    print()

    with ReusableTCPServer((args.host, args.port), CallbackHandler) as server:
        server.auth_code = None
        server.auth_error = None
        server.auth_state = None
        webbrowser.open(authorize_url)
        server.handle_request()

        if server.auth_error:
            raise SystemExit(f"Authorization failed: {server.auth_error}")
        if server.auth_state != state:
            raise SystemExit("Authorization failed: state did not match")
        if not server.auth_code:
            raise SystemExit("Authorization failed: no code received")

        token = exchange_code(
            args.client_id,
            redirect_uri,
            server.auth_code,
            verifier,
            args.ca_file,
        )

    refresh_token = token.get("refresh_token")
    if not refresh_token:
        raise SystemExit("No refresh_token returned. Check your Spotify app and scopes.")

    print("Provision these through the setup portal or Home Assistant pairing flow:")
    print()
    print(f"Spotify client ID: {args.client_id}")
    print(f"Spotify refresh token: {refresh_token}")
    print()
    print("Do not commit these values or compile them into firmware headers.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit("Canceled")
