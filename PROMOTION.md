# Newtonia — self-promotion plan

Working doc for marketing Newtonia, aimed mainly at indie subreddits.
Written 2026-08-05 against live data pulled from the Steam API, the board
worker and the public site.

---

## 1. Where we actually stand

Facts, not vibes — all verified 2026-08-05:

| Thing | State |
|---|---|
| Steam | Live since **9 Apr 2026**, **$0.99**, 5 screenshots, 1 trailer |
| Steam reviews | **Zero** (`recommendations: null`) after ~4 months |
| Steam short description | "A retro 2D top-down space-shooter." |
| Steam `website` field | **Not set** — store page doesn't link the site |
| Free web build | Live, `newtonia.metonymous.com/play`, **netplay force-disabled** |
| itch.io | Free netless project + purchase-gated `newtonia-online` |
| iOS | Live on the App Store |
| Android | **Internal beta, email-gated** |
| Leaderboard | Live, season `s1`, **3 solo rows + 3 co-op rows**, mostly testers |

Read that table honestly and the conclusion is: **the game shipped and then
nobody came.** That is a distribution problem, not a game problem. Everything
below is built around that.

### The one number that matters

**Zero Steam reviews.** Steam does not show a review score until 10 reviews,
and the discovery algorithm gives a no-review product almost nothing. At
$0.99 with no score, the store page converts badly no matter how much traffic
it gets. Getting to **10 reviews** is the highest-leverage goal on this list,
and it's a small, reachable number.

### The strategic consequence

Do **not** point cold Reddit traffic at the Steam page. A $0.99 page with no
reviews and a one-line description is a leaky bucket.

Point it at the **free browser build**. That is the strongest asset we have
and it's currently underused:

```
Reddit  →  free web build (zero friction, no install, no signup)
              ↓  they already like it
           Steam / iOS  (online co-op, achievements, cloud saves)
              ↓
           reviews  →  algorithm  →  organic traffic
```

Nobody buys a $0.99 unknown from a screenshot. Plenty of people click a free
browser game, and *some* of those buy afterwards. Sell the click, not the game.

---

## 2. Fix the shop window before driving any traffic

These are cheap and they multiply everything else. Do them first — a week of
posting into a broken funnel is a week wasted.

1. **Rewrite the Steam short description.** "A retro 2D top-down space-shooter"
   is the exact sentence a scroller uses to dismiss it. It's doing the reader's
   dismissing for them. Lead with what isn't Asteroids:
   > Newtonian drift, a lance that mirror-bounces off reflective asteroids,
   > black holes that bend light, and split-screen or online co-op. 20+ levels
   > that keep adding new ways to die.
2. **Expand the long description.** Two sentences is not a store page. Cover
   weapons, hazards, co-op, the leaderboard, the platform spread.
3. **Set the Steam `website` field** to `newtonia.metonymous.com`.
4. **Put a "Play free in your browser" link on the Steam page** (in the
   description). Counterintuitive, but a free demo path raises wishlists and
   the eventual purchase rate far more than it cannibalises a $0.99 sale.
5. **Ask for reviews in-game.** After a good run on Steam — say, clearing
   level 10 — a single unobtrusive "enjoying it? a review really helps"
   line. This is the fastest legitimate route to 10 reviews.
6. **Fix the Android gate.** `mailto:` for beta access kills every Android
   visitor. Either open the Play track or drop Android from the platform
   list until it's open — r/AndroidGaming is off the table until then.

---

## 3. The asset we're missing: GIFs

I looked at `shots/out/steam3_level5.png` and `steam5_level20.png`. They are
clean and on-brand, and as *marketing* they are weak: black background, grey
polygons, thin white lines. A still reads as "Asteroids clone" in about half a
second, which is the exact reaction that has to be pre-empted.

The game's appeal is **motion** — drift, trails, beams, shockwaves, lensing.
None of it survives a screenshot. **Static images will not carry these posts.**

Reddit also ranks native uploads well above external links, so these should be
uploaded directly to Reddit as GIF/video, with the play link in the body or
first comment.

### The capture harness — built, see `shots/clips/`

`shots/gif.sh` renders composed scenes straight to looping MP4 + GIF, headless
and deterministically (`shots/README.md` → Clips). Four usable clips exist
today, ordered by how well they answer "why isn't this just Asteroids?":

1. **Lance mirror-bounce** (`lance.shot`) — one pulse reflecting off a mirror
   asteroid and killing a chain of rocks around the corner. **The strongest
   asset we have.** Lead with it.
2. **Pulsar shockwave** (`pulsar.shot`) — a big amber ring expanding out and
   physically shoving the ship.
3. **Shock chain lightning** (`shock.shot`) — the arc snaking rock to rock,
   stopped dead by a tough one.
4. **Plain gameplay** (`gameplay.shot`) — a real generation-5 level, flown and
   shot. The "what is it actually like" clip that most feeds want.

**Correction to an earlier draft of this plan:** it called the invisible
asteroids "the best single hook we have". Having actually rendered them, that
was wrong — it was a judgement made from reading the code rather than looking
at output. The lens is a screen-space warp with no outline of its own; at GIF
resolution it is close to invisible in the unhelpful sense. The black hole is
worse: it draws *nothing* in the world view (its ring is minimap-only), so a
clip shows only rocks curving into nowhere. Both scenes are kept and
documented, neither is a lead asset.

If the invisible asteroids are ever to earn a post, it needs the **reveal**,
not the lens: empty space, a shot fired into it, something unseen breaking
into visible fragments. That's a story in three seconds. It needs more
scripting than the current scenes do.

Still worth capturing when there's an appetite:

- **Nova detonation** — full-screen wipe at 9 charges.
- **Split-screen co-op + revive** — one ship dies, the partner grabs the green
  cross and brings them back. Sells the couch-co-op angle in one loop, and no
  current clip shows two players at all.

---

## 4. Subreddit plan

> **Rules caveat:** Reddit blocks automated fetching, so I could not verify
> current sidebars. Every sub below has changed its self-promo rules at some
> point. **Read the sidebar and the removal reasons before each post.** A
> single rule-break can cost the domain (see §7).

### Tier 1 — direct posts welcome, go here first

| Sub | Why | Angle |
|---|---|---|
| **r/WebGames** | Exactly its purpose: free, instant, browser | Best single fit. Link the play URL. |
| **r/playmygame** | Built for this; has a **required template** | Follow the format exactly or it's removed |
| **r/indiegames** | Showcase-friendly, "show don't sell" | Lead with a GIF, not a pitch |
| **r/IndieGaming** | Large, tolerant of dev posts | Same GIF, different title |
| **r/IndieDev** | Dev-side, feedback-friendly | The "no engine, 7 platforms" story |
| **r/DestroyMyGame** | Brutal critique | Counterintuitively great: real feedback + real eyeballs, and the sub *likes* people who can take it |

### Tier 2 — genre and audience match

- **r/SteamDeck** — only once Deck-verified. There's real Deck touch work in
  the codebase (`glut.cpp` XI2 listener); note the portal touch setting is
  required. Don't post until it's actually verified — the sub will check.
- **r/CoOpGaming**, **r/localmultiplayergames** — split-screen + revive angle.
- **r/iosgaming** — App Store build.
- **r/Games**, **r/pcgaming** — effectively closed to self-promo. Skip.

### Tier 3 — programmer subs (different framing, high ceiling)

These want a **technical writeup that happens to have a game attached**, never
a pitch. The tech here is genuinely interesting and under-sold:

- **r/cpp** — C++11, SDL2, OpenGL, no engine, one source tree → desktop, iOS,
  Android, WASM, Xbox GDK.
- **r/WebAssembly** — the Emscripten port.
- **Hacker News, "Show HN"** — best fit for the full stack story: P2P WebRTC
  netplay, Cloudflare Workers signaling, and a **replay-verified leaderboard**
  (snapshot-stream replays, seasons). That anti-cheat design is a better HN
  story than the game itself.
- **r/gamedev** — self-promo only in Screenshot Saturday / Feedback Friday
  threads. Respect it.

### Cadence

Spread over ~6 weeks. Never blast the same asset to five subs in one day —
that is the pattern spam filters catch.

| Week | Post | Asset |
|---|---|---|
| 0 | *(no posting)* — do §2 store fixes | clips are already rendered |
| 1 | r/WebGames | `gameplay.mp4` |
| 1 | r/playmygame | Template + `gameplay.mp4` |
| 2 | r/indiegames | `lance.mp4` — the strongest clip, to the biggest showcase sub |
| 2 | r/IndieDev | "No engine, 7 platforms" + screenshots |
| 3 | r/DestroyMyGame | Trailer or 60s raw gameplay |
| 3 | r/IndieGaming | `shock.mp4` |
| 4 | r/cpp | Technical writeup |
| 4 | r/CoOpGaming | Split-screen revive clip *(needs capturing)* |
| 5 | Show HN | Netplay + replay-verified board writeup |
| 6 | r/SteamDeck *(if verified)* | Deck footage |

---

## 5. Ready-to-post drafts

Reddit detects marketing voice instantly. All of these are first person,
plain, specific, and slightly self-deprecating. Keep it that way.

### r/WebGames

> **Title:** Newtonia — free browser space shooter with Newtonian drift, no
> braking, and a wrapping world
>
> Been building this on and off for a while. It's a top-down shooter with
> proper Newtonian movement — you thrust and drift, there's no braking, and
> the world wraps.
>
> Each level adds a new way to die: reflective asteroids that bounce your
> bullets back, ones that teleport, quantum ones that only go solid when
> you're looking, invisible ones you can only spot as a lens distortion.
> Later there are black holes, pulsars that shove you around with shockwaves,
> and stations that deploy hunting ships.
>
> Free, no install, no signup: https://newtonia.metonymous.com/play/
>
> Runs on WebAssembly, works on phones too. Local split-screen if you've got
> a second keyboard. Happy to answer anything.

### r/indiegames — "show, don't sell"

> **Title:** My lance weapon ray-marches and mirror-bounces off reflective
> asteroids — took three rewrites before the reflections stopped lying
>
> *(GIF: lance pulse bouncing round a corner, killing a chain of rocks)*
>
> One instantaneous full-length pulse per trigger pull. It marches the line
> through the collision grid, kills everything killable it touches, and
> reflects off anything that reflects bullets — carrying the remaining
> distance with it. Plain invincible rocks stop it dead.
>
> The bug that took longest: the reflected segment could kill *you*, but only
> after a bounce. Took me embarrassingly long to work out that was correct
> behaviour and I should keep it.
>
> Whole game's a C++ space shooter, free in the browser if you want a look.

### r/IndieDev

> **Title:** I wrote a space shooter in C++ with no engine — same source tree
> now builds for Windows, macOS, Linux, iOS, Android, WebAssembly and Xbox
>
> No Unity, no Godot, no Unreal. C++11, SDL2 and OpenGL, with an ES2
> compatibility layer for mobile and web.
>
> Things I did not expect going in:
> - The single hardest platform was **the web**, not consoles.
> - `lineWidth > 1` silently does nothing on WebGL, macOS core GL *and*
>   ANGLE, so wide lines had to be emulated with CPU quad expansion — which
>   then became the dominant frame cost on real mobile GPUs (~44ms/frame)
>   until I detected native support and bypassed it.
> - Windows headers still `#define near` and `far`. A `std::vector` called
>   `near` failed with an error mentioning neither the variable nor the
>   cause. Got me twice.
>
> Free browser build if you want to poke at it. Happy to talk about any of
> the platform work.

### r/DestroyMyGame

> **Title:** Destroy my Asteroids-descendant space shooter — free in the
> browser, be as brutal as you like
>
> Genuinely want the harsh version. I've been staring at this too long to
> see it.
>
> The thing I already suspect and want confirmed or denied: does it read as
> "just another Asteroids clone" in the first ten seconds? If so, what's the
> first thing that should change?
>
> https://newtonia.metonymous.com/play/

### r/cpp

> **Title:** Newtonia: a C++11 game targeting desktop, iOS, Android,
> WebAssembly and Xbox GDK from one source tree
>
> Space shooter, SDL2 + OpenGL, no engine. A few things that might be of
> interest here rather than in a gamedev sub:
>
> - **One GL abstraction, five backends.** Desktop GL 3.3 core vs GLES2 for
>   mobile/web/Xbox, behind compile-time compatibility headers.
> - **Netplay is P2P WebRTC** via libdatachannel, with a Cloudflare Worker
>   doing signaling. The same code path drives the native and Emscripten
>   builds.
> - **Replays are a snapshot stream, not an input log.** Deliberate: an input
>   log is invalidated by any balance change and diverges silently, whereas a
>   snapshot stream is recorded *outcomes* — an old replay correctly shows the
>   old balance. Playback is just the net client reading a file instead of a
>   socket, so it reuses machinery that was already hardened.
> - That property is what makes the leaderboard verifiable server-side.
>
> Source and build instructions in the repo. Free WASM build in the browser.

### Show HN

> **Title:** Show HN: Newtonia – a C++/WASM space shooter with P2P netplay and
> replay-verified leaderboards
>
> Lead the body with the **replay-verification design**, not the game. That's
> the part an HN audience will actually argue about: scores are only trusted
> because the server can replay the run, and replays are snapshot streams
> rather than input logs so they survive balance changes.

---

## 6. Post mechanics that decide whether it lives or dies

Small things, disproportionate effect:

- **Upload media natively.** Reddit demotes external links hard. GIF/video in
  the post, URL in the body or first comment.
- **The first two hours decide everything.** Early velocity drives ranking.
  Be at the keyboard and reply to every single comment.
- **Account age and karma.** A fresh account posting links gets auto-filtered
  before a human ever sees it. If the account is new, spend a couple of weeks
  genuinely participating first. This is not optional.
- **Never reuse a title across subs.** Different framing per audience — the
  drafts above deliberately share no title.
- **Post timing:** weekday mornings US Eastern is the usual sweet spot, since
  that's when both US and EU are awake.
- **Answer "why $0.99?" honestly.** Someone will ask. "The browser version is
  free and complete; the paid builds add online co-op, achievements and cloud
  saves" is a good, true answer.
- **The empty leaderboard is a hook, not a liability** — if framed right.
  "#2 solo is 33k, the board's wide open" invites people in. Don't link a
  6-row board as though it were a bustling scene; frame it as land-grab.

---

## 7. What not to do

- **Don't spam.** Reddit can **domain-ban** `newtonia.metonymous.com`
  site-wide. That is close to unrecoverable and it is the single worst
  outcome available here. Everything else on this list is reversible.
- **Don't use alt accounts to upvote or comment.** Vote manipulation is
  detected, and it takes the domain down with the account.
- **Don't buy upvotes or reviews.** Steam review manipulation risks delisting.
- **Don't argue with negative feedback.** Especially in r/DestroyMyGame —
  thank them, ask a follow-up. Lurkers read the replies and judge the dev.
- **Don't cross-post the identical asset the same day.** That's the exact
  fingerprint spam filters look for.

---

## 8. What to measure

GA4 is already on the site (`G-03BDC6CK12`). Per post, track:

- Sessions on `/play/` and the referrer
- **Play-rate**: sessions that actually start a game vs bounce
- Steam page visits → wishlists → sales (Steamworks UTM tags per post)
- **Runs submitted to the board** — the truest engagement signal we have, and
  right now it's ~6 total, so any movement is legible
- Steam reviews, tracked against the target of 10

If a sub sends traffic that bounces without playing, the problem is the
landing page, not the sub. If they play but don't buy, the problem is the
store page. Those are different fixes — the numbers tell you which.

---

## 9. Honest summary

The game is finished, broadly featured and on five platforms. What it has
never had is an audience. The bottleneck is not more features — it's:

1. Nobody knows it exists.
2. The store page doesn't sell it to the few who find it.
3. Zero reviews means Steam won't help.

Fix (2), then use the free browser build to attack (1), and (3) follows from
the first two. Ten reviews is the realistic near-term target, and it changes
the shape of everything after it.
