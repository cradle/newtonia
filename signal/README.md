# Newtonia signaling worker

Cloudflare Worker + Durable Objects that mints 4-letter room codes and
relays the WebRTC offer/answer between host and joiner. Protocol and
milestone context: `../NETPLAY.md` (Milestone 2).

## Local development / e2e

No Cloudflare account needed:

```sh
cd signal
npx wrangler dev --local --port 8787
```

The game connects to `ws://127.0.0.1:8787/ws` when the `signal_url`
preference (or `NEWTONIA_SIGNAL_URL` env var) points there.

## Deploy (production)

```sh
cd signal
npx wrangler login     # once
npx wrangler deploy
```

Note the `*.workers.dev` URL it prints and update `NEWTONIA_SIGNAL_URL_DEFAULT`
in `net_signal.h`.

## Endpoints

- `GET /ws?role=host` — WebSocket; server replies `{t:"room",code:"ABCD"}`
- `GET /ws?role=join&code=ABCD` — WebSocket; `{t:"joined"}` or `{t:"err",...}`
- Host sends `{t:"offer",sdp}`; joiner sends `{t:"answer",sdp}`; the room
  relays each to the other side and replays the offer to late (re)joiners.
