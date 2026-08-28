# Referenced by android/app/build.gradle's release buildType. minifyEnabled
# is currently false, so these rules are dormant — they exist so that
# enabling R8 later cannot silently break the classes reached only through
# JNI (R8 can't see native callers, so it would strip or rename them and
# the game would lose achievements / fail to start rather than fail the
# build).

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
