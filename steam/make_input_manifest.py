#!/usr/bin/env python3
"""Generate steam/steam_input_manifest.vdf — the Steam Input ACTION MANIFEST.

Three files, three jobs (STEAMINPUT.md §10):
  game_actions_4536720.vdf   the In-Game Actions file: the action sets and
                             their localization (hand-maintained, pinned to
                             pad.h by test/unit/pad_actions_test.cpp)
  controller_<type>.vdf      one exported default layout per controller type
                             (from the client's layout editor)
  steam_input_manifest.vdf   THIS: the actions + a "configurations" block
                             naming those layouts by depot-relative path.
                             What the Steamworks Steam Input page asks for
                             ("path to your Steam Input action manifest file
                             relative to your game install directory") and
                             what ISteamInput::SetInputActionManifestFilePath
                             takes — it refused the actions file itself.

Run from the repo root after adding or changing a layout:

    python3 steam/make_input_manifest.py

Lists a configuration for every steam/controller_*.vdf that EXISTS, so the
manifest never names a file the depot will not carry. Deterministic output;
commit the result beside the layouts.
"""
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IGA = os.path.join(HERE, "game_actions_4536720.vdf")
OUT = os.path.join(HERE, "steam_input_manifest.vdf")

# Valve's controller_type names, keyed by our file name's <type> part.
TYPES = {
    "xbox360": "controller_xbox360",
    "xboxone": "controller_xboxone",
    "ps4": "controller_ps4",
    "ps5": "controller_ps5",
    "neptune": "controller_neptune",  # Steam Deck
    "switch_pro": "controller_switch_pro",
    "steamcontroller_gordon": "controller_steamcontroller_gordon",
    "generic": "controller_generic",
}


def main():
    with open(IGA) as f:
        iga = f.read()
    head = '"In Game Actions"'
    if not iga.lstrip().startswith(head):
        sys.exit("unexpected IGA head")
    # The IGA's body — everything inside its outer braces — is the
    # manifest's body too; the manifest just adds "configurations" first.
    body = iga.lstrip()[len(head):].strip()
    assert body[0] == "{" and body[-1] == "}", "IGA outer braces"
    inner = body[1:-1].rstrip("\n")

    configs = []
    for path in sorted(glob.glob(os.path.join(HERE, "controller_*.vdf"))):
        name = os.path.basename(path)
        kind = name[len("controller_"):-len(".vdf")]
        if kind not in TYPES:
            sys.exit("unknown controller type in %s (known: %s)" % (name, ", ".join(sorted(TYPES))))
        configs.append((TYPES[kind], name))

    out = ['"Action Manifest"', "{", '\t"version"\t"1"', '\t"configurations"', "\t{"]
    for i, (ctype, name) in enumerate(configs):
        out += ['\t\t"%d"' % i, "\t\t{",
                '\t\t\t"controller_type"\t"%s"' % ctype,
                '\t\t\t"path"\t"%s"' % name,
                '\t\t\t"enabled"\t"1"',
                "\t\t}"]
    out += ["\t}", inner, "}", ""]
    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print("wrote %s with %d configuration(s)%s" % (
        os.path.relpath(OUT), len(configs),
        "" if configs else " — add steam/controller_<type>.vdf exports and re-run"))


if __name__ == "__main__":
    main()
