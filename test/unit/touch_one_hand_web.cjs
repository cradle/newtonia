// Exercise the compiled production joystick handlers with a fake DOM/clock.
// No WASM or browser required. Run: node test/unit/touch_one_hand_web.cjs
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '../..');
const out = fs.mkdtempSync(path.join(os.tmpdir(), 'newtonia-touch-web-'));
let source;
try {
  execFileSync('tsc', ['-p', path.join(root, 'web/tsconfig.json'), '--outDir', out]);
  source = fs.readFileSync(path.join(out, 'main.js'), 'utf8');
} finally { fs.rmSync(out, { recursive: true, force: true }); }
// Isolate the joystick portion of the real UI factory; action buttons below
// BUTTONS are unrelated. No gesture implementation is copied into this test.
const start = source.indexOf('function buildTouchControls()');
const end = source.indexOf('const BUTTONS', start);
assert.ok(start >= 0 && end > start);
const code = source.slice(start, end) + '\nreturn joyZone; } globalThis.zone = buildTouchControls();';
const lifecycleStart = source.indexOf('document.addEventListener("visibilitychange"');
const lifecycleEnd = source.indexOf('TOUCH_MEDIA.addEventListener("change"', lifecycleStart);
assert.ok(lifecycleStart >= 0 && lifecycleEnd > lifecycleStart);
const lifecycleCode = source.slice(lifecycleStart, lifecycleEnd);
function harness() {
  let now = 1000, seq = 0;
  const timers = new Map(), joystick = [], keys = [];
  const element = () => ({ style: {}, handlers: {}, appendChild() {},
    addEventListener(k, f) { this.handlers[k] = f; } });
  const context = {
    document: { getElementById: element, createElement: element, hidden: false,
      handlers: {}, addEventListener(k, f) { this.handlers[k] = f; } },
    canvas: { getBoundingClientRect: () => ({left:0, top:0, width:1000, height:600}),
      dispatchEvent: e => keys.push([e.type, e.key]) },
    window: { setTimeout(f, ms) { timers.set(++seq, { f, at: now + ms }); return seq; },
      clearTimeout(i) { timers.delete(i); },
      handlers: {}, addEventListener(k, f) { this.handlers[k] = f; } },
    Date: { now: () => now }, Math, Set,
    KeyboardEvent: class { constructor(type, options) { this.type = type; this.key = options.key; } },
    requestAnimationFrame() {}, Module: {},
    _oneHand:true, _hand:0, _tapFire:true, _inMenuMode:false, _mineAvailable:false,
    _secondaryKind:-1, _shieldEngaged:false, _holdRelease:null, _resetTouchGestures:null,
    _joyPlaceholderEls:[], _positionJoyPlaceholder:null,
    callTouchJoystick: (x, y) => joystick.push([x, y]),
  };
  vm.createContext(context); vm.runInContext(code + lifecycleCode, context);
  return {
    context, keys,
    send(type, x=500, y=400, id=1) {
      context.zone.handlers[type]({ type, preventDefault() {},
        changedTouches:[{ identifier:id, clientX:x, clientY:y }] });
    },
    advance(ms) {
      const until = now + ms;
      for (;;) {
        const due = [...timers].filter(([, t]) => t.at <= until).sort((a,b) => a[1].at-b[1].at)[0];
        if (!due) break;
        now = due[1].at; timers.delete(due[0]); due[1].f();
      }
      now = until;
    },
    expect(x, y) {
      assert.ok(joystick.length);
      assert.ok(Math.abs(joystick.at(-1)[0]-x) < 1e-6);
      assert.ok(Math.abs(joystick.at(-1)[1]-y) < 1e-6);
    },
  };
}
const r = 600 * 0.24;
function steer(h, x=.3, y=-.4) {
  h.send('touchstart'); h.advance(16);
  h.send('touchmove', 500+x*r, 400+y*r); h.expect(x, y); h.advance(80);
}
for (const sign of [-1, 1]) {
  const h = harness(); steer(h, .3, sign*.4);
  h.send('touchmove', 500+.6*r, 400+sign*.7*r); h.advance(10);
  h.send('touchend'); h.expect(0, 0); h.advance(100);
  for (let i=0; i<3; i++) {
    h.send('touchstart'); h.expect(0, sign*.4); h.advance(40);
    h.send('touchend'); h.expect(0, sign*.4); h.advance(100);
  }
  assert.equal(h.keys.filter(([type,key]) => type === 'keydown' && key === ' ').length, 3);
  h.advance(5000); h.expect(0, sign*.4);
  h.send('touchstart'); h.expect(0, sign*.4); h.advance(40);
  h.send('touchend'); h.advance(1000); h.expect(0, sign*.4);
  h.send('touchstart'); h.send('touchmove', 500, 400-.4*r);
  h.send('touchmove', 500, 400); h.send('touchend'); h.expect(0, 0);
  h.advance(100); h.send('touchstart'); h.expect(0, 0);
}
for (const cancelled of [false, true]) {
  const h = harness(); steer(h); h.send('touchend'); h.advance(100);
  h.send('touchstart'); h.expect(0, -.4);
  if (cancelled) {
    h.send('touchcancel'); h.expect(0, 0);
    assert.equal(h.keys.filter(([type,key]) => type === 'keydown' && key === ' ').length, 0);
  } else {
    h.context._tapFire = false; h.context._holdRelease(); h.expect(0, 0);
    h.send('touchend');
  }
  h.advance(100); h.send('touchstart'); h.expect(0, 0);
}
for (const [x,y] of [[0,0], [0,.4]]) {
  const h = harness(); steer(h);
  h.send('touchmove', 500+x*r, 400+y*r); h.advance(10);
  h.send('touchend'); h.advance(100); h.send('touchstart'); h.expect(0, 0);
}
{
  const h = harness(); steer(h);
  h.send('touchmove', 500, 400-.3*r); h.advance(60);
  h.send('touchend'); h.advance(100); h.send('touchstart'); h.expect(0, -.3);
  h.send('touchmove', 500, 400+.5*r); h.advance(10);
  h.send('touchend'); h.advance(100); h.send('touchstart'); h.expect(0, .5);
}

{
  const h = harness(); steer(h); h.send('touchend'); h.advance(301);
  h.send('touchstart'); h.expect(0, 0);
}

for (const x of [-.5, .5]) {
  const h = harness(); steer(h, x, 0); h.send('touchend'); h.expect(0, 0);
  h.advance(100); h.send('touchstart'); h.expect(0, 0);
  h.send('touchmove', 500+.11*r, 400); h.expect(0, 0);
  h.send('touchend');
  h.advance(5000); h.expect(0, 0);
  assert.equal(h.keys.filter(([type,key]) => type === 'keydown' && key === ' ').length, 1);
}

// Backgrounding online never drops _tapFire. No finger is down to emit a
// touchcancel once the stick is latched, so the page lifecycle must stop it.
for (const event of ['visibilitychange', 'pagehide']) {
  for (const heldFinger of [false, true]) {
    const h = harness(); steer(h); h.send('touchend'); h.advance(100);
    h.send('touchstart'); h.advance(40); h.send('touchend');
    h.advance(1000); h.expect(0, -.4);
    if (heldFinger) h.send('touchstart', 500, 400, 7);
    if (event === 'visibilitychange') {
      h.context.document.hidden = true;
      h.context.document.handlers[event]();
    } else h.context.window.handlers[event]();
    h.expect(0, 0);
    h.context.document.hidden = false;
    h.advance(1000);
    h.send('touchstart', 500, 400, 8); h.expect(0, 0);
    // A new identifier must own the stick, even if the old finger never ended.
    h.send('touchmove', 500-.5*r, 400, 8); h.expect(-.5, 0);
  }
}

// Reset cancels a held primary and its double-tap chain, invalidates old
// long-press timers, and cannot turn a later stale release into a shot.
{
  const h = harness();
  h.send('touchstart'); h.advance(40); h.send('touchend'); h.advance(20);
  h.send('touchstart', 500, 400, 2);
  h.context._mineAvailable = true;
  h.context.window.handlers.pagehide();
  assert.deepEqual(h.keys.at(-1), ['keyup', ' ']);
  const at = h.keys.length;
  h.advance(1000); h.send('touchend', 500, 400, 2);
  assert.equal(h.keys.length, at);
  h.send('touchstart', 500, 400, 3);
  assert.equal(h.keys.length, at); // no stale fire-hold on the fresh press
}

// Repositioning the idle UI must keep showing the input that is still flying.
{
  const h = harness(); steer(h); h.send('touchend'); h.advance(100);
  h.send('touchstart'); h.advance(40); h.send('touchend');
  const nub = h.context._joyPlaceholderEls[1];
  const heldStyle = nub.style.cssText;
  h.context._positionJoyPlaceholder();
  assert.equal(nub.style.cssText, heldStyle);
  h.send('touchstart'); h.send('touchcancel');
  assert.match(nub.style.cssText, /opacity:0.4/);
}

console.log('touch_one_hand_web: all checks passed');
