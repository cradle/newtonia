#!/usr/bin/env python3
"""Exercise private-relay preflight failures without X11, a game, or Wrangler."""
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DRIVERS = (
    "identity_attested", "identity_tick", "lan_anon",
    "nseat_anon", "nseat_ban_token", "nseat_kick",
)


def executable(path, content):
    path.write_text(content)
    path.chmod(0o755)


def running(pid):
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def scenario(driver, game_present):
    with tempfile.TemporaryDirectory(prefix="relay-startup-") as tmp:
        root = Path(tmp)
        e2e = root / "test/e2e"
        e2e.mkdir(parents=True)
        (root / "signal").mkdir()
        bin_dir = root / "bin"
        bin_dir.mkdir()
        for name in (driver + ".sh", "lib.sh"):
            shutil.copy2(ROOT / "test/e2e" / name, e2e / name)
        if game_present:
            executable(root / "newtonia", "#!/bin/sh\nexit 99\n")
            executable(bin_dir / "mktemp", "#!/bin/sh\nexit 1\n")
        record = root / "relay.json"
        # Model npx -> worker: killing just the wrapper leaks the worker.
        executable(bin_dir / "npx", '''#!/usr/bin/env python3
import json, os, pathlib, subprocess, sys
child = subprocess.Popen(["sleep", "60"])
state = sys.argv[sys.argv.index("--persist-to") + 1]
pathlib.Path(os.environ["RELAY_RECORD"]).write_text(
    json.dumps({"pids": [os.getpid(), child.pid], "state": state}))
child.wait()
''')
        # Let an unfixed driver reach the missing-game check after relay startup.
        executable(bin_dir / "curl", '''#!/usr/bin/env python3
import os, pathlib, time
for _ in range(500):
    if pathlib.Path(os.environ["RELAY_RECORD"]).exists():
        break
    time.sleep(0.01)
''')
        env = dict(os.environ, DISPLAY=":test", RELAY_RECORD=str(record),
                   NEWTONIA_TEST_OUT=str(root / "out"),
                   PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
        data = None
        try:
            result = subprocess.run(["bash", str(e2e / (driver + ".sh"))],
                                    env=env, capture_output=True, text=True,
                                    timeout=15)
            if record.exists():
                data = json.loads(record.read_text())
            assert result.returncode == 1, result.stdout + result.stderr
            expected = ("could not create private relay state" if game_present
                        else "missing - build first")
            assert expected in result.stdout, result.stdout
            assert data is None, (driver, "started a relay despite failed preflight",
                                 data, "live pids",
                                 [pid for pid in data["pids"] if running(pid)])
        finally:
            if data is None and record.exists():
                data = json.loads(record.read_text())
            if data:
                for pid in data["pids"]:
                    try:
                        os.kill(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                shutil.rmtree(data["state"], ignore_errors=True)


if __name__ == "__main__":
    for driver in DRIVERS:
        scenario(driver, game_present=False)
        scenario(driver, game_present=True)
    print("Private-relay preflight checks passed (6 drivers, missing game + failed mktemp)")
