# Store pages — copy, assets and the conversion checklist

Ready-to-paste copy for the Steam and App Store listings, plus the reasoning
behind each choice. Like `steam/rich_presence.vdf`, nothing here is uploaded by
a workflow — it is pasted into the Steamworks / App Store Connect portals by
hand. Keep this file in sync when the portal text changes, so the two never
drift.

Baseline measured 2026-07-28 (Steam app 4536720, App Store id6760685759):

| | Steam | App Store |
|---|---|---|
| Price | $0.99 | $1.99 |
| Short description / subtitle | 34 chars, no hook | none set |
| Long description | 4 sentences, ~50 words | 2 sentences |
| Screenshots | 5, all near-black | unknown count, no captions |
| Trailer / preview video | 1 | none |
| Reviews / ratings | 1 (no score shown) | none |
| Controller support declared | Partial | n/a |

---

## 1. The measured problem with the screenshots

The five shipped Steam screenshots average **1–12% mean pixel brightness**:

| Shot | Mean brightness | What it shows |
|------|-----------------|---------------|
| 1 (carousel lead) | **1.1%** | Level 1. Two grey asteroids, ship ~10 px wide. |
| 2 | 6.7% | Split-screen, grey asteroid field, no combat. |
| 3 | 2.3% | Level 2, mostly empty space. |
| 4 | 11.7% | Level 19, dense grey field, no combat. |
| 5 | 6.8% | Level 21, one magenta missile trail. |

Steam renders these as small thumbnails on a dark background, so the lead
screenshot is close to indistinguishable from an empty box. None of the five
shows a weapon firing at scale, an explosion, a black hole, an enemy station,
the warp-lens effect, or online co-op. Three of the five carry the
`SHOW CONTROLS WITH F1` / `PLAYER 2 PRESS ENTER TO JOIN` prompts, which read as
placeholder UI to someone browsing.

`capture_store_screenshots.sh` drives the game headlessly to a chosen
generation with the full arsenal so replacement frames can be grabbed; see §5
for the shot list it is meant to serve.

---

## 2. Steam — short description

Max 300 characters. Shown beside the trailer, in search hover, in the wishlist
mail, and in every Steam widget — the most-read text on the page.

**Current (34 chars):**

```
A retro 2D top-down space-shooter.
```

**Replacement (261 chars):**

```
Momentum is the real enemy. Thrust, drift and shoot through endless asteroid
fields that reflect, teleport, cloak and phase. Ten weapons, black holes and
enemy stations stand between you and the next level. Solo, split-screen, or
online co-op across any device.
```

Why: leads with the mechanic that makes the game different from every other
Asteroids descendant (inertia), names concrete nouns a buyer can picture, and
closes on the co-op hook. "Retro" is dropped from the opening — it is the most
crowded tag on Steam and says nothing about what you do.

---

## 3. Steam — About This Game

Steam BBCode. Paste into App Admin → Store Presence → Description.

```
[h2]Newton's first law is trying to kill you[/h2]
There is no brake. Thrust and you keep going — the same drift that carries you
clear of a collapsing asteroid carries you straight into the next one. Every
shot you fire, every turn you make, is a negotiation with momentum.

[h2]Asteroids that fight back[/h2]
They start simple and stop being simple fast. Reflective rocks bounce your own
bullets back at you. Teleporting ones blink out of your firing line. Invisible
ones show only as a lens-warp in the starfield. Quantum ones stay uncertain
until you look at them. Tough, armoured and phasing rocks each need a different
answer — and later levels stack them together.

[h2]Ten weapons, and a reason to use each[/h2]
A gun with 22 variants, homing missiles, mines and giga-mines, deployable
shields, and three primaries that change how a level reads: the Pierce Beam
ploughs through a whole line of rocks, the Lance fires an instant
mirror-reflecting pulse, and Shock chains lightning from kill to kill. Bank
nova charges on a kill streak and clear the screen when it gets away from you.

[h2]It never stops escalating[/h2]
The world grows every level. Pulsars shove you off course with shockwaves,
comets cross the map at speed, seekers hunt you down, black holes swallow
anything that drifts too close, and enemy stations deploy waves of hunting
ships with escalating AI. There is no final level — only your best run.

[h2]Co-op and PvP, local and online[/h2]
Split-screen on one machine, or online with a friend. Online play is
cross-platform: a Steam player on a desktop or a Deck can fly with a friend on
an iPhone, an Android phone, or a browser. Friendly fire is a toggle, so
"co-op" is negotiable.

[h2]Built small and sharp[/h2]
Hand-written C++ with SDL2 and OpenGL. Vector-drawn, no engine, no launcher, no
telemetry. Full controller support, Steam Cloud saves, Remote Play Together,
and 18 achievements.
```

Rationale: Steam's own store-page research puts a structured, skimmable
description well ahead of a prose paragraph. Each `[h2]` answers one buying
question. The last block is aimed squarely at the audience that buys $5 arcade
games on Steam — people who care that it is a hand-written binary rather than a
stock engine template.

---

## 4. Steam — settings to change in the portal

1. **Controller support: Partial → Full.** Every screen is controller-driven
   today — `State::nav_key_from_controller` covers the menus and options,
   `net_lobby.cpp` ships a dedicated controller code-entry picker built for the
   Deck, and gameplay takes analog stick aiming. "Partial" excludes the game
   from controller and Deck-friendly browsing filters, which is exactly the
   audience for a pick-up-and-play arcade shooter.
2. **Submit for Steam Deck Verified.** The Deck work is already done (touch
   passthrough, the controller picker, the `$ORIGIN` rpath Linux build). The
   badge is free traffic and free trust from a segment that over-indexes on
   short arcade sessions.
3. **Price: $0.99 → $4.99.** See §6.
4. **Tags.** Drop the ones that pull in the wrong crowd and add the ones that
   describe the actual mechanic: keep Arcade, Top-Down Shooter, Local Co-Op,
   Online Co-Op, Split Screen, Space; add **Physics**, **Twin Stick Shooter**,
   **Score Attack**, **Bullet Hell** is a stretch — do not add it. "Casual" is
   working against a game whose whole pitch is that momentum is hard.
5. **Add the cross-platform co-op line to the trailer's first 5 seconds** —
   it is the one thing no competitor in this genre can claim.

---

## 5. Shot list for replacement screenshots

In carousel order. The first two do the work; Steam shows them largest.

1. **Shock chaining** — lightning arcing kill-to-kill across four or five rocks
   at once. The loudest, most colourful thing the renderer produces.
2. **Online co-op, split view** — two ships, both firing, with the peer's name
   badge visible. Sells the differentiator.
3. **Black hole** (generation 13+) with the warp-lens distortion bending the
   starfield, ship on a close pass.
4. **Enemy station** (generation 14+) mid-deployment, several hunting ships in
   frame, tracer fire.
5. **Nova detonation** at full charge — screen-clearing flash.
6. **Lance pulse** — the full-length mirror-reflecting beam bouncing off a
   reflective asteroid.
7. **Level 20+ density** with god mode active, showing the shockwave rings.

Rules for every shot: no `F1` / `PLAYER 2 PRESS ENTER` prompt text in frame,
ship no smaller than ~3% of frame width, and something on fire. Grab them at
1920×1080 on real hardware — the software-GL headless rig is for framing and
iteration, not final captures.

---

## 6. On the $0.99 price

$0.99 is the single biggest constraint on revenue, and probably on conversion
too.

- After Steam's 30% and VAT, a $0.99 sale nets roughly $0.55. At 100 page
  visits a day, even an excellent 6% conversion is about $3.30 a day.
- On Steam, price is read as a quality signal. Sub-$3 is where asset flips and
  shovelware live, and buyers discount accordingly — a $0.99 tag makes the
  1-review count look like confirmation rather than newness.
- At $4.99, the same 100 visits at half that conversion rate (3%) returns about
  $10.50 a day — roughly triple.
- Steam's discovery surfaces weight revenue, not units, so the higher price
  also buys more impressions per sale.

The move is to raise the base price to $4.99 and run launch-window discounts
(-40% to -60%) when there is a sale event to hang them on. That way the
discounted price lands near where it is now, and the anchor works for you
rather than against you. Note Steam enforces a cooldown between a price change
and a discount, so make the change well ahead of a planned sale.

---

## 7. App Store

**Subtitle** (30 char max, currently empty — this is the highest-value unused
ASO field on the listing):

```
Newtonian arcade space shooter
```

**Keywords** (100 char max, comma-separated, no spaces after commas). Words
already in the title and subtitle are indexed separately, so none of them are
repeated here:

```
asteroid,vector,shmup,twin,stick,retro,coop,online,multiplayer,physics,endless,gravity,survival,neon
```

(100/100 characters — re-count before pasting if you edit it.)

**Promotional text** (170 chars, editable without a review cycle — use it for
whatever ships next):

```
Now with cross-platform online co-op: fly with a friend on a Mac, a PC, a Steam Deck or a browser, straight from your phone.
```

**Description** — first three lines are all that shows before "more", so they
carry the pitch:

```
There is no brake in space.

Thrust and you keep drifting. The momentum that pulls you clear of one asteroid
throws you into the next. Newtonia is a vector-drawn arcade shooter about
fighting physics as much as rocks.

Asteroids stop being simple fast. Reflective ones bounce your bullets back.
Teleporting ones blink out of your line of fire. Invisible ones show only as a
warp in the starfield. Quantum ones stay uncertain until you look at them.
Tough, armoured and phasing rocks each need their own answer — and the late
levels stack them.

TEN WEAPONS
A gun with 22 variants, homing missiles, mines and giga-mines, shields, and
three primaries that change a level: the Pierce Beam ploughs through a line of
rocks, the Lance fires an instant reflecting pulse, and Shock chains lightning
from kill to kill. Bank nova charges on a streak and clear the screen.

IT KEEPS ESCALATING
The world grows every level. Pulsars shove you off course, comets cross the map
at speed, seekers hunt you, black holes swallow anything that drifts close, and
enemy stations deploy waves of hunting ships. There is no final level — only
your best run.

CROSS-PLATFORM CO-OP
Play online with a friend on a Mac, PC, Steam Deck, Android phone or browser.
One tap on a shared link drops you into their game. Friendly fire is a toggle.

BUILT FOR TOUCH
On-screen stick and fire buttons, or pair a controller. Game Center
achievements. No ads, no in-app purchases, no telemetry. 9 MB.
```

**Also add:** an App Preview video (the listing has none — Apple weights
listings with video noticeably higher, and the Steam trailer can be recut to
Apple's portrait spec) and caption text burned into each screenshot. Uncaptioned
screenshots are the most common ASO miss and this listing has it.

---

## 8. The free-web-build leak

`web/site/index.html` makes "▶ Play in your browser — free" the primary call to
action in the hero, repeats it as the final CTA, and lists Steam as one of four
small platform tiles below the fold. Anyone who lands on the marketing site
plays for free and never sees a purchase.

That is the right funnel for reach and the wrong one for revenue, and it is
fixable in this repo rather than in a portal. Options, cheapest first:

1. **Sell the paid build from inside the free one.** After a game over on web,
   show a card: the web build is the demo, the paid build has online co-op,
   cloud saves and achievements. This is the highest-leverage change on the
   list because it reaches players at the moment they have just proven they
   like the game.
2. **Give Steam and iOS equal billing in the hero**, not a tile below the fold.
3. **Cap the web build's progression** (e.g. stop at generation 10) and make the
   Steam/iOS builds the way to see the late game. This is a real product
   decision, not a copy tweak — it trades reach for conversion, and it is worth
   deciding deliberately rather than by default.

---

## 9. Reviews

Steam shows no review score below 10 reviews; the page currently reads
"1 user reviews", which is a visible negative to a browsing buyer. Going from 1
to 10 is likely the largest single conversion change available on the Steam
page, and it costs nothing:

- Steam's own "ask for a review" prompt does not exist — but a one-line,
  once-ever card after a good run (a new high score, or clearing level 10) that
  links to the review page is within Steam's rules as long as it is not
  incentivised.
- The itch.io and web players are an untapped pool; the same post-game card
  works there.

Do not offer anything in exchange for a review — Valve treats that as review
manipulation.
