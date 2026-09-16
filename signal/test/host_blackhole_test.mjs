// Accepted capacity behaviour during silent host loss: retain the last full
// report until socket closure starts grace. Exit 0 means assertions passed.
// Run against a local wrangler worker: SIGNAL_TEST_URL=ws://127.0.0.1:8798/ws
// node signal/test/host_blackhole_test.mjs
import net from 'node:net';
import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';
const base = new URL(process.env.SIGNAL_TEST_URL || 'ws://127.0.0.1:8798/ws');
assert(base.protocol === 'ws:' && ['127.0.0.1', 'localhost'].includes(base.hostname),
       'This reproduction is local-only');
const sockets = new Set();
const clients = [];
let blocked = false;
const proxy = net.createServer(client => {
  const upstream = net.connect(Number(base.port), base.hostname);
  for (const s of [client, upstream]) {
    sockets.add(s);
    s.on('error', () => {});
    s.on('close', () => { sockets.delete(s); client.destroy(); upstream.destroy(); });
  }
  client.on('data', data => { if (!blocked) upstream.write(data); });
  upstream.on('data', data => { if (!blocked) client.write(data); });
});
async function open(url) {
  const ws = new WebSocket(url);
  clients.push(ws);
  ws.frames = [];
  ws.addEventListener('message', e => ws.frames.push(JSON.parse(e.data)));
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('open timeout')), 5000);
    ws.addEventListener('open', () => { clearTimeout(timer); resolve(); }, {once:true});
    ws.addEventListener('error', () => { clearTimeout(timer); reject(new Error('socket error')); }, {once:true});
  });
  return ws;
}
async function frame(ws, type) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    const i = ws.frames.findIndex(f => f.t === type);
    if (i >= 0) return ws.frames.splice(i, 1)[0];
    await delay(20);
  }
  throw new Error('No frame: ' + type + ' received ' + JSON.stringify(ws.frames));
}
try {
  await new Promise(resolve => proxy.listen(0, '127.0.0.1', resolve));
  const hostUrl = new URL(base);
  hostUrl.port = String(proxy.address().port);
  hostUrl.search = '?role=host';
  const host = await open(hostUrl);
  const {code} = await frame(host, 'room');
  host.send(JSON.stringify({t:'seats', free:0}));
  await delay(300);
  async function freshFull(label) {
    const j = await open(base + '?role=join&code=' + code);
    const error = await frame(j, 'err');
    assert.equal(error.reason, 'room-full');
    j.close();
    console.log(label + ': room-full');
  }
  await freshFull('Connected full host (control)');
  blocked = true;
  const start = Date.now();
  for (const seconds of [30, 60]) {
    await delay(Math.max(0, start + seconds * 1000 - Date.now()));
    assert.equal(host.readyState, WebSocket.OPEN);
    await freshFull('Host traffic blackholed for ' + seconds + 's');
  }
  // Once the server actually observes a close, it enters grace and admits
  // the exact same kind of fresh join despite the stale full report.
  for (const s of sockets) s.destroy();
  await delay(500);
  const j = await open(base + '?role=join&code=' + code);
  await frame(j, 'joined');
  console.log('Host socket closed (control): fresh join admitted in grace');
  console.log('PASS: silent host loss retains capacity; socket closure enables grace admission');
} finally {
  for (const ws of clients) ws.close();
  for (const s of sockets) s.destroy();
  proxy.close();
}
