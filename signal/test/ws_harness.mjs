// Shared harness for the wrangler-dev protocol tests (PB-D5 and later).
// The pre-multi-join tests (reclaim_test.js, identity_test.js) carry their
// own inlined copy of this pattern; new families import from here.
//
// Run against `wrangler dev --local --port 8787 --var FAKE_VERIFY:1`
// (deploy-signal.yml's "Protocol tests" step). Requires Node 22 for the
// global WebSocket client.

export const BASE = process.env.SIGNAL_TEST_URL || "ws://127.0.0.1:8787/ws";

// Opens a socket with an inbox + waiter queue. `ws._recv()` pops the next
// frame; `ws._recvType(t, tries)` pops the next frame of type t, skipping
// others (ice/peer/etc — with several joiners far more unrelated frames
// interleave than the 2P tests ever saw).
export function connect(qs) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(BASE + qs);
    const inbox = [];
    const waiters = [];
    ws._recv = () => new Promise((res) => {
      if (inbox.length) return res(inbox.shift());
      waiters.push(res);
    });
    ws._recvType = async (t, tries = 12) => {
      for (let i = 0; i < tries; i++) {
        const f = await ws._recv();
        if (f && f.t === t) return f;
      }
      return null;
    };
    // Drain everything already queued (no waiting) — for "nothing else
    // arrived" assertions after a settle sleep.
    ws._drain = () => inbox.splice(0, inbox.length);
    ws.onmessage = (m) => {
      let f = null;
      try { f = JSON.parse(m.data); } catch (e) {}
      if (waiters.length) waiters.shift()(f);
      else inbox.push(f);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error("ws error"));
  });
}

export const t = (ms) => new Promise((r) => setTimeout(r, ms));

let failures = 0;
export function check(name, cond) {
  console.log((cond ? "PASS " : "FAIL ") + name);
  if (!cond) failures++;
}

// Prints the sentinel and exits — call last, with the family's OK marker.
export function finish(sentinel) {
  if (failures) {
    console.log(`${failures} FAILURES`);
    process.exit(1);
  }
  console.log(sentinel);
  process.exit(0);
}

// Convenience: host a fresh room, returning {host, code}.
export async function host_room() {
  const host = await connect("?role=host");
  const room = await host._recvType("room");
  if (!room || !room.code) {
    console.log("FAIL could not host a room");
    process.exit(1);
  }
  return { host, code: room.code };
}

// Convenience: join and wait for the joined frame; returns the socket.
// Callers asserting a REFUSAL should use connect() + _recvType("err").
export async function join_room(code) {
  const ws = await connect("?role=join&code=" + code);
  const j = await ws._recvType("joined");
  if (!j) {
    console.log("FAIL join was not accepted for " + code);
    process.exit(1);
  }
  return ws;
}
