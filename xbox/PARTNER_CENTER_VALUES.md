# Partner Center values — GitHub secrets checklist

All deployment-specific values are stored as **GitHub encrypted secrets** and
injected at build time — nothing identity-related is committed to source, so
the repo stays reusable (same pattern as the Steam deploy's `STEAM_APP_ID`).
`xbox/MicrosoftGame.config` keeps `__FILL_*__` tokens permanently; the deploy
workflow (`.github/workflows/disabled/deploy-xbox.yml`, "Stage layout")
substitutes them from the secrets below.

Set these under repo Settings → Secrets and variables → Actions.

## Package identity (substituted into MicrosoftGame.config)

| Secret | Value | Where in Partner Center |
|--------|-------|-------------------------|
| `XBOX_IDENTITY_NAME` | Package/Identity **Name** (e.g. `Publisher.Newtonia`) | Product → Product identity |
| `XBOX_IDENTITY_PUBLISHER` | Package/Identity **Publisher** (full `CN=...` string) | Product → Product identity |
| `XBOX_PUBLISHER_DISPLAY_NAME` | Publisher **display name** | Account → Account settings |
| `XBOX_STORE_ID` | 12-character **Store ID** | Product → Product identity |

## Submission (StoreBroker auth)

| Secret | Value |
|--------|-------|
| `XBOX_PARTNER_CENTER_TENANT_ID` | Azure AD tenant ID |
| `XBOX_PARTNER_CENTER_CLIENT_ID` | Azure AD app (service principal) client ID |
| `XBOX_PARTNER_CENTER_CLIENT_SECRET` | Service principal secret |

The deploy workflow checks all four identity secrets are present before
building (fail-fast), and verifies no `__FILL_` token remains after
substitution, so a missing value can never ship a placeholder package.

## Store art (not a secret)

`xbox/Assets/` holds procedurally generated placeholder art
(`xbox/generate_assets.py`). Replace with final artwork before store
submission — sizes are listed in `xbox/MicrosoftGame.config`. (Art is the
same for every deployment, so it's committed, not a secret.)

## Xbox Play Anywhere (config-only — no secret, no code)

Play Anywhere is buy-once-play-on-both: one purchase entitles the player on both
Xbox console and Windows PC, with cloud-roaming saves. It is enabled entirely in
Partner Center — no extra build artifact — once both SKUs exist:

1. **One product, one Title ID.** Publish the **Windows Desktop** SKU and the
   **Xbox Console** SKU under the *same* Partner Center product. Cross-entitlement
   is automatic when both ship under one Title ID. (The four identity secrets
   above describe that single product; Play Anywhere does not add new secrets.)
2. **Enable Play Anywhere** on the product (Partner Center product setup) once
   both SKUs are present.
3. **Cloud-roaming saves are a hard requirement (XR-052).** Local-only
   `SDL_GetPrefPath` saves do not satisfy it. The game side is already seamed:
   `save_storage.*` routes the Roaming category (savegame.dat, highscore.dat)
   through one call whose GDK body is `XGameSaveFiles` (PORT_PLAN work-item #10).
   Preferences stay machine-local and intentionally do **not** roam.
4. Verify the binary save format loads on both builds — it is the same
   `savegame.cpp` format (magic "NWTN", version 10), already shared.

This is all **NDA-free**: it is Partner Center configuration plus the
already-merged storage seam. The only blocked piece is the Xbox Console SKU
binary itself, which needs the GXDK (NDA). The Windows Desktop SKU and all the
config above can be staged now.

## Not ready yet

`deploy-xbox.yml` still uses the old winget + GDK MSBuild-platform build,
which is broken on current hosted runners, and targeting the console requires
the NDA GDKX (not available on hosted runners). It needs a self-hosted GDKX
runner and a BWOI/Ninja rework mirroring `xbox-dev.yml` before it can build a
package — see `xbox/PORT_PLAN.md` Phase 5. The secrets above are the
hardware-free part you can set up now.
