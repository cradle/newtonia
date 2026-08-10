// PB-D5 family 4 — identity fan-out + per-jid epochs: the host's
// attestation reaches every joiner; a joiner's reaches the host and every
// OTHER joiner stamped {from}; a late joiner is replayed host + all
// sitting joiners; a slow verify resolving after its announcer closed
// still attests to THAT jid (never a replacement — jids are not reused),
// and a failed slow verify attests nothing.
// Needs FAKE_VERIFY=1 (the SLOW:/SLOWFAIL: hooks).
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

(async () => {
  const { host, code } = await host_room();
  host.send(JSON.stringify({ t: "identity", platform: 2, name: "HOSTY", cred: "x" }));
  await t(250);

  const j1 = await join_room(code);
  const id1 = (await host._recvType("peer")).from;
  const hid1 = await j1._recvType("identity");
  check("late joiner replayed the host identity",
        hid1 && hid1.role === "host" && hid1.name === "HOSTY" && hid1.verified === true);

  // j1 attests: host + (later) other joiners hear it, stamped from=id1.
  j1.send(JSON.stringify({ t: "identity", platform: 2, name: "ONE", cred: "x" }));
  const i1 = await host._recvType("identity");
  check("host heard j1's attestation with from",
        i1 && i1.role === "joiner" && i1.name === "ONE" && i1.from === id1 &&
        i1.verified === true);

  const j2 = await join_room(code);
  const id2 = (await host._recvType("peer")).from;
  // Replay to the newcomer: host first, then sitting joiner j1.
  const rh = await j2._recvType("identity");
  const r1 = await j2._recvType("identity");
  check("newcomer replayed host + sitting joiner",
        rh && rh.role === "host" && r1 && r1.role === "joiner" &&
        r1.name === "ONE" && r1.from === id1);

  // Live fan-out: j2's attestation reaches host AND j1 (not j2 itself).
  j2.send(JSON.stringify({ t: "identity", platform: 2, name: "TWO", cred: "x" }));
  const h2 = await host._recvType("identity");
  const x2 = await j1._recvType("identity");
  check("j2 attestation fanned out with from",
        h2 && h2.from === id2 && x2 && x2.from === id2 && x2.name === "TWO");
  await t(200);
  check("announcer did not hear its own identity",
        !j2._drain().some((f) => f && f.t === "identity"));

  // Per-jid late verify: j3 announces with a slow verify then closes; the
  // resolved attestation still lands, named j3's id.
  const j3 = await join_room(code);
  const id3 = (await host._recvType("peer")).from;
  j3.send(JSON.stringify({ t: "identity", platform: 2, name: "THREE", cred: "SLOW:800" }));
  await t(150);
  j3.close();
  await host._recvType("peer");  // the leave
  const late = await host._recvType("identity", 20);
  check("slow verify resolved after close attests to its own jid",
        late && late.from === id3 && late.name === "THREE" && late.verified === true);

  // SLOWFAIL: a failed late verify attests nothing.
  const j4 = await join_room(code);
  const id4 = (await host._recvType("peer")).from;
  j4.send(JSON.stringify({ t: "identity", platform: 2, name: "FOUR", cred: "SLOWFAIL:600" }));
  await t(150);
  j4.close();
  await host._recvType("peer");  // the leave
  await t(900);
  check("failed slow verify attested nothing",
        !host._drain().some((f) => f && f.t === "identity" && f.from === id4));

  host.close(); j1.close(); j2.close();
  finish("IDENTITY-FANOUT-TEST-OK");
})();
