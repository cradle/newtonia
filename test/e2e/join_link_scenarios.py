#!/usr/bin/env python3
"""Real-client invite contracts (field plan 1-7), against a local relay.

Python stdlib + Xvfb/xdotool/xclip + node >=22. Screenshots use xwd/convert or
Pillow/XCB. All profiles, processes and sockets belong to this driver.
SIGSTOP models a silent peer, not physical wifi loss. A per-host TCP proxy
cuts only that game's relay connection and holds reconnects for 20 s.
Neither technique changes host networking or another application's state.
"""
import argparse
import base64
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import signal
import socket
import socketserver
import subprocess
import tempfile
import threading
import time
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('NEWTONIA_TEST_OUT') or tempfile.mkdtemp(prefix='join-links-')).resolve()
OUT.mkdir(parents=True, exist_ok=True)
URL = os.environ.get('NEWTONIA_SIGNAL_URL', 'ws://127.0.0.1:8787/ws')
endpoint = urlparse(URL)
if endpoint.scheme != 'ws' or endpoint.hostname not in ('localhost', '127.0.0.1'):
    raise SystemExit('Use a local ws:// relay: faults must never target production.')
games = []
results = []


def check(label, condition):
    if not condition:
        raise AssertionError(label)
    print('PASS ' + label, flush=True)
    results.append(label)


def wait(predicate, label, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.1)
    raise AssertionError('Timeout: ' + label)


def xdo(*args):
    return subprocess.run(['xdotool', *map(str, args)], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def clear_clipboard():
    # This display is driver-owned. NONE relinquishes the CLIPBOARD owner.
    x = ctypes.CDLL('libX11.so.6')
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x.XInternAtom.restype = ctypes.c_ulong
    x.XSetSelectionOwner.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_ulong]
    x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    d = x.XOpenDisplay(None)
    if not d:
        raise RuntimeError('No X display')
    x.XSetSelectionOwner(d, x.XInternAtom(d, b'CLIPBOARD', 0), 0, 0)
    x.XSync(d, 0)
    x.XCloseDisplay(d)


class Game:
    def __init__(self, name, code=None, **env):
        self.name = name
        self.path = OUT / (name + '.log')
        self.file = self.path.open('w')
        config = dict(os.environ, SDL_AUDIODRIVER='dummy', NEWTONIA_NET_DEBUG='1',
                      NEWTONIA_SIGNAL_URL=URL, NEWTONIA_NET_TEST_SEATS='4',
                      XDG_DATA_HOME=str(OUT / ('profile-' + name)))
        config.update(env)
        previous = set(xdo("search", "--name", "Newtonia").stdout.split())
        self.started = time.monotonic()
        self.p = subprocess.Popen([str(ROOT / 'newtonia')] + (['+connect', code] if code else []),
                                  cwd=ROOT, env=config, stdout=self.file, stderr=subprocess.STDOUT)
        games.append(self)
        self.w = None
        def window():
            self.alive()
            # FreeGLUT windows need not expose _NET_WM_PID. Launches are
            # serial on our own Xvfb, so select only the newly created ID.
            ids = sorted(set(xdo('search', '--name', 'Newtonia').stdout.split()) - previous)
            if ids:
                self.w = ids[0]
            return self.w
        wait(window, name + ' window')
        if not code:
            self.until('Presence: In the Menu')

    def log(self):
        return self.path.read_text(errors='replace')

    def alive(self):
        if self.p.poll() is not None:
            raise AssertionError(f'{self.name} exited {self.p.returncode}')

    def until(self, pattern, seconds=30, count=1):
        def found():
            self.alive()
            return len(re.findall(pattern, self.log())) >= count
        wait(found, self.name + ': ' + pattern, seconds)

    def keys(self, *keys):
        for key in keys:
            self.alive()
            if xdo('key', '--window', self.w, key).returncode:
                raise AssertionError('Key failed: ' + key)
            time.sleep(.35)

    def shot(self, label):
        self.alive()
        xdo('windowraise', self.w)
        time.sleep(.15)
        path = OUT / (label + '.png')
        if shutil.which('xwd') and shutil.which('convert'):
            raw = path.with_suffix('.xwd')
            subprocess.run(['xwd', '-id', self.w, '-out', str(raw)], check=True, stderr=subprocess.DEVNULL)
            subprocess.run(['convert', str(raw), str(path)], check=True)
            raw.unlink()
        else:
            from PIL import ImageGrab
            ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(path)

    def stop(self):
        if self.p.poll() is None:
            self.p.send_signal(signal.SIGCONT)
            self.p.terminate()
            try:
                self.p.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        self.file.close()

    def host(self):
        self.keys('Return', 's', 'Return', 'Return')
        self.until(r'\[lobby\] room [A-Z0-9]{5}')
        return re.search(r'\[lobby\] room ([A-Z0-9]{5})', self.log())[1]

    def chooser_join(self, from_menu=False):
        if from_menu:
            self.keys('Return', 's', 'Return')
        self.keys('s', 'Return')


class TCPFixture(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, mode):
        super().__init__(('127.0.0.1', 0), Handler)
        self.mode = mode
        self.enabled = True
        self.connections = set()
        self.lock = threading.Lock()
        self.paths = []
        threading.Thread(target=self.serve_forever, daemon=True).start()

    @property
    def url(self):
        return f'ws://127.0.0.1:{self.server_address[1]}/ws'

    def cut(self):
        self.enabled = False
        with self.lock:
            for s in self.connections.copy():
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def close(self):
        self.cut()
        self.shutdown()
        self.server_close()


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        server, client = self.server, self.request
        upstream = None
        with server.lock:
            server.connections.add(client)
        try:
            if not server.enabled:
                return
            client.settimeout(3)
            header = b''
            while b'\r\n\r\n' not in header:
                chunk = client.recv(4096)
                if not chunk:
                    return
                header += chunk
                if len(header) > 65536:
                    return
            server.paths.append(header.split(b'\r\n')[0].decode())
            if server.mode == 'proxy':
                upstream = socket.create_connection((endpoint.hostname, endpoint.port or 80), timeout=3)
                upstream.sendall(header)
                client.settimeout(None)
                upstream.settimeout(None)
                while server.enabled:
                    ready, _, _ = select.select([client, upstream], [], [], .2)
                    for s in ready:
                        data = s.recv(65536)
                        if not data:
                            return
                        (upstream if s is client else client).sendall(data)
            else:
                key = re.search(br'Sec-WebSocket-Key:\s*(\S+)', header, re.I)[1]
                accept = base64.b64encode(hashlib.sha1(key + b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
                client.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                if server.mode == 'no-offer':
                    payload = b'{"t":"joined","code":"ABCDE"}'
                    client.sendall(bytes([0x81, len(payload)]) + payload)
                client.settimeout(.5)
                while server.enabled:
                    try:
                        if not client.recv(4096):
                            return
                    except socket.timeout:
                        pass
        except OSError:
            pass  # Expected when the test cuts its proxy or closes a client.
        finally:
            if upstream:
                upstream.close()
            with server.lock:
                server.connections.discard(client)


def probe(code, expected):
    # Exercise the real relay upgrade, not a copied Room implementation.
    script = '''
const ws = new WebSocket(process.argv[1]);
const timer = setTimeout(() => { console.error('probe timeout'); process.exit(1); }, 5000);
ws.onerror = () => process.exit(1);
ws.onmessage = e => { const f = JSON.parse(e.data); if (f.t === 'joined' || f.t === 'err') {
 console.log(JSON.stringify(f)); clearTimeout(timer); ws.close();
 setTimeout(() => process.exit(0), 100);
} };
'''
    result = subprocess.run(['node', '--input-type=module', '-e', script,
                             URL + '?role=join&code=' + code], capture_output=True, text=True, timeout=8, check=True)
    frame = json.loads(result.stdout)
    check('relay probe ' + expected, frame.get('reason', frame['t']) == expected)
    time.sleep(.2)  # Let the probe's socket release its capacity slot.


def invites():
    dead = Game('dead', 'ZZZZZ')
    dead.until('cold invite failed: THE ROOM IS GONE', 5)
    check('1 dead invite fails promptly without retry', 'rejoin retry in' not in dead.log())
    dead.shot('01-dead')
    dead.stop()

    host = Game('departed-host')
    code = host.host()
    host.keys('Escape')  # Leaves room, but keeps its clipboard selection alive.
    host.until('Presence: In the Menu', count=2)
    gone = Game('departed-invite', code)
    gone.until(r'cold invite failed: (THE ROOM IS GONE|THE HOST ENDED THE GAME)', 5)
    check('2 departed host invite fails promptly', 'rejoin retry in' not in gone.log())
    gone.keys('Escape')
    gone.until('Presence: In the Menu', count=2)
    clipboard = subprocess.run(['xclip', '-selection', 'clipboard', '-o'],
                               capture_output=True, text=True, check=True, timeout=3).stdout
    check('5 stale link really remains on clipboard', code in clipboard)
    gone.chooser_join(from_menu=True)
    gone.until('Presence: Joining a Co-Op Game', count=2)
    time.sleep(3)  # Several clipboard polling periods.
    check('5 spent clipboard code is not retried', '[lobby] joining room' not in gone.log())
    gone.shot('05-spent-clipboard')
    gone.keys(*code.lower())
    gone.until(r"relay err '(host-closed|no-such-room)'", 5)
    check('5 explicitly typed spent code is attempted', '[lobby] joining room ' + code in gone.log())
    gone.shot('05-explicit-retry')
    gone.stop()
    host.stop()

    for mode, expected, lower, upper in [('silent', 'THE ROOM IS NOT RESPONDING', 10, 18),
                                          ('no-offer', 'THE HOST IS NOT RESPONDING', 42, 52)]:
        fixture = TCPFixture(mode)
        try:
            game = Game(mode, 'ABCDE', NEWTONIA_SIGNAL_URL=fixture.url)
            game.until('cold invite failed: ' + expected, upper)
            elapsed = time.monotonic() - game.started
            check(f'4 {mode} timeout bounded ({elapsed:.1f}s)', lower <= elapsed <= upper)
            check(mode + ' never enters rejoin budget', 'rejoin retry in' not in game.log())
            game.shot('04-' + mode)
            game.stop()
        finally:
            fixture.close()

    host = Game('full-host')
    code = host.host()
    clients = []
    for i in range(1, 4):
        game = Game('full-client-' + str(i), code)
        clients.append(game)
        host.until('seat ' + str(i + 1) + ' filled')
        check('6 healthy cold invite has no failure', 'cold invite failed' not in game.log())
        if i == 1:
            game.shot('06-waiting-room')
    for game in clients:
        game.until('bootstrap adopted')
    host.until('net: seats free 0 reported')
    cold = Game('full-cold', code)
    cold.until('cold invite failed: THAT ROOM IS FULL', 5)
    cold.shot('03-cold-full')
    check('3 full game rejects cold fifth client', True)
    cold.stop()

    clear_clipboard()  # Force player intent rather than silent auto-probe.
    plain = Game('full-typed')
    plain.keys('Return', 's', 'Return', 's', 'Return')
    plain.keys(*code.lower())
    plain.until("relay err 'room-full'", 5)
    plain.shot('03-typed-full')
    check('3 full game rejects explicitly typed fifth client', '[lobby] joining room ' + code in plain.log())
    plain.stop()
    for game in clients:
        game.stop()
    host.stop()


def recovery():
    proxy = TCPFixture('proxy')
    joinproxy = TCPFixture('proxy')
    try:
        host = Game('recovery-host', NEWTONIA_SIGNAL_URL=proxy.url)
        code = host.host()
        clients = []
        for i in range(1, 4):
            g = Game('recovery-client-' + str(i), code,
                     NEWTONIA_SIGNAL_URL=joinproxy.url if i == 2 else URL)
            clients.append(g)
            host.until('seat ' + str(i + 1) + ' filled')
        for g in clients:
            g.until('bootstrap adopted')
        host.until('net: seats free 0 reported')

        # Break only the host relay. WebRTC continues; the worker enters
        # grace. Hold new relay connects so we can observe that window.
        start = time.monotonic()
        proxy.cut()
        time.sleep(.5)
        probe(code, 'joined')
        time.sleep(max(0, 20 - (time.monotonic() - start)))
        proxy.enabled = True
        host.until('room ' + code + ' reclaimed', 20)
        host.until('net: seats free 0 reported', count=2)
        probe(code, 'room-full')
        check('3c host reclaim restores full-room refusal after 20s relay outage', True)

        # Freeze a middle client until the watchdog parks its seat. The
        # process resumes in place and must use ?rejoin=1, not a cold join.
        victim = clients[1]
        time.sleep(2)
        victim.p.send_signal(signal.SIGSTOP)
        host.until('player 3 lost', 40)
        host.until('net: seats free 1 reported')
        probe(code, 'joined')
        check('3b other peers keep playing', 'paused awaiting rejoin' not in host.log())
        victim.p.send_signal(signal.SIGCONT)
        victim.until('auto-rejoining room', 35)
        host.until('player 3 rejoined', 45)
        victim.until('bootstrap adopted', count=2)
        check('3b automatic reconnect declares rejoin intent',
              any('rejoin=1' in p for p in joinproxy.paths))
        check('3b no cold-invite fast failure on reconnect', 'cold invite failed' not in victim.log())

        # A silent host for 20 seconds must take the patient rejoin path.
        # Stop both its main loop and ICE threads without ending the game.
        # Clients have a 10-second RX watchdog, before ICE's long timeout.
        before = [g.log().count('bootstrap adopted') for g in clients]
        host.p.send_signal(signal.SIGSTOP)
        start = time.monotonic()
        clients[0].until('auto-rejoining room', 18)
        time.sleep(1)
        clients[0].shot('07-patient-rejoin')
        check('7 host absence has no cold failure', 'cold invite failed' not in clients[0].log())
        time.sleep(max(0, 20 - (time.monotonic() - start)))
        host.p.send_signal(signal.SIGCONT)
        for g, count in zip(clients, before):
            g.until('bootstrap adopted', 60, count=count + 1)
        check('7 all clients reconnect automatically after 20s host silence', True)
    finally:
        # Resume stopped processes BEFORE shutting down fixture threads.
        for g in games:
            g.stop()
        proxy.close()
        joinproxy.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('group', choices=['invites', 'recovery', 'all'], nargs='?', default='all')
    args = parser.parse_args()
    def interrupted(signum, frame):
        raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, interrupted)
    print('Evidence: ' + str(OUT), flush=True)
    status = 'failed'
    try:
        if args.group in ('invites', 'all'):
            invites()
        if args.group in ('recovery', 'all'):
            recovery()
        status = 'passed'
        print('JOIN-LINK-SCENARIOS-OK', flush=True)
    finally:
        for game in games:
            game.stop()
        (OUT / 'results.json').write_text(json.dumps(
            {'status': status, 'group': args.group, 'checks': results}, indent=2) + '\n')
