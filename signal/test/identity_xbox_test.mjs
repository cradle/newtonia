// Xbox user ids between Xbox players only (xbox/NETPLAY_XBOX.md 7b in the
// fork; decided 2026-10-24). An Xbox console checks its player's block
// list and privacy settings against each Xbox player it plays with, which
// takes that player's Xbox user id. The worker learns it when it verifies
// the console's token, and passes it ({xuid}) ONLY between Xbox-verified
// players in the same room: a PC/phone/web peer never receives one, an
// unverified Xbox claim never receives or carries one, and a frame heard
// before the recipient was verified is re-sent with the id once it is.
// Needs FAKE_VERIFY=1 (the fake "account" is the cred verbatim).
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

const XBOX = 6, STEAM = 2;
const has_xuid = (frames) =>
  frames.some((f) => f && f.t === "identity" && "xuid" in f);

(async () => {
  // Room 1: an Xbox host.
  const { host, code } = await host_room();
  host.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-HOST" }));
  await t(250);

  // A Steam joiner hears the host's identity without the id, and the host
  // hears the Steam joiner's without one either (nothing to carry).
  const pc = await join_room(code);
  const pc_id = (await host._recvType("peer")).from;
  const h_to_pc = await pc._recvType("identity");
  check("pc joiner heard the xbox host's identity",
        h_to_pc && h_to_pc.role === "host" && h_to_pc.platform === XBOX &&
        h_to_pc.verified === true);
  check("pc joiner got no xbox user id", h_to_pc && !("xuid" in h_to_pc));
  pc.send(JSON.stringify({ t: "identity", platform: STEAM, name: "PC", cred: "x" }));
  const pc_to_h = await host._recvType("identity");
  check("host heard the pc joiner", pc_to_h && pc_to_h.from === pc_id);
  check("pc joiner's frame carries no id", pc_to_h && !("xuid" in pc_to_h));

  // An Xbox joiner: replayed the host's identity BEFORE it is verified, so
  // that copy carries no id...
  const xb = await join_room(code);
  const xb_id = (await host._recvType("peer")).from;
  const h_to_xb0 = await xb._recvType("identity");
  check("unverified xbox joiner got no id", h_to_xb0 && !("xuid" in h_to_xb0));
  // ...then it verifies: the host hears it WITH its id, and it hears the
  // host's identity again, WITH the host's id.
  xb.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-JOINER" }));
  const xb_to_h = await host._recvType("identity");
  check("host heard the xbox joiner with its id",
        xb_to_h && xb_to_h.from === xb_id && xb_to_h.xuid === "XUID-JOINER");
  const h_to_xb = await xb._recvType("identity");
  check("xbox joiner re-told the host's identity with its id",
        h_to_xb && h_to_xb.role === "host" && h_to_xb.xuid === "XUID-HOST");
  await t(300);
  check("pc joiner never received an xbox id", !has_xuid(pc._drain()));

  // A claimed-Xbox joiner whose verify FAILS (the fake backend attests every
  // plain claim, so the SLOWFAIL hook stands in for a rejected token):
  // nothing with an id reaches it, and its frame carries none.
  const fake = await join_room(code);
  await host._recvType("peer");
  fake.send(JSON.stringify({ t: "identity", platform: XBOX, name: "FAKE", cred: "SLOWFAIL:50" }));
  const fake_to_h = await host._recvType("identity");
  check("unverified xbox claim carries no id",
        fake_to_h && fake_to_h.verified === false && !("xuid" in fake_to_h));
  await t(300);
  check("unverified xbox claim received no id", !has_xuid(fake._drain()));

  // Room 2: a Steam host — an Xbox joiner's frame reaches it without the id,
  // and the joiner hears no id (the host has none).
  const r2 = await host_room();
  r2.host.send(JSON.stringify({ t: "identity", platform: STEAM, name: "STEAMHOST", cred: "x" }));
  await t(250);
  const xb2 = await join_room(r2.code);
  await r2.host._recvType("peer");
  await xb2._recvType("identity");
  xb2.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-TWO" }));
  const xb2_to_h = await r2.host._recvType("identity");
  check("steam host got no xbox id",
        xb2_to_h && xb2_to_h.verified === true && !("xuid" in xb2_to_h));
  await t(300);
  check("xbox joiner of a steam host got no id", !has_xuid(xb2._drain()));

  host.close(); pc.close(); xb.close(); fake.close();
  r2.host.close(); xb2.close();
  finish("IDENTITY-XBOX-TEST-OK");
})();
