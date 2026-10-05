# TODO

Follow-ups that need a person, a device or an account we don't have to hand.

## Android cloud saves: test an existing Play Games profile (PR #583)

A Play Games profile for Newtonia made before Saved Games was switched on
(2026-09-28) signs in fine, but every saved-game open fails with "Cannot use
snapshots without enabling the 'Saved Game' feature", then SIGN_IN_REQUIRED.
Deleting the Newtonia profile on that Google account fixed it on Glenn's
phone; players can't be asked to do that.

Likely cause: saved games live in the player's Drive app folder, and a
profile made before Saved Games was switched on only granted basic games
access. `GamesSignInClient.signIn()` can't ask again: on an
already-authenticated session the SDK (22.1.0) returns the existing result
without a new flow (PR #583 review). Confirmed on a second Google account (2026-10-05). Asking through
`requestServerSideAccess` fails inside the Play Games service (a bare
NullPointerException with extra scopes, "Not signed in when calling API"
without), so `PlayGamesSaves` now asks once per launch for the
`drive.appdata` scope through Google's general authorization API
(`SavedGamesConsent`, play-services-auth) and tries once more. Whether Play
Games honours that grant is the open question. If it doesn't take, sync
stays off for that profile and the game plays on local files as before.

The board worker logs `play games verify: drive.appdata granted|missing` on
every Play Games verification (Workers Logs), which shows how many verified
players lack the access.

To do:
1. Confirm with a Google account (not a device — profiles are per account)
   that played Newtonia before 2026-09-29: install a build with PR #583 and
   run `adb logcat -s NewtoniaCloudSync NewtoniaPlayGames`. Expect
   "asking Google to grant Saved Games access", a Google consent screen,
   then either a synced save (fixed) or the "no Saved Games access" line.
2. If the consent request doesn't fix it: keep Android saves on our own
   board worker under the verified Play Games player id, or tell affected
   players to remove Newtonia's access in their Google account settings.
