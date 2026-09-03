# TEST-ONLY Opaque Browser Transport Gateway

**This tool is NOT production browser transport support.**

It is a labelled, allowlist-only byte forwarder for local/dev testing of the
Emscripten browser HTTP/WS tunnel contract described in
`docs/emscripten_browser_transport.md` §7–§8.

Production deployments require first-party Aether endpoints (or a hardened
gateway operated by Aethernet). Do not expose this process to the public
internet without additional hardening.

## What it does

- Frontends: HTTP, HTTPS, WS, WSS
- Opaque forwarder to **allowlisted** Aether TCP destinations (not an open proxy)
- Never decrypts Aether traffic; never logs payloads or secrets
- Unpredictable session IDs, idle expiry, body/queue size bounds, concurrent receive bounds
- CORS origin allowlist; WebSocket `Origin` validation

## Endpoints (v1)

| Method | Path | Body |
|--------|------|------|
| `POST` | `/aether/v1/connect` | optional JSON `{"target":"<id>"}` → `{"session":"<opaque>"}` |
| `POST` | `/aether/v1/session/{id}/send` | `application/octet-stream` |
| `GET`  | `/aether/v1/session/{id}/receive` | long-poll; `application/octet-stream` |
| `POST` | `/aether/v1/session/{id}/close` | empty |
| `WS`   | `/aether/v1/ws?target=<id>` | binary frames only |

## Install

```bash
python -m venv .venv
# Windows: .venv\Scripts\activate
pip install -r tools/browser_transport_gateway/requirements.txt
```

## Run

```bash
python tools/browser_transport_gateway/gateway.py \
  --listen 127.0.0.1:18080 \
  --ws-listen 127.0.0.1:18081 \
  --origin http://127.0.0.1:8080 \
  --origin http://localhost:8080 \
  --target local=127.0.0.1:9010
```

HTTP(S) listens on `--listen`. WS(S) listens on `--ws-listen` (default: same host, HTTP port + 1).

HTTPS / WSS (optional; applies to both listeners when set):

```bash
python tools/browser_transport_gateway/gateway.py \
  --listen 127.0.0.1:18443 \
  --ws-listen 127.0.0.1:18444 \
  --tls-cert path/to/cert.pem \
  --tls-key path/to/key.pem \
  --origin https://127.0.0.1:8080 \
  --target local=127.0.0.1:9010
```

Environment variables (override defaults; CLI wins when both set):

| Variable | Meaning |
|----------|---------|
| `AE_GW_LISTEN` | HTTP(S) `host:port` |
| `AE_GW_WS_LISTEN` | WS(S) `host:port` |
| `AE_GW_ORIGINS` | comma-separated origin allowlist |
| `AE_GW_TARGETS` | comma-separated `name=host:port` |
| `AE_GW_TLS_CERT` / `AE_GW_TLS_KEY` | PEM paths |
| `AE_GW_IDLE_SECONDS` | session idle expiry (default 60) |
| `AE_GW_MAX_BODY` | max request body bytes (default 65536) |
| `AE_GW_MAX_QUEUE` | max queued inbound bytes per session (default 262144) |
| `AE_GW_MAX_RECEIVES` | max concurrent long-polls per session (default 2) |

## Tests

```bash
pip install -r tools/browser_transport_gateway/requirements.txt
python tools/browser_transport_gateway/test_gateway.py
```

Covers: invalid origin, invalid destination, oversized body, unknown session,
binary preservation (`0x00`/`0xff`), CORS preflight.
