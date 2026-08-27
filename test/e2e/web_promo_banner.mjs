// Promo-funnel driver: the site leaderboard's would-place bar, the web
// game's game-over banner, and /join's store routing, per device.
//
// Guards two field-classes of bug:
//  - the [hidden]-vs-author-display trap (2026-08-27): .you-banner sets
//    display:flex, which silently beats the UA's [hidden] rule, so the
//    empty gold bar + COMPETE CTA drew on every plain visit. The page
//    carries .you-banner[hidden]/a.store-cta[hidden] guards; this driver
//    asserts COMPUTED visibility, never the attribute.
//  - store routing drift: store ids + the ANDROID_PUBLIC flag live once
//    in web/site/store_route.js, consumed by three surfaces. Each is
//    checked under desktop / iPhone / Android user agents, including the
//    Android no-store-button cases while the Play listing is closed.
//
// Needs NO emcc build: serves web/site/ directly and compiles main.ts
// with tsc if web/main.js is missing or stale (the game banner is plain
// DOM). Run from the repo root (playwright resolves from the script's
// directory upward — TESTING.md section 8):
//   npm install playwright --no-save
//   CHROME=/opt/pw-browsers/chromium-*/chrome-linux/chrome \
//     node test/e2e/web_promo_banner.mjs
import { chromium } from 'playwright';
import { createServer } from 'http';
import { readFile, stat } from 'fs/promises';
import { spawnSync } from 'child_process';
import { dirname, extname, join } from 'path';
import { fileURLToPath } from 'url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const SITE = join(ROOT, 'web', 'site');
const MAIN_JS = join(ROOT, 'web', 'main.js');

// Compile main.ts when main.js is missing or older (tsc is the same step
// `make web` runs; failing that, say what to run rather than testing a
// stale banner).
const mtime = async p => { try { return (await stat(p)).mtimeMs; } catch { return -1; } };
if (await mtime(MAIN_JS) < await mtime(join(ROOT, 'web', 'main.ts'))) {
  const r = spawnSync('tsc', ['-p', 'web/tsconfig.json'], { cwd: ROOT, stdio: 'inherit' });
  if (r.status !== 0)
    { console.log('WEB-PROMO-FAIL: tsc -p web/tsconfig.json failed (or tsc missing)'); process.exit(1); }
}

// Stub of shell.html's banner-relevant skeleton: the elements main.js
// grabs at load, store_route.js before main.js (shell.html's order).
const STUB = `<!doctype html><html><body>
  <div id="game-container"><canvas id="canvas" tabindex="0"></canvas>
    <div id="touch-controls"></div></div>
  <button id="fullscreen-btn"></button><button id="mute-btn"></button>
  <script>var Module = { setStatus: function(){} };</script>
  <script src="/store_route.js"></script>
  <script src="/game-main.js"></script></body></html>`;

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.png': 'image/png' };
const srv = createServer(async (req, res) => {
  let p = req.url.split('?')[0];
  if (p === '/game/') { res.writeHead(200, { 'content-type': 'text/html' }); return res.end(STUB); }
  if (p === '/game-main.js') {
    res.writeHead(200, { 'content-type': 'text/javascript' });
    return res.end(await readFile(MAIN_JS));
  }
  if (p.endsWith('/')) p += 'index.html';
  try {
    const body = await readFile(join(SITE, p));
    res.writeHead(200, { 'content-type': MIME[extname(p)] || 'text/plain' });
    res.end(body);
  } catch { res.writeHead(404); res.end(); }
});
await new Promise(r => srv.listen(8123, r));

// Two known rows so a handed-over 50000 places #2 and 10 places #3.
const SNAP = {
  generated_at: 1756200000000,
  boards: [{ players: 1, season: 's1', live: true, rows: [
    { rank: 1, name: 'ACE', platform: 2, score: 90000, generation: 9, duration_ms: 900000, date: 1756100000000, verified: true, has_replay: false },
    { rank: 2, name: 'BAKER', platform: 4, score: 40000, generation: 6, duration_ms: 600000, date: 1756100000000, verified: true, has_replay: false },
  ] }],
};

const UAS = {
  desktop: undefined,   // playwright's default desktop Chrome UA
  ios: 'Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 Mobile/15E148 Safari/604.1',
  android: 'Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36',
};

const browser = await chromium.launch({
  headless: true, ...(process.env.CHROME ? { executablePath: process.env.CHROME } : {}),
});
const failures = [];
const check = (label, cond, detail) => {
  console.log(`${cond ? 'ok  ' : 'FAIL'} ${label}${cond ? '' : ' — ' + detail}`);
  if (!cond) failures.push(label);
};

for (const [device, userAgent] of Object.entries(UAS)) {
  const ctx = await browser.newContext(userAgent ? { userAgent } : {});
  const page = await ctx.newPage();
  const errs = [];
  page.on('pageerror', e => errs.push(e.message));
  await page.route('**/site/leaderboard.json', r => r.fulfill({ json: SNAP, headers: { 'access-control-allow-origin': '*' } }));
  await page.route('**googletagmanager.com/**', r => r.abort());

  // ---- site leaderboard: bar only with a run to place ----
  await page.goto('http://127.0.0.1:8123/leaderboard/', { waitUntil: 'load' });
  await page.waitForTimeout(300);
  check(`${device}: plain visit hides bar`, !(await page.locator('#you-banner').isVisible()), 'bar visible');
  await page.goto('http://127.0.0.1:8123/leaderboard/?score=banana&players=1', { waitUntil: 'load' });
  await page.waitForTimeout(300);
  check(`${device}: bogus score hides bar`, !(await page.locator('#you-banner').isVisible()), 'bar visible');

  await page.goto('http://127.0.0.1:8123/leaderboard/?score=50000&players=1', { waitUntil: 'load' });
  await page.waitForTimeout(300);
  const barVis = await page.locator('#you-banner').isVisible();
  const barText = await page.locator('#you-banner-text').textContent();
  check(`${device}: score shows bar`, barVis && /WOULD PLACE #2/.test(barText || ''), `vis=${barVis} text=${barText}`);
  const cta = page.locator('#you-banner-cta');
  const ctaVis = await cta.isVisible();
  const ctaText = (await cta.textContent()) || '';
  const ctaHref = (await cta.getAttribute('href')) || '';
  if (device === 'desktop')
    check('desktop: CTA -> Steam', ctaVis && /COMPETE ON STEAM/.test(ctaText) &&
        ctaHref.includes('store.steampowered.com') && ctaHref.includes('utm_campaign=lb_would_place'),
        `vis=${ctaVis} text=${ctaText} href=${ctaHref}`);
  if (device === 'ios')
    check('ios: CTA -> App Store', ctaVis && /COMPETE ON IOS/.test(ctaText) &&
        ctaHref.includes('apps.apple.com/app/id6760685759'), `vis=${ctaVis} text=${ctaText} href=${ctaHref}`);
  if (device === 'android')
    check('android: CTA hidden (listing closed), text still shows', !ctaVis, 'CTA visible');

  // ---- game-over banner (compiled main.js over the stub page) ----
  await page.goto('http://127.0.0.1:8123/game/', { waitUntil: 'load' });
  await page.evaluate(() => window.newtGameOver(12345, 1));
  const rank = page.locator('#go-banner a').first();
  const rankText = (await rank.textContent()) || '';
  const rankHref = (await rank.getAttribute('href')) || '';
  check(`${device}: rank link`, /SCORE 12,345/.test(rankText) && rankHref.includes('?score=12345&players=1'),
      `text=${rankText} href=${rankHref}`);
  const storeCount = await page.locator('#go-banner a.go-store').count();
  if (device === 'android') {
    check('android: game banner has no store link', storeCount === 0, `count=${storeCount}`);
  } else {
    const sText = (await page.locator('#go-banner a.go-store').textContent()) || '';
    const sHref = (await page.locator('#go-banner a.go-store').getAttribute('href')) || '';
    if (device === 'desktop')
      check('desktop: game banner -> Steam', /GET IT ON STEAM/.test(sText) &&
          sHref.includes('store.steampowered.com') && sHref.includes('utm_campaign=gameover'),
          `text=${sText} href=${sHref}`);
    if (device === 'ios')
      check('ios: game banner -> App Store', /GET IT ON THE APP STORE/.test(sText) &&
          sHref.includes('apps.apple.com/app/id6760685759'), `text=${sText} href=${sHref}`);
  }
  await page.locator('#go-banner button').click();
  check(`${device}: dismiss hides game banner`, !(await page.locator('#go-banner').isVisible()), 'still visible');

  // Zero score: the leaderboard page refuses to place 0 (?score= gate is
  // digits > 0), so the banner must not promise SEE WHERE YOU'D RANK.
  // With a store link the banner shows store-only; Android (no store
  // while the listing is closed) shows nothing at all.
  await page.evaluate(() => window.newtGameOver(0, 1));
  if (device === 'android') {
    check('android: zero score shows no banner', !(await page.locator('#go-banner').isVisible()), 'banner visible');
  } else {
    check(`${device}: zero score hides rank link, keeps store`,
        (await page.locator('#go-banner').isVisible()) &&
        !(await rank.isVisible()) &&
        (await page.locator('#go-banner a.go-store').isVisible()),
        'wrong visibility mix');
    // ...and a later scoring run brings the rank link back.
    await page.evaluate(() => window.newtGameOver(500, 1));
    check(`${device}: next scoring run restores rank link`, await rank.isVisible(), 'rank still hidden');
  }

  // ---- /join routing (same shared store_route.js) ----
  await page.goto('http://127.0.0.1:8123/join/?code=TESTROOM', { waitUntil: 'load' });
  check(`${device}: join shows the code`, (await page.locator('#code').textContent()) === 'TESTROOM', 'code missing');
  const steamVis = await page.locator('#steamBtn').isVisible();
  const appVis = await page.locator('#appStoreBtn').isVisible();
  const playVis = await page.locator('#playStoreBtn').isVisible();
  // The parked itch button must stay hidden on EVERY device (task #156);
  // it was visible everywhere until .hidden went !important — a.btn's
  // display:block outranked the utility class (2026-08-27).
  const itchVis = await page.locator('#itchBtn').isVisible();
  check(`${device}: join keeps itch parked`, !itchVis, 'itch visible');
  if (device === 'desktop')
    check('desktop: join -> Steam launch', steamVis && !appVis && !playVis &&
        (await page.locator('#steamBtn').getAttribute('href')) === 'steam://run/4536720//+connect%20TESTROOM',
        `steam=${steamVis} app=${appVis} play=${playVis}`);
  if (device === 'ios')
    check('ios: join -> App Store', appVis && !steamVis && !playVis, `steam=${steamVis} app=${appVis} play=${playVis}`);
  if (device === 'android')
    check('android: join -> needs-the-app (listing closed)', !steamVis && !appVis && !playVis &&
        (await page.locator('#sub').textContent()) === 'Joining needs the app',
        `steam=${steamVis} app=${appVis} play=${playVis} sub=${await page.locator('#sub').textContent()}`);

  check(`${device}: no page errors`, errs.length === 0, errs[0] || '');
  await ctx.close();
}

await browser.close(); srv.close();
console.log(failures.length ? `WEB-PROMO-FAIL (${failures.length})` : 'WEB-PROMO-OK');
process.exit(failures.length ? 1 : 0);
