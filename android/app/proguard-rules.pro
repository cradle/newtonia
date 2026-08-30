# Referenced by android/app/build.gradle's release buildType, where
# minifyEnabled is ON (R8; PLAYQUALITY.md). These rules pin the classes
# reached only through JNI — R8 can't see native callers, so without them
# it would strip or rename the classes and the game would lose
# achievements / identity attestation / fail to start rather than fail
# the build. android.yml boots the minified APK through the emulator
# selftest (catches the fail-hard cases); the Play Games paths fail SOFT
# and need a device check after any change here.

# Play Games achievements bridge: looked up and invoked from native code
# (play_games_achievements.cpp) via FindClass/GetStaticMethodID.
-keep class org.newtonia.PlayGamesAchievements { *; }

# Netplay identity + leaderboard attestation bridge: resolved by name from
# native code (play_games_identity.cpp) exactly like the achievements
# class above. Stripping this one fails SOFT — identity degrades to an
# unattested claim (role labels online, no leaderboard submission) with a
# logcat line as the only symptom — so it must be kept even though no
# Java code references it.
-keep class org.newtonia.PlayGamesIdentity { *; }

# SDL's Java layer and the activity are likewise driven through JNI by the
# SDL2 native library and NewtoniaActivity's native field reads.
-keep class org.libsdl.app.** { *; }
-keep class org.newtonia.NewtoniaActivity { *; }

# Install-referrer deferred deep link (NewtoniaActivity.checkInstallReferrer):
# the AAR ships its own consumer rules, but this path also fails soft — a
# join code riding the referrer is silently lost — and the library is a few
# KB, so pin it rather than trust the transitive rules forever.
-keep class com.android.installreferrer.** { *; }
