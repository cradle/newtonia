// Exercise the compiled production collector without contacting Google.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const source = require('./web_test_source.cjs')();
const start = source.indexOf('function createControlAnalytics(');
const end = source.indexOf('// END control analytics', start);
assert.ok(start >= 0 && end > start, 'production collector slice markers missing');
const code = source.slice(start, end);
function harness() {
  const wh = {}, dh = {}, events = [];
  let now = 0, playing = true, focus = true, timer;
  const w = {newtoniaAnalyticsReady:true, dataLayer: [], gtag: (...args) => events.push(args),
    addEventListener: (n, f) => wh[n] = f, setInterval(f, ms) {assert.equal(ms,250); timer=f;}};
  const d = {hidden:false, hasFocus:()=>focus, addEventListener:(n,f)=>dh[n]=f};
  const c = {window:w,document:d,navigator:{getGamepads:()=>[]},performance:{now:()=>now},query:k=>!playing ? -1 : ({32:2,119:1,120:4,101:8,116:16,112:32}[k] || 0)};
  vm.createContext(c); vm.runInContext(code+'\nthis.collector=createControlAnalytics(k => query(k));',c);
  const key=(name, extra={})=>wh.keydown({key:name,isTrusted:true,repeat:false,...extra});
  return {c,w,d,wh,dh,events,key, playing(v){playing=v;}, focus(v){focus=v;},
    tick(ms){now+=ms;timer();}, release(k){wh.keyup({key:k,isTrusted:true});}};
}
let h=harness();
h.key(' '); for(let i=0;i<10000;i++) h.key(' ',{repeat:true});
assert.equal(h.events.length,0); h.tick(29999); assert.equal(h.events.length,0);
h.tick(1); assert.equal(h.events.length,2); assert.equal(h.events[0][1],'game_controls_used');
assert.equal(h.events[1][2].fire,1); assert.equal(h.events[1][2].keyboard_presses,1);
h.tick(30000); assert.equal(h.events.length,2);
h.release(' '); h.key(' '); h.d.hidden=true; h.dh.visibilitychange(); h.wh.pagehide();
assert.equal(h.events.length,3); assert.equal(h.events[2][1],'game_controls_summary');
h=harness(); h.playing(false); h.key('w'); h.release('w'); h.playing(true);
h.focus(false); h.key('x'); h.focus(true); h.key('q'); h.key('Secret'); h.key('e',{ctrlKey:true});
h.tick(30000); assert.equal(h.events.length,0);
h.key(' ',{isTrusted:false}); h.c.collector.joystick(.5,0);
for(let i=0;i<10000;i++) h.c.collector.joystick(.6,0);
h.c.collector.joystick(0,0); h.c.collector.joystick(.5,0); h.tick(30000);
assert.equal(h.events[1][2].touch_presses,3); assert.equal(h.events[1][2].move,2);
h=harness(); h.c.navigator.getGamepads=()=>[{connected:true,buttons:[{pressed:true}],axes:[0]}];
h.tick(250); h.tick(30000); assert.equal(h.events[1][2].gamepad_active_samples,1);
h.c.navigator.getGamepads=()=>{throw Error('denied');}; assert.doesNotThrow(()=>h.tick(30000));
h=harness(); h.w.gtag=()=>{throw Error('blocked');}; h.key(' '); assert.doesNotThrow(()=>h.tick(30000));
h=harness(); h.w.newtoniaAnalyticsReady=false; h.key(' '); h.tick(30000); assert.equal(h.events.length,0);
h.w.newtoniaAnalyticsReady=true; h.tick(30000); assert.equal(h.events.length,0); // dropped, not retried
console.log('web analytics: batching, repeat suppression, lifecycle, privacy, touch and gamepad tests passed');

// Summaries flush even after game-over or loss of focus, without new sampling.
for (const gate of ['playing','focus']) {
  h=harness(); h.key(' '); h[gate](false); h.tick(30000);
  assert.equal(h.events[1][2].fire,1);
}
// Unknown mappings often expose idle trigger axes at -1: do not count them.
h=harness(); h.c.navigator.getGamepads=()=>[{connected:true,mapping:'',buttons:[],axes:[0,0,-1,-1]}];
h.tick(250); h.tick(30000); assert.equal(h.events.length,0);
h.c.navigator.getGamepads=()=>[{connected:true,mapping:'standard',buttons:[],axes:[.5,0,0,0]}];
h.tick(250); h.tick(30000); assert.equal(h.events[1][2].gamepad_active_samples,1);
// Healthy loaded tags must keep recording regardless of dataLayer history.
h=harness(); h.w.dataLayer.length=2000; h.key(' '); h.tick(30000); assert.equal(h.events.length,2);
// Live bridge mappings can change without modifying the JS key table (P2/remaps).
h=harness(); h.c.query=k=>k===122 ? 8 : 0; h.key('z'); h.tick(30000);
assert.equal(h.events[1][2].boost,1);
console.log('review regressions passed');

h=harness(); h.c.query=()=>{throw Error('runtime not ready');};
assert.doesNotThrow(()=>h.key(' ')); assert.doesNotThrow(()=>h.tick(30000));
assert.equal(h.events.length,0);

// Latin-1 characters must not alias special keys, but real arrows still work.
h=harness(); h.c.query=k=>k>=228 && k<=231 ? 1 : 0;
for (const key of ['ä','å','æ','ç']) h.key(key);
h.tick(30000); assert.equal(h.events.length,0);
h.key('ArrowLeft'); h.tick(30000); assert.equal(h.events[1][2].move,1);
// Deferred keyup does not collapse touch taps; taking over a hold is not a tap.
h=harness();
for(let i=0;i<5;i++) h.key(' ',{isTrusted:false});
h.key(' ',{isTrusted:false,newtoniaControlContinuation:true});
h.tick(30000); assert.equal(h.events[1][2].fire,5);
assert.equal(h.events[1][2].touch_presses,5);
// Duplicate physical keydown without repeat is still only one press.
h=harness(); h.key(' '); h.key(' '); h.tick(30000);
assert.equal(h.events[1][2].keyboard_presses,1);
console.log('second-review regressions passed');
