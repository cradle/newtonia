// Joins a room over the relay as a named pilot and then does NOTHING —
// no answer, no transport, no game. Companion to nseat_rejoin_flap_swap.sh
// (and a sibling of fake_old_host.mjs, which fakes the other end).
//
// It exists because the thing under test needs only two worker events —
// the PeerJoin the host files, and the identity frame that names the jid —
// and getting those from a real instance means booting a game, waiting for
// its window, and walking the menu with xdotool. That took ~34 s on a
// loaded CI runner, which blew the window it had to land inside (the
// adoption's ICE hold), and the driver failed on timing rather than on
// anything it was asserting. This lands in under a second.
//
// Deliberately never answers the offer: the in-game rejoin door's offer is
// unaddressed and goes to the oldest connected joiner, so a real instance
// parked in the room can eat the offer meant for the next rejoiner. One
// that cannot answer cannot squat. Kill it when done regardless — an idle
// socket is still the oldest joiner.
//
// Usage: node fake_joiner.mjs <CODE> <NAME>; prints "JOINED" once seated
// in the room, "FAILED <reason>" if the relay refuses. SIGNAL_WS overrides
// the relay (default local).
const BASE = process.env.SIGNAL_WS || "ws://127.0.0.1:8787/ws";
const [code, name] = process.argv.slice(2);
if (!code || !name) {
  console.log("usage: fake_joiner.mjs <CODE> <NAME>");
  process.exit(2);
}
const ws = new WebSocket(`${BASE}?role=join&code=${encodeURIComponent(code)}`);
ws.onmessage = (m) => {
  const f = JSON.parse(m.data);
  if (f.t === "joined") {
    // platform 1 = desktop (net_identity.h's wire tag). No `cred`, so the
    // worker attests nothing and the claim stays CLAIMED — which is what
    // the resolver under test matches on in an all-desktop room anyway.
    ws.send(JSON.stringify({ t: "identity", platform: 1, name }));
    console.log("JOINED");
  } else if (f.t === "err") {
    console.log("FAILED " + f.reason);
    process.exit(1);
  }
};
ws.onerror = () => { console.log("FAILED socket"); process.exit(1); };
ws.onclose = () => { console.log("CLOSED"); process.exit(1); };
// Backstop: never outlive the driver that started it.
setTimeout(() => process.exit(0), 120000);
