// Exercise the compiled production collector without contacting Google.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');
execFileSync('tsc', ['-p', 'web/tsconfig.json']);
const source = fs.readFileSync('web/main.js', 'utf8');
const code = source.slice(source.indexOf('function createControlAnalytics('), source.indexOf('// END control analytics'));
function harness() {
  const wh = {}, dh = {}, events = [];
  let now = 0, playing = true, focus = true, timer;
  const w = {dataLayer: [], gtag: (...args) => events.push(args),
    addEventListener: (n, f) => wh[n] = f, setInterval(f, ms) {assert.equal(ms,250); timer=f;}};
  const d = {hidden:false, hasFocus:()=>focus, addEventListener:(n,f)=>dh[n]=f};
  const c = {window:w,document:d,navigator:{getGamepads:()=>[]},performance:{now:()=>now},isPlaying:()=>playing};
  vm.createContext(c); vm.runInContext(code+'\nthis.collector=createControlAnalytics(isPlaying);',c);
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
h.tick(30000); assert.equal(h.events[1][2].gamepad_active_samples,1);
h.c.navigator.getGamepads=()=>{throw Error('denied');}; assert.doesNotThrow(()=>h.tick(30000));
h=harness(); h.w.gtag=()=>{throw Error('blocked');}; h.key(' '); assert.doesNotThrow(()=>h.tick(30000));
h=harness(); h.w.dataLayer.length=1000; h.key(' '); h.tick(30000); assert.equal(h.events.length,0);
h.w.dataLayer.length=0; h.tick(30000); assert.equal(h.events.length,0); // dropped, not retried
console.log('web analytics: batching, repeat suppression, lifecycle, privacy, touch and gamepad tests passed');
