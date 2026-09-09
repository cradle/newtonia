# Agent instructions

## Build before reporting that a build is unavailable

For code changes and PR reviews, inspect the available toolchain and attempt the
relevant build automatically. Install or stage ordinary build dependencies when
permitted. Do not infer that compilation is unavailable merely because
`pkg-config`, CMake, or development headers are missing.

Read `CLAUDE.md` for project build conventions and `TESTING.md` for relevant
tests. Preserve existing work. For a PR review, use a separate checkout at the
PR's exact head SHA and report that SHA with the build result.

## Linux build

Check the environment first:

```sh
uname -s
cat /etc/os-release
for tool in make g++ git sdl2-config pkg-config cmake curl python3 dpkg-deb; do
  command -v "$tool" || true
done
git rev-parse HEAD
git status --short
```

The ordinary desktop Makefile requires GNU Make, G++, SDL2, SDL2_mixer,
FreeGLUT, OpenGL/GLU, X11, and Xi development files. CMake and pkg-config are
not required for the Linux build with netplay disabled.

On Ubuntu, prefer the normal package installation when available (use `sudo`
only when needed and available). Install the compiler toolchain below, then
follow [CLAUDE.md — Linux dependencies](CLAUDE.md#linux-dependencies) for the
application packages (the CMake/OpenSSL command there is only for netplay):

```sh
apt-get update
apt-get install -y build-essential
```

On Ubuntu 24.04, `libsdl2-dev` pulls in `libx11-dev` and `libxi-dev`.
These are direct Newtonia dependencies (`-lX11`, `-lXi`, and `XInput2.h`),
so install them explicitly if another release's SDL2 package no longer supplies
them transitively.

For audio changes and other changes independent of networking, the documented
build without netplay is a useful compile/link check:

```sh
make NETPLAY=0 -j4
```

The default `make` enables netplay and requires the project's patched
libdatachannel. For networking changes or a requested default/release build,
follow `CLAUDE.md` and `build_netplay_deps.sh`, then build with netplay enabled.
Always state which configuration was built.

## Fallback when apt cannot install packages

If apt reports `setgroups`, `setegid`, or `seteuid` errors while switching
to its `_apt` download user, first try the following as root (or prepend
`sudo` when needed and available). Use the same option on the application
package installation commands from
[CLAUDE.md — Linux dependencies](CLAUDE.md#linux-dependencies):

```sh
apt-get -o APT::Sandbox::User=root update
apt-get -o APT::Sandbox::User=root install -y build-essential
```

This disables apt's download privilege drop for those commands; it does not
grant installation privileges or fix unrelated apt failures. The original
PR #529 environment was not retested with this option.

### Last resort: extract packages into a local prefix

When package installation remains unavailable, download `.deb` files, verify
their SHA-256 hashes against Ubuntu's package index, and extract them with
`dpkg-deb -x`. This does not run package install scripts.

The original recipe built on one **Ubuntu 24.04 (noble), amd64** host for
PR #529. A later clean-host review reported a link failure because the staged
PulseAudio private-library directory was missing from the search path. The
build block below includes that directory. Separate historical and revised
recipe verification results are recorded at the end.

Check the host first; adapt the distribution, architecture, and package names
for other hosts. This is a focused build prefix, not a general package manager:
it uses installed runtime libraries and does not resolve package version
constraints or all development dependencies. Keep the runtime dependency walk:
shared libraries can require other libraries through `DT_NEEDED` even when
those libraries do not appear in the Makefile's explicit `-l` arguments.
If an additional header/library is missing, stage its matching package as well.

Run from the repository root. Keep the prefix and logs outside the repository;
reuse a compatible existing prefix rather than downloading it again. The host
still needs `make`, `g++`, `curl`, `python3`, `dpkg-query`, and `dpkg-deb`, plus
CA certificates for HTTPS; this recipe stages the application dependencies.
The runtime walk can also fetch large Mesa/LLVM packages on a minimal host.
Downloads allow up to ten minutes per attempt and retry transient failures
twice; already downloaded packages are reused after checksum verification.

```sh
export NEWTONIA_DEPS_DIR="$(dirname "$PWD")/newtonia-linux-deps"
mkdir -p "$NEWTONIA_DEPS_DIR"
for component in main universe; do
  curl -fsSL --connect-timeout 15 --max-time 600 --retry 2 \
    "https://archive.ubuntu.com/ubuntu/dists/noble/$component/binary-amd64/Packages.xz" \
    -o "$NEWTONIA_DEPS_DIR/$component.xz"
done

python3 - <<'PY'
import hashlib
import lzma
import os
from pathlib import Path
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor

root = Path(os.environ['NEWTONIA_DEPS_DIR'])
packages = {}
for component in ('main', 'universe'):
    index = lzma.decompress((root / (component + '.xz')).read_bytes()).decode()
    for stanza in index.split('\n\n'):
        fields = {}
        for line in stanza.splitlines():
            if line and not line[0].isspace() and ': ' in line:
                key, value = line.split(': ', 1)
                fields[key] = value
        if 'Package' in fields:
            packages[fields['Package']] = fields

result = subprocess.run(
    ['dpkg-query', '-W', '-f=${Package} ${Status}\n'],
    capture_output=True, text=True, check=True)
installed = {line.split()[0] for line in result.stdout.splitlines()
             if line.endswith('install ok installed')}
queue = [
    'libsdl2-dev', 'libsdl2-2.0-0', 'libsdl2-mixer-dev', 'libsdl2-mixer-2.0-0',
    'libglut-dev', 'libglut3.12', 'libx11-dev', 'libxi-dev',
    'libgl-dev', 'libgl1', 'libglx-dev', 'libglx0',
    'libglu1-mesa-dev', 'libglu1-mesa',
    'libxext-dev', 'libxfixes-dev', 'x11proto-dev',
]
# libglu1-mesa needs an explicit seed: -dev dependencies are not traversed,
# and the other noble runtime packages do not pull it in. SDL2 and GL/GLX
# runtimes are listed for explicitness; mixer/GLUT already pull them in.
# Keep transitive runtime dependencies. The PulseAudio link fix is the
# LD_LIBRARY_PATH below, not these redundant seeds.
selected = {}
while queue:
    name = queue.pop(0)
    if name in selected or name in installed:
        continue
    package = packages.get(name)
    if package is None:
        raise RuntimeError('Not in index: ' + name)
    selected[name] = package
    if name.endswith('-dev'):
        continue  # Required development packages are listed explicitly above.
    dependencies = package.get('Pre-Depends', '') + ',' + package.get('Depends', '')
    for dependency in dependencies.split(','):
        alternatives = [re.split(r'[ (:\[]', item.strip())[0]
                        for item in dependency.split('|') if item.strip()]
        if not alternatives or any(item in installed for item in alternatives):
            continue
        candidate = next((item for item in alternatives if item in packages), None)
        if candidate is None:
            raise RuntimeError('Resolve missing dependency: ' + dependency)
        queue.append(candidate)

def download(item):
    name, package = item
    destination = root / Path(package['Filename']).name
    expected = package['SHA256']
    if (not destination.exists()
            or hashlib.sha256(destination.read_bytes()).hexdigest() != expected):
        subprocess.run([
            'curl', '-fsSL', '--connect-timeout', '15', '--max-time', '600',
            '--retry', '2',
            'https://archive.ubuntu.com/ubuntu/' + package['Filename'],
            '-o', str(destination)], check=True)
    if hashlib.sha256(destination.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Checksum mismatch: ' + name)
    print('Verified', name, flush=True)
    return destination

with ThreadPoolExecutor(max_workers=6) as pool:
    archives = list(pool.map(download, selected.items()))
for archive in archives:
    subprocess.run(['dpkg-deb', '-x', str(archive), str(root / 'prefix')], check=True)

# Development-library symlinks can point to a runtime already installed on
# the host. Add local references to those existing runtimes where necessary.
lib = root / 'prefix/usr/lib/x86_64-linux-gnu'
for entry in lib.iterdir():
    if entry.is_symlink() and not entry.exists():
        target_name = Path(os.readlink(entry)).name
        system = Path('/usr/lib/x86_64-linux-gnu') / target_name
        local = lib / target_name
        if system.exists() and not local.exists() and not local.is_symlink():
            local.symlink_to(system)
PY
```

Build in a subshell so the local library paths do not affect later commands:

```sh
(
  export NEWTONIA_BUILD_PREFIX="$NEWTONIA_DEPS_DIR/prefix/usr"
  export PATH="$NEWTONIA_BUILD_PREFIX/bin:$PATH"
  export CPATH="$NEWTONIA_BUILD_PREFIX/include:$NEWTONIA_BUILD_PREFIX/include/x86_64-linux-gnu${CPATH:+:$CPATH}"
  export LIBRARY_PATH="$NEWTONIA_BUILD_PREFIX/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
  export LD_LIBRARY_PATH="$NEWTONIA_BUILD_PREFIX/lib/x86_64-linux-gnu:$NEWTONIA_BUILD_PREFIX/lib/x86_64-linux-gnu/pulseaudio${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  make NETPLAY=0 -j4 \
    > "$NEWTONIA_DEPS_DIR/build.log" 2>&1
  build_status=$?
  tail -n 35 "$NEWTONIA_DEPS_DIR/build.log"
  exit "$build_status"
)
```

The staged `sdl2-config` on PATH supplies the SDL2 compiler flags. Do not
override the Makefile's complete `CFLAGS`, so the project's compiler flags and
version stamping remain intact. The desktop link rule does not consume
`LDFLAGS`; `LD_LIBRARY_PATH` above includes PulseAudio's private library
directory, which the native linker searches to resolve `libpulse.so.0`'s
`DT_NEEDED` dependencies. Adding that directory to `LIBRARY_PATH` alone does
not resolve them. Runtime checks must use the same `LD_LIBRARY_PATH`
inside this subshell (or recreate that environment); a successful link alone
does not prove the executable starts or audio works. If changing
dependency prefixes after an earlier build, clean generated build outputs
before rebuilding to avoid reusing incompatible objects.

## Verification and reporting

- Check the actual make exit status and that the final executable linked.
- Report compiler/linker warnings and errors, and distinguish missing tools or
  dependencies from source-code failures. Do not stop at the first missing
  dependency when an ordinary permitted installation or local fallback works.
- Select relevant existing tests using `TESTING.md`; compilation alone does
  not establish runtime correctness. Never claim that audio clipping was
  verified solely because the build passed.
- Record the reviewed SHA, build configuration, dependency versions, and
  remaining limitations. Keep package downloads, prefixes, logs, and generated
  binaries out of commits.

Historical verification (original recipe, one host): PR #529, commit
`cd3749cf0ecd86c8d49d55e573e1e266ec03a285`,
built and linked successfully with GNU Make 4.3, G++, SDL2 2.30.0, and
SDL_mixer 2.8.0 using the local-prefix fallback and `NETPLAY=0 -j4`.
There were no compiler warnings or errors. Audio playback was not tested.

Revised fallback verification (2026-09-09): source commit
`46e63ec3be2a8cfdbc659864e156b43f424f6372` with these documentation changes,
in a fresh Ubuntu 24.04 amd64 container with only the compiler/download tools
installed. `dpkg-query` confirmed that neither `libpulse0` nor
`libsdl2-2.0-0` was installed system-wide, before staging and after the build.
The recipe built and linked with `NETPLAY=0 -j4`, GNU Make 4.3, G++ 13.3.0,
SDL2 2.30.0, and SDL_mixer 2.8.0, with no compiler/linker warnings or errors.
The staged `sdl2-config --cflags` emitted the prefix's SDL2 include directory
without an override. `ldd ./newtonia` under the documented `LD_LIBRARY_PATH`
reported no missing libraries. Gameplay, graphics, and audio were not tested.

The same container subsequently passed apt update/install with
`APT::Sandbox::User=root`, using `build-essential` and CLAUDE.md's application
package list, then a clean `make NETPLAY=0 -j4` with no compiler/linker warnings
or errors. This checks the option and package list on that container; it does
not reproduce the original PR #529 privilege-drop failure.
