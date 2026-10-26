// Joiner identities between joiners, Xbox recipients only (fork
// xbox/NETPLAY_XBOX.md 7c). In a 3-4 player room an Xbox console checks its
// player's block list and privacy settings against every player it will
// play with, not only the host, so an Xbox-verified joiner hears the other
// joiners' identity frames: role "joiner", stamped {from}, with the Xbox
// user id only when that joiner is Xbox-verified too. A joiner that is not
// Xbox-verified hears none of them (identity_fanout_test covers the plain
// rooms), and a joiner that becomes verified is replayed everyone already
// in the room. Needs FAKE_VERIFY=1 (the fake "account" is the cred).
import { t, check, finish, host_room, join_room } from "./ws_harness.mjs";

const XBOX = 6, STEAM = 2;
const joiner_frames = (frames) =>
  frames.filter((f) => f && f.t === "identity" && f.role === "joiner");

(async () => {
  // A Steam host: the joiner leg must not depend on the host's platform.
  const { host, code } = await host_room();
  host.send(JSON.stringify({ t: "identity", platform: STEAM, name: "HOST", cred: "x" }));
  await t(250);

  // Xbox joiner A verifies first.
  const a = await join_room(code);
  const a_id = (await host._recvType("peer")).from;
  a.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-A" }));
  await host._recvType("identity");
  await t(300);
  a._drain();

  // A PC joiner: A hears it (no id to carry); the PC joiner hears no joiner.
  const pc = await join_room(code);
  const pc_id = (await host._recvType("peer")).from;
  pc.send(JSON.stringify({ t: "identity", platform: STEAM, name: "PC", cred: "x" }));
  await host._recvType("identity");
  await t(300);
  const a_pc = joiner_frames(a._drain());
  check("xbox joiner heard the pc joiner",
        a_pc.length > 0 && a_pc[a_pc.length - 1].from === pc_id &&
        a_pc[a_pc.length - 1].platform === STEAM &&
        a_pc[a_pc.length - 1].verified === true);
  check("pc joiner's frame to the xbox joiner carries no id",
        a_pc.every((f) => !("xuid" in f)));

  // Xbox joiner B: before it is verified it hears no joiner; once verified
  // it is replayed A (with A's id) and the PC joiner (without one), and A
  // hears B with B's id.
  const b = await join_room(code);
  const b_id = (await host._recvType("peer")).from;
  await t(250);
  check("unverified joiner heard no other joiner",
        joiner_frames(b._drain()).length === 0);
  b.send(JSON.stringify({ t: "identity", platform: XBOX, name: "", cred: "XUID-B" }));
  await host._recvType("identity");
  await t(400);
  const b_heard = joiner_frames(b._drain());
  const b_a = b_heard.find((f) => f.from === a_id);
  const b_pc = b_heard.find((f) => f.from === pc_id);
  check("verified xbox joiner replayed the other xbox joiner with its id",
        b_a && b_a.verified === true && b_a.xuid === "XUID-A");
  check("verified xbox joiner replayed the pc joiner without an id",
        b_pc && !("xuid" in b_pc));
  check("joiner never hears itself",
        !b_heard.some((f) => f.from === b_id));
  const a_b = joiner_frames(a._drain()).find((f) => f.from === b_id);
  check("xbox joiner heard the new xbox joiner with its id",
        a_b && a_b.verified === true && a_b.xuid === "XUID-B");

  check("pc joiner never heard another joiner",
        joiner_frames(pc._drain()).length === 0);
  pc.close();  // a room holds three joiners
  await host._recvType("peer");

  // A claimed Xbox joiner whose verify fails: the Xbox joiners hear the
  // claim (to wait on), never an id; it hears no joiner itself.
  const fake = await join_room(code);
  const fake_id = (await host._recvType("peer")).from;
  fake.send(JSON.stringify({ t: "identity", platform: XBOX, name: "FAKE", cred: "SLOWFAIL:50" }));
  await host._recvType("identity");
  await t(400);
  const a_fake = joiner_frames(a._drain()).filter((f) => f.from === fake_id);
  check("xbox joiner heard the unverified xbox claim",
        a_fake.length > 0 && a_fake.every((f) => f.verified === false &&
                                               f.platform === XBOX));
  check("unverified claim carries no id", a_fake.every((f) => !("xuid" in f)));
  check("unverified claim heard no other joiner",
        joiner_frames(fake._drain()).length === 0);

  host.close(); a.close(); b.close(); fake.close();
  finish("IDENTITY-XBOX-JOINERS-TEST-OK");
})();
