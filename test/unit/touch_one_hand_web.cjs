// Exercise the compiled production touch controls with a fake DOM/clock.
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
// Run the complete UI factory, including action buttons: their finger
// ownership must be reset alongside the joystick. No handlers are copied.
const start = source.indexOf('function buildTouchControls()');
const end = source.indexOf('// Tracks the active resize listener', start);
assert.ok(start >= 0 && end > start);
const code = source.slice(start, end) + '\nbuildTouchControls();';
const lifecycleStart = source.indexOf('document.addEventListener("visibilitychange"');
const lifecycleEnd = source.indexOf('TOUCH_MEDIA.addEventListener("change"', lifecycleStart);
assert.ok(lifecycleStart >= 0 && lifecycleEnd > lifecycleStart);
const lifecycleCode = source.slice(lifecycleStart, lifecycleEnd);
function harness() {
  let now = 1000, seq = 0;
  const timers = new Map(), joystick = [], keys = [];
  const element = () => {
    const el = { style: {}, handlers: {}, children: [], className: '',
      appendChild(child) { this.children.push(child); },
      querySelector(selector) {
        return this.children.find(child => child.classList.contains(selector.slice(1)));
      },
      addEventListener(k, f) { this.handlers[k] = f; },
    };
    el.classList = {
      contains(cls) { return el.className.split(' ').includes(cls); },
      add(cls) { if (!this.contains(cls)) el.className += ' ' + cls; },
      remove(cls) { el.className = el.className.split(' ').filter(c => c !== cls).join(' '); },
    };
    return el;
  };
  const container = element();
  const context = {
    document: { getElementById: () => container, createElement: element, hidden: false,
      handlers: {}, addEventListener(k, f) { this.handlers[k] = f; } },
    canvas: { getBoundingClientRect: () => ({left:0, top:0, width:1000, height:600}),
      dispatchEvent: e => keys.push([e.type, e.key]) },
    window: { setTimeout(f, ms) { timers.set(++seq, { f, at: now + ms }); return seq; },
      clearTimeout(i) { timers.delete(i); },
      handlers: {}, addEventListener(k, f) { this.handlers[k] = f; } },
    Date: { now: () => now }, Math, Set,
    KeyboardEvent: class { constructor(type, options) { this.type = type; this.key = options.key; } },
    requestAnimationFrame() {}, Module: {},
    ResizeObserver: class { observe() {} disconnect() {} },
    _resizeObserver:null, _circleButtonEls:[], _teleportReady:true,
    setTeleportReady() {}, _menuOverlay:null,
    _oneHand:true, _hand:0, _tapFire:true, _inMenuMode:false, _mineAvailable:false,
    _secondaryKind:-1, _shieldEngaged:false, _holdRelease:null, _resetTouchGestures:null,
    _joyPlaceholderEls:[], _positionJoyPlaceholder:null,
    callTouchJoystick: (x, y) => joystick.push([x, y]),
  };
  vm.createContext(context); vm.runInContext(code + lifecycleCode, context);
  return {
    context, keys,
    button(cls, type, id) {
      container.querySelector('.' + cls).handlers[type]({ preventDefault() {},
        changedTouches:[{ identifier:id }] });
    },
    pressed(cls) { return container.querySelector('.' + cls).classList.contains('pressed'); },
    send(type, x=500, y=400, id=1) {
      container.querySelector('.joy-zone').handlers[type]({ type, preventDefault() {},
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
  h.send('touchmove', 500+x*r, 400+y*r); h.advance(80);
}
for (const sign of [-1, 1]) {
  const h = harness(); steer(h, .3, sign*.4);
  h.send('touchmove', 500+.6*r, 400+sign*.7*r); h.advance(10);
  h.send('touchend'); h.expect(0, 0); h.advance(100);
  for (let i=0; i<3; i++) {
    h.send('touchstart'); h.expect(.3, sign*.4); h.advance(40);
    h.send('touchend'); h.expect(.3, sign*.4); h.advance(100);
  }
  assert.equal(h.keys.filter(([type,key]) => type === 'keydown' && key === ' ').length, 3);
  h.advance(5000); h.expect(.3, sign*.4);
  h.send('touchstart'); h.expect(.3, sign*.4); h.advance(40);
  h.send('touchend'); h.advance(1000); h.expect(.3, sign*.4);
  h.send('touchstart'); h.send('touchmove', 500, 400-.4*r);
  h.send('touchmove', 500, 400); h.send('touchend'); h.expect(0, 0);
  h.advance(100); h.send('touchstart'); h.expect(0, 0);
}
for (const cancelled of [false, true]) {
  const h = harness(); steer(h); h.send('touchend'); h.advance(100);
  h.send('touchstart'); h.expect(.3, -.4);
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

{
  const h = harness(); steer(h, .5, 0); h.send('touchend'); h.advance(100);
  h.send('touchstart'); h.expect(.5, 0); h.send('touchend');
  h.advance(5000); h.expect(.5, 0);
}

// Backgrounding online never drops _tapFire. No finger is down to emit a
// touchcancel once the stick is latched, so the page lifecycle must stop it.
for (const event of ['visibilitychange', 'pagehide']) {
  for (const heldFinger of [false, true]) {
    const h = harness(); steer(h); h.send('touchend'); h.advance(100);
    h.send('touchstart'); h.advance(40); h.send('touchend');
    h.advance(1000); h.expect(.3, -.4);
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


// Hidden pages can omit touchcancel. Reset each button's complete finger
// set, release the held key once, and accept a new press after resume.
for (const event of ['visibilitychange', 'pagehide']) {
  for (const [cls, key] of [['touch-mine', 'x'], ['touch-boost', 'e'],
                          ['touch-teleport', 't'], ['touch-pause', 'p'],
                          ['touch-shoot', ' ']]) {
    const h = harness();
    h.button(cls, 'touchstart', 11);
    h.button(cls, 'touchstart', 12);
    assert.deepEqual(h.keys, [['keydown', key]]);
    assert.ok(h.pressed(cls));
    if (event === 'visibilitychange') {
      h.context.document.hidden = true;
      h.context.document.handlers[event]();
      h.context.document.hidden = false;
    } else h.context.window.handlers[event]();
    assert.deepEqual(h.keys, [['keydown', key], ['keyup', key]]);
    assert.ok(!h.pressed(cls));
    // Reset is idempotent, and old touches cannot release a fresh hold.
    h.context._resetTouchGestures();
    h.button(cls, 'touchstart', 13);
    h.button(cls, 'touchend', 11);
    h.button(cls, 'touchcancel', 12);
    assert.deepEqual(h.keys.at(-1), ['keydown', key]);
    assert.ok(h.pressed(cls));
    h.button(cls, 'touchend', 13);
    assert.deepEqual(h.keys, [['keydown', key], ['keyup', key],
                              ['keydown', key], ['keyup', key]]);
    assert.ok(!h.pressed(cls));
  }
}

// Ordinary multi-finger holds still last until the final finger lifts.
{
  const h = harness();
  h.button('touch-mine', 'touchstart', 1);
  h.button('touch-mine', 'touchstart', 2);
  h.button('touch-mine', 'touchend', 1);
  assert.deepEqual(h.keys, [['keydown', 'x']]);
  h.button('touch-mine', 'touchcancel', 2);
  assert.deepEqual(h.keys, [['keydown', 'x'], ['keyup', 'x']]);
  h.button('touch-mine', 'touchend', 2);
  assert.equal(h.keys.length, 2);
}

console.log('touch_one_hand_web: all checks passed');
