# Partner Center values — GitHub secrets checklist

> **DEFERRED (2026-07-30): Partner Center onboarding and these secrets belong
> to the private repo `cradle/newtonia-xbox`** along with all other Xbox work
> — see `xbox/PRIVATE_REPO.md`. Set them on the private repo, which is where
> a package will actually be built and submitted; the public repo's
> `deploy-xbox.yml` stays disabled and unconfigured. The token/substitution
> contract below is the reference for doing that.

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

## Not ready yet

`deploy-xbox.yml` still uses the old winget + GDK MSBuild-platform build,
which is broken on current hosted runners, and targeting the console requires
the NDA GDKX (not available on hosted runners). It needs a self-hosted GDKX
runner and a BWOI/Ninja rework mirroring `xbox-dev.yml` before it can build a
package — see `xbox/PORT_PLAN.md` Phase 5. The secrets above are the
hardware-free part you can set up now.
