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
// Execute the complete production factory, including moving button DOM.
const start = source.indexOf('function buildTouchControls()');
const end = source.indexOf('// Tracks the active resize listener', start);
assert.ok(start >= 0 && end > start);
const code = source.slice(start, end) + '\nglobalThis.resizeControls = buildTouchControls();';
const lifecycleStart = source.indexOf('document.addEventListener("visibilitychange"');
const lifecycleEnd = source.indexOf('TOUCH_MEDIA.addEventListener("change"', lifecycleStart);
assert.ok(lifecycleStart >= 0 && lifecycleEnd > lifecycleStart);
const lifecycleCode = source.slice(lifecycleStart, lifecycleEnd);
function harness(width=1000, height=600, hand=0) {
  let now = 1000, seq = 0;
  const timers = new Map(), joystick = [], keys = [];
  const element = () => {
    const el = { style: {}, handlers: {}, children: [], className: '',
      appendChild(child) { this.children.push(child); },
      addEventListener(k, f) { this.handlers[k] = f; },
      querySelector(selector) {
        return this.children.find(child => child.classList.contains(selector.slice(1)));
      },
    };
    el.classList = {
      contains: name => el.className.split(' ').includes(name),
      add(name) { if (!this.contains(name)) el.className += ' '+name; },
      remove(name) { el.className = el.className.split(' ').filter(x => x !== name).join(' '); },
      toggle(name, on) { if (on) this.add(name); else this.remove(name); },
    };
    return el;
  };
  const container = element();
  const bounds = {left:0, top:0, width, height};
  const context = {
    document: { getElementById: () => container, createElement: element, hidden: false,
      handlers: {}, addEventListener(k, f) { this.handlers[k] = f; } },
    canvas: { getBoundingClientRect: () => bounds,
      dispatchEvent: e => keys.push([e.type, e.key]) },
    window: { setTimeout(f, ms) { timers.set(++seq, { f, at: now + ms }); return seq; },
      clearTimeout(i) { timers.delete(i); },
      handlers: {}, addEventListener(k, f) { this.handlers[k] = f; } },
    Date: { now: () => now }, Math, Set,
    KeyboardEvent: class { constructor(type, options) { this.type = type; this.key = options.key; } },
    requestAnimationFrame() {}, Module: {},
    ResizeObserver: class { observe() {} disconnect() {} },
    _resizeObserver:null, _circleButtonEls:[], _menuOverlay:null,
    _teleportReady:true, setTeleportReady() {},
    _oneHand:true, _hand:hand, _tapFire:true, _inMenuMode:false, _mineAvailable:false,
    _secondaryKind:-1, _shieldEngaged:false, _holdRelease:null, _resetTouchGestures:null,
    _joyPlaceholderEls:[], _positionJoyPlaceholder:null,
    callTouchJoystick: (x, y) => joystick.push([x, y]),
  };
  vm.createContext(context); vm.runInContext(code + lifecycleCode, context);
  context.zone = container.querySelector(".joy-zone");
  context.resizeControls();
  return {
    context, keys, container, bounds,
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
function steer(h, x=0, y=-.4) {
  h.send('touchstart'); h.advance(16);
  h.send('touchmove', 500+x*r, 400+y*r); h.expect(x, y); h.advance(80);
}
for (const horizontal of [-.6, .6]) for (const sign of [-1, 1]) {
  const h = harness(); steer(h, .3, sign*.4);
  h.send('touchmove', 500+horizontal*r, 400+sign*.7*r); h.advance(10);
  h.send('touchend'); h.expect(0, 0); h.advance(100);
  assert.ok(h.context._joyPlaceholderEls[1].style.cssText.includes(`top:${400+sign*.7*r}px`));
  assert.ok(h.context._joyPlaceholderEls[1].style.cssText.includes(`left:${500+horizontal*r}px`));
  for (let i=0; i<3; i++) {
    h.send('touchstart', 500+horizontal*r, 400+sign*.7*r); h.expect(horizontal, sign*.7);
    assert.equal(h.context._joyPlaceholderEls[1].style.left, `${500+horizontal*r}px`);
    h.advance(40);
    h.send('touchend'); h.expect(0, 0); h.advance(100);
  }
  assert.equal(h.keys.filter(([type,key]) => type === 'keydown' && key === ' ').length, 3);
  h.advance(399); h.expect(0, 0); // 499 ms after the final tap release
  assert.ok(h.context._joyPlaceholderEls[1].style.cssText.includes(`left:${500+horizontal*r}px`));
  h.advance(1); h.expect(0, 0);
  assert.match(h.context._joyPlaceholderEls[1].style.cssText, /left:500px;top:400px/);
  h.send('touchstart'); h.expect(0, 0); h.advance(40);
  h.send('touchend'); h.advance(5000); h.expect(0, 0);
  h.send('touchstart'); h.send('touchmove', 500, 400-.4*r);
  h.send('touchmove', 500, 400); h.send('touchend'); h.expect(0, 0);
  h.advance(100); h.send('touchstart'); h.expect(0, 0);
}
for (const cancelled of [false, true]) {
  const h = harness(); steer(h); h.send('touchend'); h.advance(100);
  h.send('touchstart', 500, 400-.4*r); h.expect(0, -.4);
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
  h.send('touchend'); h.advance(100); h.send('touchstart', 500, 400+y*r); h.expect(0, y);
}
{
  const h = harness(); steer(h);
  h.send('touchmove', 500, 400-.3*r); h.advance(60);
  h.send('touchend'); h.advance(100); h.send('touchstart', 500, 400-.3*r); h.expect(0, -.3);
  h.send('touchmove', 500, 400+.5*r); h.advance(10);
  h.send('touchend'); h.advance(100); h.send('touchstart', 500, 400+.5*r); h.expect(0, .5);
}

{
  const h = harness(); steer(h); h.send('touchend'); h.advance(500);
  h.send('touchstart'); h.expect(0, 0);
}

for (const x of [-.5, .5]) {
  const h = harness(); steer(h, x, 0); h.send('touchend'); h.expect(0, 0);
  h.advance(100); h.send('touchstart', 500+x*r, 400); h.expect(x, 0);
  h.send('touchmove', 500+(x+.11)*r, 400); h.expect(x, 0);
  h.send('touchend');
  h.advance(5000); h.expect(0, 0);
  assert.equal(h.keys.filter(([type,key]) => type === 'keydown' && key === ' ').length, 1);
}

// Backgrounding online never drops _tapFire. No finger is down to emit a
// touchcancel after release, so the page lifecycle must clear the snapshot.
for (const event of ['visibilitychange', 'pagehide']) {
  for (const heldFinger of [false, true]) {
    const h = harness(); steer(h); h.send('touchend'); h.advance(100);
    h.send('touchstart',500,400-.4*r); h.advance(40); h.send('touchend');
    h.advance(100); h.expect(0, 0);
    if (heldFinger) h.send('touchstart', 500, 400-.4*r, 7);
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

// Repositioning the idle UI preserves the saved-direction preview.
{
  const h = harness(); steer(h); h.send('touchend'); h.advance(100);
  h.send('touchstart',500,400-.4*r); h.advance(40); h.send('touchend');
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

function actionPositions(h) {
  return ['touch-mine', 'touch-boost', 'touch-teleport'].map(name => {
    const el = h.container.querySelector('.'+name);
    return { el, x:parseFloat(el.style.left), y:parseFloat(el.style.top),
      radius:parseFloat(el.style.width)/2 };
  });
}
function pressButton(button, type, id) {
  button.el.handlers[type]({ preventDefault() {},
    changedTouches:[{identifier:id, clientX:button.x, clientY:button.y}] });
}
// A slower lift/tap/rehold keeps the ring, nub and action cluster anchored.
{
  const h = harness(); steer(h, 0, -.4);
  const [base, nub] = h.context._joyPlaceholderEls;
  const placed = actionPositions(h).map(p => [p.x,p.y]);
  h.send('touchend'); h.expect(0, 0);
  assert.ok(nub.style.cssText.includes(`top:${400-.4*r}px`));
  h.advance(450);
  for (const [nx,ny] of [[-.4,-.6], [.4,-.6], [.4,.6]]) {
    const x=500+nx*r, y=400+ny*r;
    h.send('touchstart', x,y); h.expect(nx, ny);
    assert.match(base.style.cssText, /left:500px;top:400px/);
    assert.equal(nub.style.left, `${x}px`);
    assert.equal(nub.style.top, `${y}px`);
    assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
    h.send('touchmove', x+2,y+1); h.expect(nx, ny);
    h.advance(40); h.send('touchend',x,y); h.expect(0, 0); h.advance(450);
  }
  // Neutral input retains the base for the full window too.
  h.send('touchstart', 500,400); h.expect(0,0);
  assert.equal(nub.style.left, '500px');
  assert.equal(nub.style.top, '400px');
  h.send('touchend'); h.advance(499);
  h.send('touchstart',500+.5*r,400); h.expect(.5,0);
  h.send('touchmove',500-.5*r,400-.4*r); h.expect(-.5,-.4);
  h.advance(1000); // a held finger keeps the base even beyond 500 ms
  assert.match(base.style.cssText, /left:500px;top:400px/);
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
  h.send('touchend'); h.expect(0,0); h.advance(500);
  h.send('touchstart',650,420); h.expect(0,0);
  assert.match(base.style.cssText, /left:650px;top:420px/);
  h.send('touchmove',650+.3*r,420); h.expect(.3,0);
  assert.notDeepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
}
{
  const h = harness();
  const home = actionPositions(h).map(p => [p.x,p.y]);
  h.send('touchstart', 400, 500);
  const placed = actionPositions(h).map(p => [p.x,p.y]);
  assert.notDeepEqual(placed, home);
  h.send('touchmove', 450, 450);
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
  h.send('touchend');
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
  const boost = actionPositions(h)[1];
  pressButton(boost, 'touchstart', 2);
  assert.deepEqual(h.keys.at(-1), ['keydown', 'e']);
  h.advance(1500); // a held action keeps the base beyond the ordinary window
  h.send('touchstart', 100, 200, 3);
  h.send('touchmove', 130, 200, 3);
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
  pressButton(boost, 'touchend', 2);
  assert.deepEqual(h.keys.at(-1), ['keyup', 'e']);
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), placed);
  // A page hide cannot leave the action cluster frozen on a lost finger.
  pressButton(actionPositions(h)[0], 'touchstart', 4);
  h.context.window.handlers.pagehide();
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]), home);
  h.send('touchstart', 700, 500, 5);
  assert.notDeepEqual(actionPositions(h).map(p => [p.x,p.y]), home);
  h.bounds.width = 600; h.bounds.height = 1000;
  h.context.resizeControls(); h.expect(0, 0);
  assert.deepEqual(actionPositions(h).map(p => [p.x,p.y]),
    actionPositions(harness(600,1000)).map(p => [p.x,p.y]));
}
for (const [width,height] of [[1000,600], [600,1000], [600,600]]) {
  for (const hand of [-1,0,1]) {
    const h = harness(width,height,hand);
    for (let ix=0; ix<=10; ++ix) for (let iy=0; iy<=10; ++iy) {
      const x=width*ix/10, y=height*iy/10;
      // These presses intentionally belong to zoom, so cannot relocate the stick.
      if ((hand<0 ? ix<=1 : ix>=9) && iy>=4 && iy<6) continue;
      h.send('touchstart',x,y);
      const buttons=actionPositions(h), R=Math.min(width,height)*.24;
      for (let i=0; i<buttons.length; ++i) {
        const p=buttons[i], eps=.01;
        assert.ok(p.x>=p.radius && p.x<=width-p.radius && p.y>=p.radius && p.y<=height-p.radius);
        assert.ok(Math.hypot(p.x-x,p.y-y)+eps>=R+p.radius, `stick overlap at ${width}x${height}/${hand}/${ix},${iy}`);
        const zx0=hand<0?0:width*.88, zx1=hand<0?width*.12:width;
        const dx=Math.max(zx0-p.x,p.x-zx1,0), dy=Math.max(height*.4-p.y,p.y-height*.6,0);
        assert.ok(Math.hypot(dx,dy)+eps>=p.radius);
        const pauseX=width*(hand<0?.125:.875), pauseY=height*.12;
        assert.ok(Math.hypot(p.x-pauseX,p.y-pauseY)+eps>=p.radius+Math.min(width,height)*.19*.62*.5);
        for (const q of buttons.slice(i+1)) assert.ok(Math.hypot(p.x-q.x,p.y-q.y)+eps>=p.radius+q.radius);
      }
      h.context._resetTouchGestures();
    }
  }
}

// Secondary/boost/teleport pin the base while held, then give a full
// second to return. Expiry, resets and ordinary release timing still work.
for (const cls of ['touch-mine', 'touch-boost', 'touch-teleport']) {
  for (const scenario of ['return', 'expire', 'reset', 'cancel']) {
    const h = harness();
    h.send('touchstart',400,500); h.send('touchmove',440,460);
    h.send('touchend'); h.advance(600);
    h.button(cls,'touchstart',2); h.advance(1500); h.expect(0,0);
    if (scenario === 'reset') h.context.window.handlers.pagehide();
    h.button(cls,scenario === 'cancel' ? 'touchcancel' : 'touchend',2);
    h.advance(scenario === 'return' ? 999 : scenario === 'expire' ? 1000 : 10);
    h.send('touchstart',430,470,3);
    if (scenario === 'return') {
      h.expect(30/r,-30/r);
      assert.match(h.context._joyPlaceholderEls[0].style.cssText,/left:400px;top:500px/);
      h.send('touchend',430,470,3); h.advance(500);
      h.send('touchstart',450,480,4); h.expect(0,0);
    } else h.expect(0,0);
  }
}

console.log('touch_one_hand_web: all checks passed');
