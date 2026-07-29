// Reproduce the field test in a real browser: play into level 2, kill the tab,
// reopen, and report what actually reached IndexedDB.
//
// Uses a persistent context so IDBFS survives between "sessions", exactly as a
// returning player's profile does.
import { chromium } from 'playwright';

const URL = process.env.GAME_URL || 'http://127.0.0.1:8099/index.html';
const PROFILE = process.env.PROFILE || '/tmp/pwprofile';
const KILL = process.env.KILL || 'close';        // close | crash
const PLAY_MS = parseInt(process.env.PLAY_MS || '12000', 10);

const log = (...a) => console.log(...a);

async function newPage(ctx, tag) {
  const page = await ctx.newPage();
  page.on('console', m => {
    const t = m.text();
    if (/replay|newtonia|focus/i.test(t)) log(`  [${tag}] ${t}`);
  });
  await page.goto(URL, { waitUntil: 'load' });
  return page;
}

// Read the IDBFS store directly: every mounted file lives in object store
// FILE_DATA of a database named after the mount point.
async function dumpFiles(page) {
  return page.evaluate(async () => {
    const dbs = await indexedDB.databases();
    const out = [];
    for (const { name } of dbs) {
      const db = await new Promise((res, rej) => {
        const r = indexedDB.open(name);
        r.onsuccess = () => res(r.result);
        r.onerror = () => rej(r.error);
      });
      if (!db.objectStoreNames.contains('FILE_DATA')) { db.close(); continue; }
      const tx = db.transaction('FILE_DATA', 'readonly');
      const store = tx.objectStore('FILE_DATA');
      const keys = await new Promise(res => { const r = store.getAllKeys(); r.onsuccess = () => res(r.result); });
      const vals = await new Promise(res => { const r = store.getAll();     r.onsuccess = () => res(r.result); });
      keys.forEach((k, i) => {
        const v = vals[i];
        const size = v && v.contents ? v.contents.length : 0;
        if (String(k).includes('replay') || String(k).endsWith('.nrp') ||
            String(k).includes('savegame'))
          out.push({ db: name, path: String(k), size });
      });
      db.close();
    }
    return out;
  });
}

// CHROME lets a container point at a pre-installed browser whose build number
// does not match the npm playwright package (TESTING.md §8).
const ctx = await chromium.launchPersistentContext(PROFILE, {
  headless: true,
  ...(process.env.CHROME ? { executablePath: process.env.CHROME } : {}),
  args: ['--use-gl=swiftshader', '--enable-unsafe-swiftshader', '--disable-gpu-sandbox'],
});

log(`session 1: load, start a run, skip a level, play ${PLAY_MS / 1000}s, then ${KILL}`);
let page = await newPage(ctx, 's1');
await page.waitForTimeout(9000);              // wasm + IDBFS + audio decode

const key = async (k, hold = 60) => {
  await page.keyboard.down(k); await page.waitForTimeout(hold); await page.keyboard.up(k);
};

await key('Enter'); await page.waitForTimeout(800);   // attract -> menu
await key('Enter'); await page.waitForTimeout(2500);  // NEW GAME / CONTINUE
await key('n');     await page.waitForTimeout(2500);  // skip level 1 -> generation 2
// Play inside level 2 so there are records banked but no boundary flush.
const until = Date.now() + PLAY_MS;
while (Date.now() < until) {
  await key('w', 250); await key('a', 120); await key(' ', 80);
}
// EXPLICIT=wait|nowait: call the hook by hand to separate "the event never
// fired" from "the async FS.syncfs never landed".
const mode = process.env.EXPLICIT || 'none';
if (mode !== 'none') {
  await page.evaluate(() => { window.Module._web_focus_lost(); });
  log(`  called _web_focus_lost() directly (${mode})`);
  if (mode === 'wait') await page.waitForTimeout(2500);
}
log('  banked records in level 2; killing the tab now');

if (KILL === 'crash') {
  await page.evaluate(() => { window.location.href = 'about:blank'; }).catch(() => {});
} else {
  await page.close({ runBeforeUnload: false });   // hard close, no unload grace
}

log('session 2: reopen and read IndexedDB');
page = await newPage(ctx, 's2');
await page.waitForTimeout(9000);
const files = await dumpFiles(page);
for (const f of files) log(`  ${f.path}  ${f.size} bytes`);
// EXPORT_NRP: pull the bytes out so replay_check.py can say what is in it.
const bytes = await page.evaluate(async () => {
  const db = await new Promise(res => { const r = indexedDB.open('/libsdl/cc.gfm/newtonia'); r.onsuccess = () => res(r.result); });
  const store = db.transaction('FILE_DATA', 'readonly').objectStore('FILE_DATA');
  const v = await new Promise(res => { const r = store.get('/libsdl/cc.gfm/newtonia/replays/current.nrp'); r.onsuccess = () => res(r.result); });
  db.close();
  return v && v.contents ? Array.from(v.contents) : null;
});
if (bytes) {
  const fs = await import('fs');
  fs.writeFileSync(process.env.NRP_OUT || '/tmp/web_current.nrp', Buffer.from(bytes));
  log(`  wrote ${bytes.length} bytes -> ${process.env.NRP_OUT || '/tmp/web_current.nrp'}`);
}
const cur = files.find(f => f.path.endsWith('current.nrp'));
log(cur ? `RESULT current.nrp = ${cur.size} bytes (64 = header only, nothing recorded)`
        : 'RESULT no current.nrp in IndexedDB at all');

await ctx.close();
