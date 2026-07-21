// Worker protocol test for peer identity attestation (NETPLAY.md V0/V1):
// a client announces {t:"identity", platform, name, cred?}; the worker
// verifies (FAKE_VERIFY dev flag stands in for the platform backend) and
// broadcasts {t:"identity", role, platform, name, verified} to the PEER,
// replaying the stored copy to a peer that joins after the announce.
//
// Run against `wrangler dev --local --port 8787 --var FAKE_VERIFY:1`.
const BASE = process.env.IDENTITY_TEST_URL || "ws://127.0.0.1:8787/ws";

function connect(qs) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(BASE + qs);
    const inbox = [];
    const waiters = [];
    ws._recv = () => new Promise((res) => {
      if (inbox.length) return res(inbox.shift());
      waiters.push(res);
    });
    // Wait for the next frame of a given type, skipping others (ice/peer/etc).
    ws._recvType = async (t, tries = 10) => {
      for (let i = 0; i < tries; i++) {
        const f = await ws._recv();
        if (f.t === t) return f;
      }
      return null;
    };
    ws.onmessage = (m) => {
      const f = JSON.parse(m.data);
      if (waiters.length) waiters.shift()(f);
      else inbox.push(f);
    };
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error("ws error"));
  });
}

const t = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
function check(name, cond) {
  console.log((cond ? "PASS " : "FAIL ") + name);
  if (!cond) failures++;
}

(async () => {
  // 1. Host a room, announce its identity BEFORE any joiner is present.
  const host = await connect("?role=host");
  const room = await host._recvType("room");
  check("room frame", !!room && !!room.code);
  const code = room.code;
  host.send(JSON.stringify({ t: "identity", platform: 1, name: "GLENN" }));
  await t(200);  // let the worker attest + store it

  // 2. Joiner arrives AFTER the host announced: the stored host identity is
  //    replayed to it (verified, since FAKE_VERIFY attests the claim).
  const join = await connect(`?role=join&code=${code}`);
  const joined = await join._recvType("joined");
  check("joined", !!joined);
  const hostId = await join._recvType("identity");
  check("host identity replayed to late joiner",
        !!hostId && hostId.role === "host" && hostId.name === "GLENN" &&
        hostId.platform === 1 && hostId.verified === true);

  // 3. Joiner announces: the worker broadcasts it live to the host.
  join.send(JSON.stringify({ t: "identity", platform: 2, name: "BOB" }));
  const joinId = await host._recvType("identity");
  check("joiner identity broadcast to host",
        !!joinId && joinId.role === "joiner" && joinId.name === "BOB" &&
        joinId.platform === 2 && joinId.verified === true);

  // 4. A name is capped and a hostile length can't crash the worker: send an
  //    over-long name; it's truncated (<= 24 bytes on the wire cap).
  host.send(JSON.stringify({ t: "identity", platform: 1, name: "X".repeat(200) }));
  const capped = await join._recvType("identity");
  check("over-long name capped to 24", !!capped && capped.name.length <= 24);

  // 5. A joiner that leaves and is replaced does NOT leak the old identity:
  //    a fresh joiner's replay carries only the host's identity, and until
  //    the new joiner announces, the host has nothing stale about it.
  join.close();
  await host._recvType("peer");  // leave
  await t(200);
  const join2 = await connect(`?role=join&code=${code}`);
  await join2._recvType("joined");
  const hostId2 = await join2._recvType("identity");
  check("replay still works for a replacement joiner",
        !!hostId2 && hostId2.role === "host" && hostId2.verified === true);

  host.close(); join2.close();
  console.log(failures ? `\n${failures} FAILURES` : "\nIDENTITY-TEST-OK");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error("test crashed:", e); process.exit(1); });
