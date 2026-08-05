# Steam store copy

Replacement text for the Newtonia store page (app 4536720). Written
2026-08-05 against the live page, which currently carries a one-line short
description and a two-sentence body — see PROMOTION.md §2 for why that is the
first thing to fix and not the last.

Everything below is checked against the code. Nothing claims a feature the
build does not have: refunds and angry reviews are far more expensive than a
modest description, and at zero reviews the page cannot absorb either.

---

## 1. Short description

**Steamworks:** Store Presence → Basic Info → *Short Description*.
**Limit: 300 characters.** This is the highest-leverage field on the whole
page — it is what shows in search results, in the hover capsule, on the
library shelf, and in every wishlist mail. The current one
("A retro 2D top-down space-shooter.") does the reader's dismissing for them.

Glenn's copy, 271 of 300 characters:

> Newtonia is a 2D top-down space shooter. Think Asteroids with powerful
> weapons and challenging asteroids. Progress level after level against
> increasing challenge with advanced weapons dropping regularly. Try and get
> a high score, or play co-operatively with your friends.

The load-bearing line is "Think Asteroids with powerful weapons and
challenging asteroids". Naming the comparison outright and answering it in
the same breath beats working around it: "Asteroids clone" is the reflex this
page has to survive in the first two seconds, and a reader who has already
been given the answer has nothing left to dismiss.

Optional tightening, same voice, 279 characters — three changes, each
independent:

> A 2D top-down space shooter. Think Asteroids with powerful weapons and far
> more dangerous rocks. Progress level after level against increasing
> challenge, with advanced weapons dropping regularly. Go for a high score,
> or play co-operatively with a friend: split-screen or online.

1. Drops "Newtonia is a" — 13 characters restating the name printed directly
   above it, and the opening is the part that survives truncation in a search
   result.
2. "asteroids" appears twice in one sentence (the game, then the rocks), and
   "challenging" runs straight into "increasing challenge".
3. "co-operatively with your friends" is vaguer than the truth. Split-screen,
   online co-op, PvP and Remote Play Together are each their own Steam
   category, and naming them catches the people filtering for exactly that.

**Do not let a rewrite creep back in past those three points.** An earlier
draft of this file opened "Thrust, drift, and never quite stop… ones you only
see by the light they bend" — three list-of-three rhythms in four sentences,
and a lyrical flourish spent on the invisible asteroids, which are the least
visible thing in the game (shots/README.md → Clips). Plain beats poetic here.

---

## 2. About This Game

**Steamworks:** Store Presence → Description → *About This Game*.
BBCode. Paste as-is.

```
There is no brake.

Newtonia is a top-down space shooter built on momentum. You thrust, you
drift, and the world wraps around behind you. Every shot you take pushes
you somewhere you have to deal with a second later. Clear the field to
advance. The field gets bigger, stranger and more crowded every time.

[h2]Every level adds a new way to die[/h2]
It starts with rocks. Then the rocks start cheating.

[list]
[*][b]Reflective[/b]: bounces your bullets straight back at you
[*][b]Teleporting[/b]: gone before your shot arrives
[*][b]Invisible[/b]: no body at all; you find it by the light it bends
[*][b]Quantum[/b]: only solid while someone is looking at it
[*][b]Tough[/b]: five hits, cracking as it goes
[*][b]Armoured[/b]: one rotating face deflects everything; hit the seam
[*][b]Phasing[/b]: solid, then not, then solid again
[/list]

Then the things that are not rocks arrive. Pulsars that charge up and shove
you across the map with a shockwave. Comets that shed debris as they burn
past. Seekers that hunt you down and ram. A black hole parked in the middle
of the world, bending everything toward it. Enemy stations that deploy
waves of hunting ships and get better at it each wave.

The levels do not stop. The world grows, the count climbs, and the run ends
when you do.

[h2]Ten weapons, and none of them are just "the gun"[/h2]
[list]
[*][b]Lance[/b]: one instantaneous full-length pulse that mirror-bounces
off reflective surfaces and keeps going
[*][b]Shock[/b]: chain lightning that hops from kill to kill, and bursts
into sparks against anything it cannot destroy
[*][b]Pierce Beam[/b]: ploughs through a whole line of asteroids, stopping
only at one it cannot break
[*][b]Nova[/b]: charges off your kill streak, then clears the screen
[*][b]Homing missiles, mines, giga mines, shields, and God Mode[/b]: ten
seconds of invincibility that fires shockwaves while it lasts
[/list]

[h2]Bring someone[/h2]
Split-screen co-op on one machine, online co-op over the internet, or LAN.
Turn friendly fire on if you trust each other less than that. If your
partner runs out of lives, a revive pickup drops. Reach it and they are
back in the fight. Play PvP if you would rather settle it directly.

Also supports Remote Play Together, so a second pilot does not need a copy.

[h2]A leaderboard that can prove it[/h2]
Every run can be recorded as a replay, and the leaderboard verifies scores
against them. A run gets on the board because the server watched it happen,
not because the client claimed a number. Replays are watchable in a browser,
so a top run is a link you can send someone.

[h2]Also[/h2]
[list]
[*]18 achievements
[*]Steam Cloud saves
[*]Windows, macOS, Linux, and Steam Deck
[*]Keyboard, controller, or one of each
[/list]

[h2]Try it before you buy it[/h2]
The single-player and split-screen game runs free in your browser, no
install and no account:

[url=https://newtonia.metonymous.com/play/]newtonia.metonymous.com/play[/url]

Buying gets you online co-op and PvP, achievements, cloud saves, and the
native build.
```

---

## 3. Other fields worth fixing while you are in there

| Field | Now | Set it to |
|---|---|---|
| Website (Basic Info) | **unset** | `https://newtonia.metonymous.com` |
| Short description | one line | §1 above |
| About This Game | two sentences | §2 above |

### Tags

Tags drive more Steam discovery than the description does, and they are
ordered — the first few matter most. Suggested, most-important first:

> Space, Arcade, Shoot 'Em Up, 2D, Physics, Score Attack, Local Co-Op,
> Split Screen, Online Co-Op, Action, Indie, Singleplayer, Multiplayer,
> Retro, Minimalist, Difficult, Replay Value, PvP

**Do not tag Twin Stick Shooter.** It is rotate-and-thrust, not twin-stick,
and the mismatch buys refunds from exactly the people the tag attracts.

### Two rules to not trip over

- **Do not link itch.io from the Steam page.** Linking your own site is fine
  and the free browser build is a demo, not a rival storefront — but a link
  to another store front is against Steam's distribution terms. Keep the URL
  pointed at `newtonia.metonymous.com`.
- **Do not describe the browser version as "the full game free."** It has
  netplay force-disabled (see `.github/workflows/web.yml`). "The
  single-player and split-screen game" is the accurate framing and is what
  §2 uses.

---

## 4. The in-game review ask

Getting to **10 reviews** is what makes Steam's algorithm start working —
below that there is no review score shown at all. The fastest legitimate
route is to ask once, at a good moment, and never again.

Suggested: after clearing level 10 on a Steam build, once per install, on the
level-clear screen, dismissed by any input:

> ENJOYING NEWTONIA?
> A STEAM REVIEW HELPS MORE THAN YOU'D THINK
> [ LEAVE ONE ]      [ NOT NOW ]

Keep it to one shot per install — a repeat ask reliably earns the negative
review it was fishing to avoid. `SteamFriends()->ActivateGameOverlayToStore`
with the app ID opens the store page where the review button lives.

---

## 5. Publish checklist

Store page content is portal-only — there is no API for descriptions, so none
of this can be scripted from the repo. It is a paste job at
[partner.steamgames.com](https://partner.steamgames.com) against app 4536720.

Live state as of 2026-08-05: short description still
`A retro 2D top-down space-shooter.`, website unset, zero reviews.

**Ship these three now.** All settled, and between them they are most of the
available win:

| # | Steamworks location | Paste |
|---|---|---|
| 1 | Store Presence → Basic Info → Short Description | §1, Glenn's 271-char copy |
| 2 | Store Presence → Basic Info → website/links | `https://newtonia.metonymous.com` |
| 3 | Store Presence → tags (up to 20, **ordered**) | §3, first few matter most |

**Hold this one.** The §2 About This Game body is unrewritten assistant prose
and reads like it. It is the least urgent field on the page — nobody reads it
before deciding — so publishing the three above without it loses nothing, and
publishing it as-is puts a voice on the page that does not match the short
description directly above it.

Then:

- **Edits save as a draft; the Store Presence section has a separate publish
  step.** A saved change that was never published is the classic way this
  ends with nothing actually live. Reload the public store page after
  publishing and confirm the new short description is really there.
- The short description propagates to search results, hover capsules, the
  library shelf and wishlist mail, but not instantly everywhere — do not
  re-edit in a panic if a cached surface still shows the old line.

## 6. What this copy deliberately does not say

Worth writing down so it does not creep back in:

- **No "retro".** It is the word that makes a reader file this next to every
  other Asteroids clone and scroll on. The game earns a better frame than
  that, so the copy leads with momentum and the special asteroids instead.
- **No review quotes, no awards, no player counts.** There are none yet.
  Inventing social proof is both against Steam's rules and instantly obvious.
- **No roadmap or "coming soon" features.** The page sells what builds today.
