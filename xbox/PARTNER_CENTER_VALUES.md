# Partner Center values — fill-in checklist

Once Newtonia's title exists in Partner Center, these are the only
project-specific values to drop in. Two destinations: the package manifest
(`xbox/MicrosoftGame.config`, committed) and GitHub encrypted secrets (used by
`.github/workflows/disabled/deploy-xbox.yml`).

## 1. Package manifest — `xbox/MicrosoftGame.config`

Replace the four `__FILL_*__` placeholders. Find them all with:

```sh
grep __FILL_ xbox/MicrosoftGame.config
```

| Placeholder | Value | Where in Partner Center |
|-------------|-------|-------------------------|
| `__FILL_IDENTITY_NAME__` | Package/Identity **Name** (e.g. `Publisher.Newtonia`) | Product → Product identity |
| `__FILL_IDENTITY_PUBLISHER__` | Package/Identity **Publisher** (the full `CN=...` string) | Product → Product identity |
| `__FILL_PUBLISHER_DISPLAY_NAME__` | Publisher **display name** (appears twice in the file) | Account → Account settings |
| `__FILL_STORE_ID__` | 12-character **Store ID** | Product → Product identity |

All four are public values, so commit them once filled. They must match
Partner Center exactly.

## 2. GitHub secrets — for the deploy workflow

Set under repo Settings → Secrets and variables → Actions:

| Secret | Value |
|--------|-------|
| `XBOX_STORE_ID` | Same 12-char Store ID as `__FILL_STORE_ID__` above |
| `XBOX_PARTNER_CENTER_TENANT_ID` | Azure AD tenant ID |
| `XBOX_PARTNER_CENTER_CLIENT_ID` | Azure AD app (service principal) client ID |
| `XBOX_PARTNER_CENTER_CLIENT_SECRET` | Service principal secret |

The deploy workflow's first step verifies the config has no `__FILL_`
placeholders left and that `XBOX_STORE_ID` matches the config's `<StoreId>`,
so a mismatch or unfilled value fails fast instead of shipping a bad package.

## 3. Store art (not from Partner Center)

`xbox/Assets/` currently holds procedurally generated placeholder art
(`xbox/generate_assets.py`). Replace with final artwork before store
submission — sizes are listed in `xbox/MicrosoftGame.config`.

## Not ready yet

`deploy-xbox.yml` still uses the old winget + GDK MSBuild-platform build,
which is broken on current hosted runners, and targeting the console requires
the NDA GDKX (not available on hosted runners). It needs a self-hosted GDKX
runner and a BWOI/Ninja rework mirroring `xbox-dev.yml` before it can build a
package — see `xbox/PORT_PLAN.md` Phase 5. This checklist only covers the
submission identity, which is hardware-free to prepare now.
