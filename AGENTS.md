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
only when needed and available):

```sh
apt-get update
apt-get install -y build-essential libsdl2-dev libsdl2-mixer-dev \
  freeglut3-dev libx11-dev libxi-dev
```

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

In the cloud environment used for PR #529, `apt-get update` failed with
`setgroups`, `setegid`, and `seteuid` permission errors. Direct HTTPS downloads
from Ubuntu's archive worked. The successful fallback was to download `.deb`
files, verify their SHA-256 hashes against Ubuntu's package index, and extract
them with `dpkg-deb -x` into a local prefix. This does not run package install
scripts or require changes to apt's sandbox settings.

The following recipe was verified on **Ubuntu 24.04 (noble), amd64**. Check the
host first; adapt the distribution, architecture, and package names for other
hosts. This is a focused build prefix, not a general package manager: it uses
installed runtime libraries and does not resolve package version constraints
or all development dependencies. If an additional header/library is missing,
stage its matching package as well.

Run from the repository root. Keep the prefix and logs outside the repository;
reuse a compatible existing prefix rather than downloading it again.

```sh
export NEWTONIA_DEPS_DIR="$(dirname "$PWD")/newtonia-linux-deps"
mkdir -p "$NEWTONIA_DEPS_DIR"
for component in main universe; do
  curl -fsSL --connect-timeout 15 --max-time 90 \
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
    'libsdl2-dev', 'libsdl2-mixer-dev', 'libsdl2-mixer-2.0-0',
    'libglut-dev', 'libglut3.12', 'libx11-dev', 'libxi-dev',
    'libgl-dev', 'libglx-dev', 'libglu1-mesa-dev',
    'libxext-dev', 'libxfixes-dev', 'x11proto-dev',
]
selected = {}
while queue:
    name = queue.pop(0)
    if name in selected or name in installed:
        continue
    package = packages[name]
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
            'curl', '-fsSL', '--connect-timeout', '15', '--max-time', '90',
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
  export LD_LIBRARY_PATH="$NEWTONIA_BUILD_PREFIX/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  make NETPLAY=0 -j4 \
    SDL2_CFLAGS="-I$NEWTONIA_BUILD_PREFIX/include/SDL2 -D_REENTRANT" \
    > "$NEWTONIA_DEPS_DIR/build.log" 2>&1
  build_status=$?
  tail -n 35 "$NEWTONIA_DEPS_DIR/build.log"
  exit "$build_status"
)
```

Override only `SDL2_CFLAGS`, not the Makefile's complete `CFLAGS`, so the
project's compiler flags and version stamping remain intact. If changing
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

Verified example: PR #529, commit `cd3749cf0ecd86c8d49d55e573e1e266ec03a285`,
built and linked successfully with GNU Make 4.3, G++, SDL2 2.30.0, and
SDL_mixer 2.8.0 using the local-prefix fallback and `NETPLAY=0 -j4`.
There were no compiler warnings or errors. Audio playback was not tested.
