CC = g++
SDL2_CFLAGS := $(shell sdl2-config --cflags)
SDL2_LIBS   := $(shell sdl2-config --libs) -lSDL2_mixer
CFLAGS = -Wall -O3 -std=c++11 $(SDL2_CFLAGS)

UNAME := $(shell uname)
ANDROID_SRCS = android_main.cpp

ifeq ($(UNAME), Darwin)
  LIBS = -framework GLUT -framework OpenGL -framework AppKit $(SDL2_LIBS)
  CFLAGS += -DGL_SILENCE_DEPRECATION -Wno-char-subscripts
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
else ifneq (,$(findstring _NT,$(UNAME)))
  # Windows (MSYS2 MINGW64 shell) — mirrors .github/workflows/windows.yml:
  # static link so newtonia.exe runs without mingw64 DLLs on PATH.
  CFLAGS += -D_USE_MATH_DEFINES -DFREEGLUT_STATIC
  LIBS = -static $(shell pkg-config --libs --static sdl2 SDL2_mixer freeglut) \
         -lopengl32 -lglu32
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
else
  # -lXi: the XInput2 touchscreen listener in glut.cpp (Steam Deck touch).
  LIBS = -lglut -lGL -lGLU -lX11 -lXi $(SDL2_LIBS)
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
endif

# Native netplay backend (macOS/Linux/Windows MinGW) — ON BY DEFAULT since
# the netplay release; see NETPLAY.md. Needs libdatachannel, which is not
# packaged by Homebrew — build it from source ONCE:
#   ./build_netplay_deps.sh              (--universal for `make osx`)
# Opt out (netless binary, no deps needed):
#   make NETPLAY=0
# NETPLAY_PREFIX defaults to the script's install dir (./netplay-libs);
# pass it only for a prefix elsewhere. Netplay-on defines NEWTONIA_NET_RTC
# (activates net_transport_rtc.cpp and the menu's ONLINE row) and links the
# libdatachannel C API. Missing deps are a hard error, not a silent
# fallback — a netless binary must be asked for, never shipped by accident.
NETPLAY ?= 1
ifeq ($(NETPLAY),1)
  NETPLAY_PREFIX ?= $(CURDIR)/netplay-libs
  ifeq ($(filter clean web-clean android android-install android-assets android-clean web ios ios-install ios-deps-check ios-clean,$(MAKECMDGOALS)),)
    ifeq ($(wildcard $(NETPLAY_PREFIX)/include/rtc/rtc.h),)
      $(error netplay builds by default but $(NETPLAY_PREFIX)/include/rtc/rtc.h \
is missing — run ./build_netplay_deps.sh once (--universal for `make osx`), \
point NETPLAY_PREFIX at an existing install, or build without netplay: \
make NETPLAY=0)
    endif
    # A prefix built BEFORE patches/libdatachannel-ws-ca-cert.patch has no
    # caCertificatePemFile, so the TLS-verifying sockets fail to compile
    # (LEADERBOARD.md S1) — correctly, but a few hundred lines into the build
    # and pointing at our source rather than the stale dependency. Say it here
    # instead. The check is a grep of one header, once per make invocation.
    ifeq ($(shell grep -c caCertificatePemFile $(NETPLAY_PREFIX)/include/rtc/rtc.h 2>/dev/null),0)
      $(error $(NETPLAY_PREFIX) predates patches/libdatachannel-ws-ca-cert.patch \
(no caCertificatePemFile in rtc/rtc.h) — rebuild it: ./build_netplay_deps.sh \
(--universal for `make osx`))
    endif
  endif
  CFLAGS += -DNEWTONIA_NET_RTC -I$(NETPLAY_PREFIX)/include
  ifneq (,$(findstring _NT,$(UNAME)))
    # Windows (MSYS2 MINGW64) — mirrors .github/workflows/windows.yml: static
    # libdatachannel + vendored libjuice/usrsctp archives ("--start-group"
    # resolves their circular references), msys2's static OpenSSL, and the
    # Winsock/crypto system libs. RTC_STATIC stops rtc.h declaring dllimport
    # symbols. Populate $(NETPLAY_PREFIX) with include/ and lib/*.a first
    # (see the CI workflow's "Build libdatachannel" step).
    CFLAGS += -DRTC_STATIC
    LIBS += -Wl,--start-group $(wildcard $(NETPLAY_PREFIX)/lib/*.a) -Wl,--end-group \
            -lssl -lcrypto -lws2_32 -liphlpapi -lbcrypt -lcrypt32
  else
    LIBS += -L$(NETPLAY_PREFIX)/lib -ldatachannel -Wl,-rpath,$(NETPLAY_PREFIX)/lib
  endif
endif

CFLAGS += -MMD -MP

# --- Version stamp -------------------------------------------------------
# Stamped into every replay header (Replay::Header::game_version, 23 chars
# max) so leaderboard seasons can bucket runs by release — REPLAY.md R4.
# Write-once per file and never back-fillable, so every build path defines
# it: this one, the osx/web/ios targets below, the root and xbox
# CMakeLists (Android/Xbox), and the deploy workflows, which pass the tag
# explicitly because a shallow CI checkout has no tags to describe.
#
#   make                        -> git describe (tag, or the short sha)
#   make NEWTONIA_VERSION=v1.2.3 -> exactly that
#
# Empty (no git, e.g. a source tarball) leaves replay.h's "dev" default.
# A CFLAGS override on the command line drops the stamp with everything
# else in CFLAGS — that only affects hand-rolled debug builds, which are
# honestly "dev".
#
# The 23-char cap is a silent right-truncation (strncpy in replay.cpp), so
# the describe format is kept short enough to survive it: `-dirty` would
# spend 6 of those chars, and a tag plus commit distance plus sha already
# runs to ~20 ("v1.47.0-27-ga24a9be"). `--dirty=+` spends one, and the
# truncation that remains possible for a very long tag drops the sha tail
# rather than the tag the seasons actually bucket on. Same format in the
# root and xbox CMakeLists — keep the three in step.
# Memoized (:=) behind an origin check rather than ?=: a recursively
# expanded describe re-ran git on every expansion of CFLAGS — once per
# compiled object, ~100 subprocess spawns per full build, with a window
# for a mid-build commit to stamp replay.o differently from what
# version.stamp recorded. The origin test keeps ?='s semantics: command
# line and environment still win.
ifeq ($(origin NEWTONIA_VERSION), undefined)
  NEWTONIA_VERSION := $(shell git describe --tags --abbrev=7 --dirty=+ --always 2>/dev/null)
endif
ifneq ($(NEWTONIA_VERSION),)
  VERSION_CFLAGS = -DNEWTONIA_VERSION_STRING='"$(NEWTONIA_VERSION)"'
  # The iOS build takes it as an xcodebuild setting, the same hand-off
  # NEWTONIA_NET_DEFINE uses (project.yml expands the placeholder into
  # GCC_PREPROCESSOR_DEFINITIONS, and to nothing when it isn't passed).
  # The \\\" survives make -> shell -> xcodebuild as a literal \" , which
  # is the escaping Xcode needs to define a string macro.
  IOS_VERSION_SETTING = \
    "NEWTONIA_VERSION_DEFINE=NEWTONIA_VERSION_STRING=\\\"$(NEWTONIA_VERSION)\\\""
endif
CFLAGS += $(VERSION_CFLAGS)

COMPILE = $(CC) $(CFLAGS) -c
OBJFILES := $(patsubst %.cpp,%.o,$(ALL_SRCS))
ifeq ($(UNAME), Darwin)
  OBJFILES += macos_window.o
endif
DEPFILES := $(OBJFILES:.o=.d)


all: newtonia

# Flavor stamp: the netplay seam files compile to EMPTY translation units
# without NEWTONIA_NET_RTC, so switching NETPLAY on or off between builds
# must rebuild everything — otherwise stale objects from the other flavor
# link against the wrong library set ("undefined symbols: _rtcCreate...").
FLAVOR := netplay-$(if $(filter 1,$(NETPLAY)),on,off)
.PHONY: FORCE
FORCE: ;
flavor.stamp: FORCE
	@[ "`cat flavor.stamp 2>/dev/null`" = "$(FLAVOR)" ] || echo "$(FLAVOR)" > flavor.stamp
$(OBJFILES): flavor.stamp

# Version stamp, same trick, one object: make doesn't notice a changed
# -D, so an incremental build would keep stamping the sha replay.o was
# first compiled with. Only replay.cpp reads the macro, so scope the
# rebuild to it — hanging every object off this would mean a full rebuild
# on every commit. Both flavors of that one object: replay.steam.o
# compiles from the same $(CFLAGS) under a different rule, so it needs the
# prerequisite too or `make steam` keeps stamping the old version.
version.stamp: FORCE
	@[ "`cat version.stamp 2>/dev/null`" = "$(NEWTONIA_VERSION)" ] || \
	  echo "$(NEWTONIA_VERSION)" > version.stamp
replay.o replay.steam.o: version.stamp

# --- macOS universal bundle ----------------------------------------------
# Two whole-program compiles (arm64 + x86_64) lipo'd together, mirroring
# the CI recipe. The x86_64 half links against the Rosetta Homebrew tree
# (/usr/local) — install it plus sdl2/sdl2_mixer there for local universal
# builds. The default netplay build needs a UNIVERSAL libdatachannel here:
#   ./build_netplay_deps.sh --universal
# and the dylib is embedded in the bundle at Contents/Frameworks.
OSX_SDL_ARM ?= /opt/homebrew
OSX_SDL_X86 ?= /usr/local
OSX_MIN = -mmacosx-version-min=12.0
ifeq ($(NETPLAY),1)
  OSX_NET_CFLAGS = -DNEWTONIA_NET_RTC -I$(NETPLAY_PREFIX)/include
  OSX_NET_LIBS = -L$(NETPLAY_PREFIX)/lib -ldatachannel \
                 -Wl,-rpath,@executable_path/../Frameworks \
                 -Wl,-rpath,$(NETPLAY_PREFIX)/lib
endif

newtonia-arm64: OSX_SDL = $(OSX_SDL_ARM)
newtonia-x86_64: OSX_SDL = $(OSX_SDL_X86)
newtonia-arm64 newtonia-x86_64: osx-netplay-check FORCE
	$(CC) -O3 -Wall -std=c++11 -arch $(patsubst newtonia-%,%,$@) $(OSX_MIN) \
	  -DGL_SILENCE_DEPRECATION -Wno-char-subscripts $(OSX_NET_CFLAGS) \
	  $(VERSION_CFLAGS) \
	  -I$(OSX_SDL)/include/SDL2 -D_THREAD_SAFE \
	  -o $@ $(ALL_SRCS) macos_window.mm \
	  -L$(OSX_SDL)/lib -lSDL2 -lSDL2_mixer $(OSX_NET_LIBS) \
	  -framework GLUT -framework OpenGL -framework AppKit

# A thin (single-arch) libdatachannel from a plain `./build_netplay_deps.sh`
# run fails the x86_64 link with pages of undefined _rtc* symbols; catch it
# up front instead.
.PHONY: osx-netplay-check
osx-netplay-check:
ifeq ($(NETPLAY),1)
	@lipo -info $(NETPLAY_PREFIX)/lib/libdatachannel.dylib | grep -q x86_64 && \
	 lipo -info $(NETPLAY_PREFIX)/lib/libdatachannel.dylib | grep -q arm64 || { \
	  echo "error: $(NETPLAY_PREFIX)/lib/libdatachannel.dylib is not universal —" ; \
	  echo "       rebuild the deps with: ./build_netplay_deps.sh --universal" ; \
	  exit 1 ; }
	@grep -q caCertificatePemFile $(NETPLAY_PREFIX)/include/rtc/rtc.h || { \
	  echo "error: $(NETPLAY_PREFIX) predates the WS CA patch —" ; \
	  echo "       rebuild the deps with: ./build_netplay_deps.sh --universal" ; \
	  exit 1 ; }
endif

osx: osx-netplay-check newtonia-arm64 newtonia-x86_64
	lipo -create -output newtonia newtonia-arm64 newtonia-x86_64
	lipo -info newtonia
	mkdir -p Newtonia.app/Contents/MacOS
	mkdir -p Newtonia.app/Contents/Resources
	cp newtonia Newtonia.app/Contents/MacOS/Newtonia
	rm -rf Newtonia.app/Contents/Resources/audio
	cp -r audio Newtonia.app/Contents/Resources/audio
	cp icon.icns Newtonia.app/Contents/Resources/icon.icns
	sed 's/$${EXECUTABLE_NAME}/Newtonia/g' Newtonia-Info.plist > Newtonia.app/Contents/Info.plist
ifeq ($(NETPLAY),1)
	mkdir -p Newtonia.app/Contents/Frameworks
	cp $(NETPLAY_PREFIX)/lib/libdatachannel.*.dylib Newtonia.app/Contents/Frameworks/
endif

newtonia: $(OBJFILES)
	$(CC) -o newtonia $(OBJFILES) $(LIBS)

clean:
	rm -rf $(OBJFILES) $(DEPFILES) newtonia newtonia.exe newtonia-arm64 newtonia-x86_64 flavor.stamp

# ============================================================
# Web / Emscripten target
# ============================================================
# em++, not emcc: web.yml installs emsdk "latest", and current Emscripten
# only links the C++ runtime (libc++, exceptions) when the driver is the
# C++ one — emcc-as-linker fails on undefined operator delete/__cxa_throw.
EMCC = em++

# Exclude desktop and Android entry points; add web entry point
WEB_EXCL = glut.cpp android_main.cpp
WEB_SRCS := $(filter-out $(WEB_EXCL), $(wildcard *.cpp) $(wildcard */*.cpp))

# GROWABLE_ARRAYBUFFERS (default-on since emscripten 6.0.2) breaks Firefox:
# its TextDecoder rejects views over resizable ArrayBuffers, crashing
# UTF8ToString at startup. Requires emcc >= 4.0.12 to recognise the setting.
# ccall is used by the netplay test hooks (nwtest_* in net_transport_web.cpp)
# to pass SDP strings from JS; harmless to production pages. HEAPU8 +
# _malloc/_free let main.ts hand a downloaded leaderboard replay blob to
# web_watch_replay (?replay= watch link) — ccall's "array" type would put
# multi-MB blobs on the wasm STACK, so the heap route is deliberate.
# (_main must be listed once EXPORTED_FUNCTIONS is set at all;
# EMSCRIPTEN_KEEPALIVE exports ride along regardless.)
WEB_FLAGS = -std=c++11 -O2 \
            -s USE_SDL=2 \
            -s USE_SDL_MIXER=2 \
            -s SDL2_MIXER_FORMATS='["wav","mp3"]' \
            -s FULL_ES2=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s GROWABLE_ARRAYBUFFERS=0 \
            -s EXPORTED_RUNTIME_METHODS='["ccall","HEAPU8"]' \
            -s EXPORTED_FUNCTIONS='["_main","_malloc","_free"]' \
            -lidbfs.js \
            --shell-file web/shell.html

WEB_FLAGS += --preload-file audio@audio
WEB_FLAGS += $(VERSION_CFLAGS)

# `make web NETPLAY=0` ships the web game with netplay force-disabled
# (NEWTONIA_NET_DISABLED: ONLINE row hidden, invite codes drained and
# dropped, factories return null — the Worker/TURN are never contacted).
# Used by the PUBLIC web deploys (GitHub Pages, itch release channel)
# until live infra usage is understood; the netplay test channel builds
# without it. The backend still compiles, so no source-set changes.
ifeq ($(NETPLAY),0)
  WEB_FLAGS += -DNEWTONIA_NET_DISABLED
endif

.PHONY: web web-clean

# Landing page (web/site/) is served at the root; the playable game lives at /play.
web:
	mkdir -p web/dist/play
	tsc -p web/tsconfig.json
	$(EMCC) $(WEB_SRCS) $(WEB_FLAGS) -o web/dist/play/index.html
	cp web/main.js web/dist/play/main.js
	# Shared store routing (store IDs + the ANDROID_PUBLIC flag, one
	# file): the site pages load it from the root, and the game bundle
	# gets its own copy so the itch.io deploy (play-only) carries it.
	cp web/site/store_route.js web/dist/play/store_route.js
	cp web/site/index.html web/site/styles.css web/site/site.js web/site/icon.png web/site/store_route.js web/dist/
	cp web/site/CNAME web/dist/CNAME
	# Universal join link (invites.h): the /join landing page + the
	# apple-app-site-association / assetlinks.json association files that make
	# a tapped link open the native app (Universal / App Links). The hardcoded
	# cp list above skips them, so copy the dirs explicitly.
	cp -r web/site/join web/dist/join
	cp -r web/site/.well-known web/dist/.well-known
	# Site leaderboard (LEADERBOARD.md "site leaderboard"): a static page
	# that renders the board worker's daily snapshot and deep-links row
	# replays into /play/?replay= for in-browser playback.
	cp -r web/site/leaderboard web/dist/leaderboard

web-clean:
	rm -rf web/dist

# ============================================================
# Android build: make android / make android-install
# ============================================================
# Thin wrapper over the Gradle build in android/. The native half is built
# from the root CMakeLists.txt (see android/app/build.gradle); SDL2 and
# SDL2_mixer must be cloned as siblings to the repo root first (see
# .github/workflows/android.yml for the exact clone commands). Audio assets
# are copied into the app's assets dir here, matching the CI "Copy audio
# assets" step, so the APK ships its sounds.
ANDROID_DIR = android
ANDROID_ASSETS = $(ANDROID_DIR)/app/src/main/assets
ANDROID_APK = $(ANDROID_DIR)/app/build/outputs/apk/debug/app-debug.apk

.PHONY: android android-install android-assets android-clean

android-assets:
	mkdir -p $(ANDROID_ASSETS)
	rm -rf $(ANDROID_ASSETS)/audio
	cp -r audio $(ANDROID_ASSETS)/audio

# Build the debug APK. Output lands at $(ANDROID_APK).
android: android-assets
	cd $(ANDROID_DIR) && ./gradlew assembleDebug
	@echo "APK: $(ANDROID_APK)"

# Build, install AND (re)launch the debug APK on a connected device /
# running emulator — the same build-run loop as `make ios-install`
# (requires adb to see exactly one target; ANDROID_SERIAL=<serial>
# selects one when several are attached, adb's own convention).
android-install: android-assets
	cd $(ANDROID_DIR) && ./gradlew installDebug
	adb shell am force-stop org.newtonia
	adb shell am start -n org.newtonia/.NewtoniaActivity

android-clean:
	cd $(ANDROID_DIR) && ./gradlew clean
	rm -rf $(ANDROID_ASSETS)/audio

# ============================================================
# iOS device build: make ios / make ios-install
# ============================================================
# Command-line device build through the XcodeGen project — no Xcode GUI
# project configuration needed. One-time setup on the Mac:
#   brew install xcodegen
#   ./build_sdl_deps_ios.sh device       # SDL2 + SDL2_mixer -> sdl-libs-ios/
#   ./build_netplay_deps_ios.sh device   # MbedTLS + libdatachannel -> netplay-libs-ios/
#   Xcode > Settings > Accounts: sign in the Apple ID once (automatic
#   signing then mints/refreshes the dev profile from the CLI via
#   -allowProvisioningUpdates; the Debug config signs with
#   ios/EntitlementsDev.plist).
# `make ios` builds the signed .app; `make ios-install` installs + launches
# it on the connected device (auto-detected; override with
# IOS_DEVICE=<name-or-udid>; the phone needs Developer Mode on).
# Audio ships via the ios/project.yml folder resource, so the bundle is
# complete as signed — nothing is copied in afterwards.

IOS_TEAM ?= 4RWPRHJG6D
IOS_CONFIG ?= Debug
IOS_SDL_PREFIX = sdl-libs-ios
IOS_NET_PREFIX = netplay-libs-ios
IOS_DERIVED = ios/build
IOS_APP = $(IOS_DERIVED)/Build/Products/$(IOS_CONFIG)-iphoneos/Newtonia.app

.PHONY: ios ios-install ios-deps-check ios-clean

# Hard error on a missing prefix (same convention as the desktop netplay
# build — never a silent fallback).
ios-deps-check:
	@test -f $(IOS_SDL_PREFIX)/lib/libSDL2.a || { \
	  echo "error: $(IOS_SDL_PREFIX)/ missing or incomplete —" ; \
	  echo "       build it ONCE with: ./build_sdl_deps_ios.sh device" ; \
	  exit 1 ; }
	@test -n "$(wildcard $(IOS_NET_PREFIX)/lib/*.a)" || { \
	  echo "error: $(IOS_NET_PREFIX)/ missing or incomplete —" ; \
	  echo "       build it ONCE with: ./build_netplay_deps_ios.sh device" ; \
	  exit 1 ; }
	@grep -q caCertificatePemFile $(IOS_NET_PREFIX)/include/rtc/rtc.h || { \
	  echo "error: $(IOS_NET_PREFIX) predates the WS CA patch —" ; \
	  echo "       rebuild it with: ./build_netplay_deps_ios.sh device" ; \
	  exit 1 ; }

ios: ios-deps-check
	cd ios && xcodegen generate
	@set -e; \
	DEST="generic/platform=iOS"; \
	JSON=$$(mktemp); \
	if xcrun devicectl list devices --json-output "$$JSON" >/dev/null 2>&1; then \
	  UDID=$$(python3 -c "import json,sys;ds=json.load(open(sys.argv[1]))['result']['devices'];u=lambda d:d.get('hardwareProperties',{}).get('udid') or '';c=[d for d in ds if d.get('connectionProperties',{}).get('tunnelState')=='connected'];print(next((u(d) for d in c+ds if u(d)),''))" "$$JSON"); \
	  if [ -n "$$UDID" ]; then DEST="platform=iOS,id=$$UDID"; fi; \
	fi; \
	rm -f "$$JSON"; \
	echo "Build destination: $$DEST"; \
	xcodebuild build \
	  -project ios/Newtonia-iOS.xcodeproj \
	  -scheme Newtonia \
	  -configuration $(IOS_CONFIG) \
	  -destination "$$DEST" \
	  -derivedDataPath $(IOS_DERIVED) \
	  -allowProvisioningUpdates \
	  -allowProvisioningDeviceRegistration \
	  DEVELOPMENT_TEAM=$(IOS_TEAM) \
	  "SDL2_HEADER_PATH=$(abspath $(IOS_SDL_PREFIX))/include/SDL2" \
	  "SDL2_MIXER_HEADER_PATH=$(abspath $(IOS_SDL_PREFIX))/include/SDL2" \
	  "NEWTONIA_NET_DEFINE=NEWTONIA_NET_RTC=1" \
	  "NEWTONIA_NET_HEADER_PATH=$(abspath $(IOS_NET_PREFIX))/include" \
	  $(IOS_VERSION_SETTING) \
	  "OTHER_LDFLAGS=-ObjC $(abspath $(IOS_SDL_PREFIX))/lib/libSDL2main.a $(abspath $(IOS_SDL_PREFIX))/lib/libSDL2.a $(abspath $(IOS_SDL_PREFIX))/lib/libSDL2_mixer.a $(abspath $(wildcard $(IOS_NET_PREFIX)/lib/*.a))"
	@echo "App: $(IOS_APP)"

# Install + launch on a device. Auto-picks the first connected device from
# devicectl (prefers an active tunnel); IOS_DEVICE=<name-or-udid> overrides.
ios-install: ios
	@set -e; \
	DEV="$(IOS_DEVICE)"; \
	if [ -z "$$DEV" ]; then \
	  JSON=$$(mktemp); \
	  xcrun devicectl list devices --json-output "$$JSON" >/dev/null; \
	  DEV=$$(python3 -c "import json;ds=json.load(open('$$JSON'))['result']['devices'];print(next((d['identifier'] for d in ds if d.get('connectionProperties',{}).get('tunnelState')=='connected'),ds[0]['identifier'] if ds else ''))"); \
	  rm -f "$$JSON"; \
	fi; \
	test -n "$$DEV" || { echo "error: no device found — plug one in or set IOS_DEVICE=<name-or-udid>"; exit 1; }; \
	echo "Installing to device $$DEV"; \
	xcrun devicectl device install app --device "$$DEV" "$(IOS_APP)"; \
	xcrun devicectl device process launch --terminate-existing --device "$$DEV" cc.gfm.Newtonia

ios-clean:
	rm -rf $(IOS_DERIVED)

# ============================================================
# Steam build (local testing): make steam
# ============================================================
# Requires the Steamworks SDK unzipped at ./sdk (so that
# sdk/public/steam/steam_api.h exists). Produces ./newtonia-steam plus
# steam_appid.txt and the Steam runtime library beside it, so it can be
# launched directly from the repo root with the Steam client running.
# Steam objects build as *.steam.o so they never mix with the plain
# build's objects (different -D flags). The deploy-steam workflow remains
# the source of truth for shippable depots — this target is for local
# achievement/overlay testing only (never ship steam_appid.txt).
STEAM_APPID ?= 4536720
STEAM_SDK ?= sdk
# Missing-SDK guard, at PARSE time like the netplay one above. It used to be
# a `check-steam-sdk` prerequisite of the steam target, which `make steam -j8`
# defeated: prerequisites run in PARALLEL, so the compile reached
# steam_build.h's #include <steam/steam_api.h> and died on the raw header
# error before the friendly message ever printed (field, 2026-08-11).
ifneq ($(filter steam newtonia-steam,$(MAKECMDGOALS)),)
  ifeq ($(wildcard $(STEAM_SDK)/public/steam/steam_api.h),)
    $(error Steamworks SDK not found at ./$(STEAM_SDK)/ — download it from \
partner.steamgames.com (Downloads -> Steamworks SDK) and unzip it in the repo \
root, which creates ./sdk/, or point STEAM_SDK at an existing unzip)
  endif
endif
STEAM_CFLAGS = $(CFLAGS) -DSTEAM_BUILD -I$(STEAM_SDK)/public
# Object-name tag: `steam` for the host build, `sniper` when
# build_steam_sniper.sh builds inside Valve's runtime container, so the two
# never share (glibc-incompatible) objects.
STEAM_OBJ_TAG ?= steam
STEAM_OBJFILES := $(patsubst %.cpp,%.$(STEAM_OBJ_TAG).o,$(ALL_SRCS))
ifeq ($(UNAME), Darwin)
  STEAM_OBJFILES += macos_window.$(STEAM_OBJ_TAG).o
  STEAM_RUNTIME = libsteam_api.dylib
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/osx/libsteam_api.dylib
  STEAM_LINK = -L. -lsteam_api
else ifneq (,$(findstring _NT,$(UNAME)))
  # Windows (MSYS2 MINGW64): the SDK ships steam_api64.dll. MinGW's linker
  # links directly against the DLL — no gendef/dlltool import library needed
  # — and the DLL rides beside newtonia-steam.exe at runtime (copied by the
  # $(STEAM_RUNTIME) rule below). The Windows _NT netplay branch above already
  # put SDL/GL/libdatachannel/Winsock into $(LIBS); this only adds the Steam
  # DLL. Mirrors the deploy-steam Windows job.
  STEAM_RUNTIME = steam_api64.dll
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/win64/steam_api64.dll
  STEAM_LINK = ./steam_api64.dll
else
  STEAM_RUNTIME = libsteam_api.so
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/linux64/libsteam_api.so
  STEAM_LINK = -L. -lsteam_api -Wl,-rpath,'$$ORIGIN'
endif
STEAM_DEPFILES := $(STEAM_OBJFILES:.o=.d)

.PHONY: steam steam-clean

steam: newtonia-steam steam_appid.txt
ifeq ($(UNAME), Darwin)
	# Also wrap the Steam binary in Newtonia.app so macOS treats it as a real
	# app: window activation/focus, Game Mode, and App Nap suppression all key
	# off the bundle's Info.plist (macos_window.mm + Newtonia-Info.plist), and
	# the Steam overlay only injects into a bundled, Steam-launched app.
	# Mirrors the deploy-steam macOS layout. The Steam runtime dylib sits
	# beside the executable (its id is @loader_path/libsteam_api.dylib);
	# steam_appid.txt rides along for standalone (non-Steam) launches. The
	# bare ./newtonia-steam at the repo root still works too (run from here so
	# steam_appid.txt is in the CWD). A netplay build resolves libdatachannel
	# through its absolute netplay-libs rpath, so no extra copy is needed for
	# local testing.
	mkdir -p Newtonia.app/Contents/MacOS Newtonia.app/Contents/Resources
	cp newtonia-steam Newtonia.app/Contents/MacOS/Newtonia
	cp $(STEAM_RUNTIME) Newtonia.app/Contents/MacOS/$(STEAM_RUNTIME)
	cp steam_appid.txt Newtonia.app/Contents/MacOS/steam_appid.txt
	rm -rf Newtonia.app/Contents/Resources/audio
	cp -r audio Newtonia.app/Contents/Resources/audio
	cp steam_appid.txt Newtonia.app/Contents/Resources/steam_appid.txt
	cp icon.icns Newtonia.app/Contents/Resources/icon.icns
	sed 's/$${EXECUTABLE_NAME}/Newtonia/g' Newtonia-Info.plist > Newtonia.app/Contents/Info.plist
	@echo "Bundled Newtonia.app (Steam build) - launch via Steam for overlay/presence."
endif

$(STEAM_RUNTIME): $(STEAM_SDK_LIB)
	cp $< $@
ifeq ($(UNAME), Darwin)
	install_name_tool -id @loader_path/libsteam_api.dylib $@
endif

newtonia-steam: $(STEAM_OBJFILES) $(STEAM_RUNTIME)
	$(CC) -o $@ $(STEAM_OBJFILES) $(STEAM_LINK) $(LIBS)

steam_appid.txt:
	echo $(STEAM_APPID) > $@

steam-clean:
	rm -rf $(STEAM_OBJFILES) $(STEAM_DEPFILES) newtonia-steam newtonia-steam.exe $(STEAM_RUNTIME) steam_appid.txt Newtonia.app \
	  $(wildcard *.sniper.o */*.sniper.o *.sniper.d */*.sniper.d)

%.$(STEAM_OBJ_TAG).o: %.cpp
	$(CC) $(STEAM_CFLAGS) -c -o $@ $<

%.$(STEAM_OBJ_TAG).o: %.mm
	$(CC) $(STEAM_CFLAGS) -c -o $@ $<

-include $(DEPFILES)
-include $(STEAM_DEPFILES)

%.o: %.cpp
	$(COMPILE) -o $@ $<

%.o: %.mm
	$(CC) $(CFLAGS) -c -o $@ $<
