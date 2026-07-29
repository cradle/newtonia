// Regression driver for the best-promotion stack overflow (2026-07-29).
//
// A CLEAN run — no level skips, so not cheat-flagged — abandoned and then
// discarded with NEW GAME. That rotation runs maybe_promote_best, whose
// copy_file was the only routine caller: with a 64 KB stack buffer on
// emscripten's 64 KB stack it aborted the wasm module every time, which
// reached players as "index out of bounds" / "Aborted()" in the main loop.
//
// The cheat flag is why this hid: every skip-level soak run is flagged, and
// promotion returns early on those, so only ordinary play reaches the copy.
//
// Run against a served web build (TESTING.md section 8):
//   CHROME=/opt/pw-browsers/chromium-*/chrome-linux/chrome \
//     node test/e2e/web_replay_promote.mjs
// Build with -sASSERTIONS=2 to have a stack overflow name itself.
import { chromium } from 'playwright';

const ctx = await chromium.launchPersistentContext('/tmp/pw_promote', {
  headless: true, ...(process.env.CHROME ? { executablePath: process.env.CHROME } : {}),
  args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader'],
});
const page = await ctx.newPage();
const bad = [];
page.on('console', m => { const t = m.text();
  if (/replay:|abort|Abort|RuntimeError|out of bounds|stack|Assertion|SAFE_HEAP/i.test(t)) {
    console.log('  ' + t.slice(0, 260));
    if (/abort|RuntimeError|out of bounds|stack overflow|Assertion|SAFE_HEAP/i.test(t)) bad.push(t);
  }});
page.on('pageerror', e => { console.log('  PAGEERROR ' + e.message.slice(0, 260)); bad.push(e.message); });
await page.goto('http://127.0.0.1:8099/index.html', { waitUntil: 'load' });
await page.waitForTimeout(12000);
const key = async (k, h = 60) => { await page.keyboard.down(k); await page.waitForTimeout(h); await page.keyboard.up(k); };
const play = async ms => { const t = Date.now() + ms;
  while (Date.now() < t) { await key('w', 200); await key('a', 90); await key(' ', 70); } };

await key('Enter'); await page.waitForTimeout(800);
await key('Enter'); await page.waitForTimeout(2500);
console.log('clean run (no level skips, so not cheat-flagged)');
await play(20000);
await key('Escape'); await page.waitForTimeout(2500);   // clean abandon: header patched

console.log('menu -> NEW GAME (rotation + best promotion)');
await key('Enter'); await page.waitForTimeout(1200);   // dismiss the attract screen
await page.screenshot({ path: '/tmp/promote-menu.png' });
await key('ArrowDown'); await page.waitForTimeout(500);
await page.screenshot({ path: '/tmp/promote-menu-sel.png' });
await key('Enter'); await page.waitForTimeout(1500);
await page.screenshot({ path: '/tmp/promote-confirm.png' });
await key('ArrowUp'); await page.waitForTimeout(500);     // YES sits above NO
await key('Enter'); await page.waitForTimeout(4000);
await page.screenshot({ path: '/tmp/promote-after.png' });
await play(6000);
console.log(bad.length ? `WEB-PROMOTE-FAIL: ${bad[0].slice(0,200)}` : 'WEB-PROMOTE-OK');
await ctx.close(); process.exit(0);
