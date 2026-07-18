// Pretends to be a pre-pv (old) game build hosting a room: creates the
// room, immediately sends a pv-less offer (exercising the worker's
// stored-offer replay), prints "CODE XXXXX", and idles until killed.
// Companion to mismatch.sh. SIGNAL_WS overrides the relay (default local).
const BASE = process.env.SIGNAL_WS || "ws://127.0.0.1:8787/ws";
const ws = new WebSocket(BASE + "?role=host");
ws.onmessage = (m) => {
  const f = JSON.parse(m.data);
  if (f.t === "room") {
    console.log("CODE " + f.code);
    ws.send(JSON.stringify({ t: "offer", sdp: "v=0\r\no=old-build" }));
  }
};
setTimeout(() => process.exit(0), 60000);
