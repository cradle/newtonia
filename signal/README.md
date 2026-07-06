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

## Cost / abuse notes

The paid resources are the Worker/DO invocations and TURN relay bandwidth
(Cloudflare Calls). Defenses in `worker.js`:

- **Per-IP rate limits** (`Limiter` DO, fixed 10-min window): 10 host /
  30 join attempts. Keyed on the IP collapsed to its **/64** for IPv6
  (`rate_key`) so a v6 user can't rotate host bits to bypass the cap.
- **TURN credentials** are minted only on a rate-limited host attempt, or
  a join to an actually-open room, with a **15-minute TTL** — long enough
  for ICE setup, short enough that a harvested credential is nearly
  worthless as free relay bandwidth.
- **Relay content is length-capped** (SDP 16 KB, candidate 512 B, ≤32
  buffered) so a peer can't pin unbounded DO memory.
- **WebSocket Hibernation API**: after the handshake the signaling socket
  is idle for the whole game session (traffic is peer-to-peer), so the DO
  evicts between messages and stops billing wall-clock for held sockets.
  Room state lives in DO storage; sockets recover by tag. A storage alarm
  frees abandoned rooms after the grace window.

There is **no hard monthly spend cap** on paid Workers — the backstops are:
`[limits] cpu_ms` in wrangler.toml (set, 50 ms/invocation), **Billing →
Notifications** usage alerts in the dashboard, and the manual kill
switches: disable the worker (the game degrades to the manual clipboard
code flow) and delete the Calls TURN key (revokes all outstanding TURN
credentials instantly).

## Tests

- `node test/reclaim_test.js` — protocol test (needs `wrangler dev` on :8787).
- `node test/rate_key_test.mjs` — `rate_key` /64-collapse unit test.
