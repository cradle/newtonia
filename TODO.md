# TODO

Follow-ups that need a person, a device or an account we don't have to hand.

## Android cloud saves: test an existing Play Games profile (PR #583)

A Play Games profile for Newtonia made before Saved Games was switched on
(2026-09-28) signs in fine, but every saved-game open fails with "Cannot use
snapshots without enabling the 'Saved Game' feature", then SIGN_IN_REQUIRED.
Deleting the Newtonia profile on that Google account fixed it on Glenn's
phone; players can't be asked to do that.

No in-game recovery is known. `GamesSignInClient.signIn()` can't do it: on
an already-authenticated session the SDK (22.1.0) returns the existing result
without starting a new flow (PR #583 review). So `PlayGamesSaves` recognises
the error, logs "this Play Games profile has no Saved Games access", and
leaves sync off for that profile; the game plays on local files as before.

To do:
1. Confirm with a Google account (not a device — profiles are per account)
   that played Newtonia before 2026-09-29: install a build with PR #583 and
   run `adb logcat -s NewtoniaCloudSync NewtoniaPlayGames`. Expect sign-in
   and achievements to work and the "no Saved Games access" line.
2. Find a supported route that grants the access without the player
   deleting their Newtonia profile, or decide to tell affected players how.
