#!/usr/bin/env python3
"""Check actual Ship stick groups in every bundled Steam Input layout."""
from pathlib import Path
import re


def parse_vdf(text):
    # Preserve duplicate keys: Valve exports repeat the top-level group/preset.
    tokens = re.findall(r'"((?:\\.|[^"\\])*)"|([{}])', text)
    tokens = iter(brace or string for string, brace in tokens)

    def block(nested=False):
        entries = []
        for key in tokens:
            if key == '}':
                assert nested, 'unexpected closing brace'
                return entries
            value = next(tokens)
            entries.append((key, block(True) if value == '{' else value))
        assert not nested, 'unclosed block'
        return entries

    return block()


def get(entries, key):
    values = [value for name, value in entries if name == key]
    assert len(values) == 1, f'expected one {key}, got {len(values)}'
    return values[0]


root = Path(__file__).resolve().parents[2]
layouts = sorted((root / 'steam').glob('controller_*.vdf'))
assert layouts, 'no bundled layouts'
for path in layouts:
    layout = parse_vdf(path.read_text())[0][1]
    groups = {get(value, 'id'): value for key, value in layout if key == 'group'}
    ship = next(value for key, value in layout
                if key == 'preset' and get(value, 'name') == 'Ship')
    sources = get(ship, 'group_source_bindings')
    for source, action in [('joystick active', 'steer'), ('right_joystick active', 'camera')]:
        ids = [key for key, value in sources if value == source]
        assert len(ids) == 1, (path.name, source, ids)
        group = groups[ids[0]]
        assert get(group, 'mode') == 'joystick_move', path.name
        assert get(get(group, 'gameactions'), 'Ship') == action, (path.name, source)
        if action == 'camera':
            assert 'game_action Ship help,' in str(get(group, 'inputs')), path.name
    actions = get(get(layout, 'actions'), 'Ship')
    camera = get(get(actions, 'StickPadGyro'), 'camera')
    assert get(camera, 'input_mode') == 'joystick_move', path.name
    print(f'{path.name}: camera axes, steering and right-stick click PASS')
