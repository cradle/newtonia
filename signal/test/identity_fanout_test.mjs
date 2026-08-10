// PB-D5 family 4 — identity fan-out + per-jid late verifies. B3 scope:
// the host's attestation reaches every joiner; a joiner's reaches the
// HOST ONLY, stamped {from} — joiner badges are deliberately NOT fanned
// or replayed to other joiners yet, because today's 2P clients apply
// identity frames role-blind and another joiner's badge (including a
// rejoiner's OWN, replayed) would overwrite the host's on their screen;
// B4's roster consumer re-adds that leg. A slow verify resolving after
// its announcer closed still attests to THAT jid (never a replacement —
// jids are not reused), and a failed slow verify attests nothing.
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

  // j1 attests: the host hears it, stamped from=id1.
  j1.send(JSON.stringify({ t: "identity", platform: 2, name: "ONE", cred: "x" }));
  const i1 = await host._recvType("identity");
  check("host heard j1's attestation with from",
        i1 && i1.role === "joiner" && i1.name === "ONE" && i1.from === id1 &&
        i1.verified === true);

  const j2 = await join_room(code);
  const id2 = (await host._recvType("peer")).from;
  // Replay to the newcomer: the HOST's identity only — never another
  // joiner's (2P clients are role-blind; see the header comment).
  const rh = await j2._recvType("identity");
  check("newcomer replayed the host identity", rh && rh.role === "host");
  await t(250);
  check("newcomer was NOT replayed another joiner's badge",
        !j2._drain().some((f) => f && f.t === "identity"));

  // Live fan-out: j2's attestation reaches the host, and ONLY the host.
  j2.send(JSON.stringify({ t: "identity", platform: 2, name: "TWO", cred: "x" }));
  const h2 = await host._recvType("identity");
  check("host heard j2's attestation with from",
        h2 && h2.from === id2 && h2.name === "TWO");
  await t(250);
  check("other joiners did not hear it",
        !j1._drain().some((f) => f && f.t === "identity"));
  check("announcer did not hear its own identity",
        !j2._drain().some((f) => f && f.t === "identity"));

  // Per-jid late verify: j3 announces with a slow verify then closes; the
  // resolved attestation still lands on the host, named j3's id.
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
