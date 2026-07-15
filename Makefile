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
  LIBS = -lglut -lGL -lGLU -lX11 $(SDL2_LIBS)
  ALL_SRCS := $(filter-out $(ANDROID_SRCS),$(wildcard *.cpp) $(wildcard */*.cpp))
endif

# Optional native netplay backend (macOS/Linux) — see NETPLAY.md.
# libdatachannel is not packaged by Homebrew; build it from source first:
#   ./build_netplay_deps.sh              (--universal for `make osx`)
#   make NETPLAY=1
# NETPLAY_PREFIX defaults to the script's install dir (./netplay-libs);
# pass it only for a prefix elsewhere. Defines NEWTONIA_NET_RTC (activates
# net_transport_rtc.cpp and the menu's ONLINE row) and links the
# libdatachannel C API.
ifeq ($(NETPLAY),1)
  NETPLAY_PREFIX ?= $(CURDIR)/netplay-libs
  ifeq ($(filter clean web-clean,$(MAKECMDGOALS)),)
    ifeq ($(wildcard $(NETPLAY_PREFIX)/include/rtc/rtc.h),)
      $(error NETPLAY=1 but $(NETPLAY_PREFIX)/include/rtc/rtc.h is missing — \
run ./build_netplay_deps.sh first (--universal for `make osx`), or point \
NETPLAY_PREFIX at an existing install)
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

# --- macOS universal bundle ----------------------------------------------
# Two whole-program compiles (arm64 + x86_64) lipo'd together, mirroring
# the CI recipe. The x86_64 half links against the Rosetta Homebrew tree
# (/usr/local) — install it plus sdl2/sdl2_mixer there for local universal
# builds. With NETPLAY=1 the prefix must hold a UNIVERSAL libdatachannel:
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
EMCC = emcc

# Exclude desktop and Android entry points; add web entry point
WEB_EXCL = glut.cpp android_main.cpp
WEB_SRCS := $(filter-out $(WEB_EXCL), $(wildcard *.cpp) $(wildcard */*.cpp))

# GROWABLE_ARRAYBUFFERS (default-on since emscripten 6.0.2) breaks Firefox:
# its TextDecoder rejects views over resizable ArrayBuffers, crashing
# UTF8ToString at startup. Requires emcc >= 4.0.12 to recognise the setting.
# ccall is used by the netplay test hooks (nwtest_* in net_transport_web.cpp)
# to pass SDP strings from JS; harmless to production pages.
WEB_FLAGS = -std=c++11 -O2 \
            -s USE_SDL=2 \
            -s USE_SDL_MIXER=2 \
            -s SDL2_MIXER_FORMATS='["wav","mp3"]' \
            -s FULL_ES2=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s GROWABLE_ARRAYBUFFERS=0 \
            -s EXPORTED_RUNTIME_METHODS='["ccall"]' \
            -lidbfs.js \
            --shell-file web/shell.html

WEB_FLAGS += --preload-file audio@audio

.PHONY: web web-clean

# Landing page (web/site/) is served at the root; the playable game lives at /play.
web:
	mkdir -p web/dist/play
	tsc -p web/tsconfig.json
	$(EMCC) $(WEB_SRCS) $(WEB_FLAGS) -o web/dist/play/index.html
	cp web/main.js web/dist/play/main.js
	cp web/site/index.html web/site/styles.css web/site/site.js web/site/icon.png web/dist/
	cp web/site/CNAME web/dist/CNAME
	# Universal join link (invites.h): the /join landing page + the
	# apple-app-site-association / assetlinks.json association files that make
	# a tapped link open the native app (Universal / App Links). The hardcoded
	# cp list above skips them, so copy the dirs explicitly.
	cp -r web/site/join web/dist/join
	cp -r web/site/.well-known web/dist/.well-known

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

# Build and install the debug APK onto a connected device / running emulator
# (requires adb to see exactly one target).
android-install: android-assets
	cd $(ANDROID_DIR) && ./gradlew installDebug

android-clean:
	cd $(ANDROID_DIR) && ./gradlew clean
	rm -rf $(ANDROID_ASSETS)/audio

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
STEAM_CFLAGS = $(CFLAGS) -DSTEAM_BUILD -I$(STEAM_SDK)/public
STEAM_OBJFILES := $(patsubst %.cpp,%.steam.o,$(ALL_SRCS))
ifeq ($(UNAME), Darwin)
  STEAM_OBJFILES += macos_window.steam.o
  STEAM_RUNTIME = libsteam_api.dylib
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/osx/libsteam_api.dylib
  STEAM_LINK = -L. -lsteam_api
else
  STEAM_RUNTIME = libsteam_api.so
  STEAM_SDK_LIB = $(STEAM_SDK)/redistributable_bin/linux64/libsteam_api.so
  STEAM_LINK = -L. -lsteam_api -Wl,-rpath,'$$ORIGIN'
endif
STEAM_DEPFILES := $(STEAM_OBJFILES:.o=.d)

.PHONY: steam steam-clean check-steam-sdk

steam: check-steam-sdk newtonia-steam steam_appid.txt
ifeq ($(UNAME), Darwin)
	# Also wrap the Steam binary in Newtonia.app so macOS treats it as a real
	# app: window activation/focus, Game Mode, and App Nap suppression all key
	# off the bundle's Info.plist (macos_window.mm + Newtonia-Info.plist), and
	# the Steam overlay only injects into a bundled, Steam-launched app.
	# Mirrors the deploy-steam macOS layout. The Steam runtime dylib sits
	# beside the executable (its id is @loader_path/libsteam_api.dylib);
	# steam_appid.txt rides along for standalone (non-Steam) launches. The
	# bare ./newtonia-steam at the repo root still works too (run from here so
	# steam_appid.txt is in the CWD). A NETPLAY=1 build resolves libdatachannel
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

check-steam-sdk:
	@test -f $(STEAM_SDK)/public/steam/steam_api.h || { \
	  echo "Steamworks SDK not found at ./$(STEAM_SDK)/."; \
	  echo "Unzip steamworks_sdk.zip in the repo root (creates ./sdk/) and retry."; \
	  exit 1; }

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
	rm -rf $(STEAM_OBJFILES) $(STEAM_DEPFILES) newtonia-steam $(STEAM_RUNTIME) steam_appid.txt Newtonia.app

%.steam.o: %.cpp
	$(CC) $(STEAM_CFLAGS) -c -o $@ $<

%.steam.o: %.mm
	$(CC) $(STEAM_CFLAGS) -c -o $@ $<

-include $(DEPFILES)
-include $(STEAM_DEPFILES)

%.o: %.cpp
	$(COMPILE) -o $@ $<

%.o: %.mm
	$(CC) $(CFLAGS) -c -o $@ $<
