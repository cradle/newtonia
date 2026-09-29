# TODO

Follow-ups that need a person, a device or an account we don't have to hand.

## Android cloud saves: test an existing Play Games profile (PR #583)

A Play Games profile for Newtonia made before Saved Games was switched on
(2026-09-28) signs in fine, but every saved-game open fails with "Cannot use
snapshots without enabling the 'Saved Game' feature", then SIGN_IN_REQUIRED.
Deleting the Newtonia profile on that Google account fixed it on Glenn's
phone; players can't be asked to do that.

`PlayGamesSaves.askConsent` shows Google's sign-in once per install on those
errors, hoping it grants the missing access. Untested: it needs a Google
account (not device — profiles are per account) that played Newtonia before
2026-09-29.

To test: install a build with PR #583 signed in to that account, launch,
and run `adb logcat -s NewtoniaCloudSync NewtoniaPlayGames`. Pass: "asking
Play Games for Saved Games access", then "read the saved game". Fail: the
open still errors after the prompt — those players then don't sync (the
game plays on local files as before) and we need another route.
