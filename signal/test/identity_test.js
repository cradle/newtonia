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

  // Robustness: valid-JSON but non-object frames must not crash the message
  // handler (JSON.parse("null") -> null, then null.t would throw). If any of
  // these tore the socket down, the identity flow below would fail.
  host.send("null"); host.send("5"); host.send('"x"'); host.send("[]");
  await t(100);

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

  // 5. A joiner that leaves and is replaced does NOT leak the old identity
  //    ONTO the replacement: the fresh joiner replays the host's identity
  //    first (any departed jid's badge replays under that jid's own id —
  //    per-jid since PB-D5 — never as the newcomer's), and until the new
  //    joiner announces, the host has nothing stale about IT.
  join.close();
  await host._recvType("peer");  // leave
  await t(200);
  const join2 = await connect(`?role=join&code=${code}`);
  await join2._recvType("joined");
  const hostId2 = await join2._recvType("identity");
  check("replay still works for a replacement joiner",
        !!hostId2 && hostId2.role === "host" && hostId2.verified === true);

  // 6. Late verify vs socket close — the vacant-slot race (field: Steam
  //    joiner vs Android host, 2026-08-07). A joiner announces with a SLOW
  //    credential (the FAKE_VERIFY slow-verify hook: full async machinery,
  //    injected delay) and disconnects before the verify resolves — the
  //    fast-ICE-connect handoff pattern. The attestation must still reach
  //    the host: the player is mid-game over WebRTC, only the signaling
  //    socket closed.
  join2.send(JSON.stringify({ t: "identity", platform: 5, name: "DROID",
                              cred: "SLOW:800" }));
  await t(100);   // let the announce start its (slow) verify
  join2.close();  // signaling socket gone while the verify is in flight
  const late = await host._recvType("identity");
  check("late verify reaches host after joiner socket closed",
        !!late && late.role === "joiner" && late.name === "DROID" &&
        late.platform === 5 && late.verified === true);

  // 7. A replacement joiner does NOT inherit the late-stored badge:
  //    accept_joiner clears the stored identity, so the replacement's own
  //    FAILED verify lands as its unverified claim — the never-demote guard
  //    must not re-push the departed player's verified name onto it.
  const join3 = await connect(`?role=join&code=${code}`);
  await join3._recvType("joined");
  await join3._recvType("identity");  // the host's identity, replayed
  join3.send(JSON.stringify({ t: "identity", platform: 3, name: "EVE",
                              cred: "SLOWFAIL:0" }));
  const claim = await host._recvType("identity");
  check("replacement joiner not pinned to the departed verified badge",
        !!claim && claim.role === "joiner" && claim.name === "EVE" &&
        claim.platform === 3 && claim.verified === false);

  // 8. Two departed announcers' slow verifies resolving OUT OF ORDER —
  //    the last-resolver-wins overwrite hazard this case used to guard
  //    is structurally gone under per-jid identity (PB-D5): jids are
  //    never reused, so each late verify lands under its OWN id and
  //    neither can clobber the other. Assert exactly that: B's faster
  //    verify lands first, A's slower one still lands later under a
  //    DIFFERENT id, and a fresh claim afterwards is undisturbed.
  join3.close();
  await host._recvType("peer");  // leave
  await t(200);                  // let the drop settle before the next join
  const joinA = await connect(`?role=join&code=${code}`);
  await joinA._recvType("joined");
  joinA.send(JSON.stringify({ t: "identity", platform: 5, name: "OLDA",
                              cred: "SLOW:2500" }));
  await t(100); joinA.close();
  await t(300);
  const joinB = await connect(`?role=join&code=${code}`);
  await joinB._recvType("joined");
  joinB.send(JSON.stringify({ t: "identity", platform: 5, name: "NEWB",
                              cred: "SLOW:300" }));
  await t(100); joinB.close();
  const lateB = await host._recvType("identity");
  check("newest departed announcer's late verify lands",
        !!lateB && lateB.role === "joiner" && lateB.name === "NEWB" &&
        lateB.verified === true);
  const lateA = await host._recvType("identity", 20);  // resolves ~2.5 s in
  check("older announcer's late verify lands under its OWN id",
        !!lateA && lateA.role === "joiner" && lateA.name === "OLDA" &&
        lateA.verified === true && lateA.from !== lateB.from);
  const joinC = await connect(`?role=join&code=${code}`);
  await joinC._recvType("joined");
  await joinC._recvType("identity");  // the host's identity, replayed
  joinC.send(JSON.stringify({ t: "identity", platform: 1, name: "CARL" }));
  const afterStale = await host._recvType("identity");
  check("a fresh claim lands undisturbed after the late verifies",
        !!afterStale && afterStale.role === "joiner" &&
        afterStale.name === "CARL");

  host.close(); joinC.close();
  console.log(failures ? `\n${failures} FAILURES` : "\nIDENTITY-TEST-OK");
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error("test crashed:", e); process.exit(1); });
