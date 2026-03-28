#!/usr/bin/env python3
"""Generate placeholder WAV sound files for Newtonia game."""
import wave
import struct
import math
import random
import os

SAMPLE_RATE = 48000

def write_wav(filename, samples):
    with wave.open(filename, 'w') as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SAMPLE_RATE)
        clamped = [max(-32767, min(32767, int(s * 32767))) for s in samples]
        data = struct.pack(f'<{len(clamped)}h', *clamped)
        f.writeframes(data)

def make_shoot():
    """Laser pew: frequency sweep 800→200 Hz, 150ms."""
    n = int(SAMPLE_RATE * 0.15)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        freq = 800 - 600 * (i / n)
        env = math.exp(-t * 20)
        samples.append(math.sin(2 * math.pi * freq * t) * env * 0.8)
    return samples

def make_empty():
    """Empty ammo click: short noise burst + tone, 80ms."""
    n = int(SAMPLE_RATE * 0.08)
    rng = random.Random(11)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 60)
        noise = (rng.random() * 2 - 1) * 0.4 * env
        tone = math.sin(2 * math.pi * 400 * t) * 0.5 * env
        samples.append(noise + tone)
    return samples

def make_click():
    """UI click: short high tone, 60ms."""
    n = int(SAMPLE_RATE * 0.06)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 80)
        samples.append(math.sin(2 * math.pi * 1000 * t) * 0.7 * env)
    return samples

def make_mine():
    """Mine deploy: low metallic clank, 300ms."""
    n = int(SAMPLE_RATE * 0.3)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 8)
        s  = math.sin(2 * math.pi * 150 * t) * 0.6 * env
        s += math.sin(2 * math.pi * 450 * t) * 0.3 * env
        s += math.sin(2 * math.pi * 900 * t) * 0.1 * env * math.exp(-t * 30)
        samples.append(s)
    return samples

def make_explode():
    """Asteroid explosion: noise + low thud, 800ms."""
    n = int(SAMPLE_RATE * 0.8)
    rng = random.Random(42)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 5)
        noise = (rng.random() * 2 - 1) * 0.5
        thud  = math.sin(2 * math.pi * 60  * t) * math.exp(-t * 10)
        crunch = math.sin(2 * math.pi * 200 * t) * math.exp(-t * 15)
        samples.append((noise * 0.5 + thud * 0.3 + crunch * 0.2) * env)
    return samples

def make_thud():
    """Bullet hits invincible asteroid: low woody knock, 600ms."""
    n = int(SAMPLE_RATE * 0.60)
    rng = random.Random(99)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Slow body decay for a dense, woody resonance
        body_env = math.exp(-t * 5)
        # Low fundamental with inharmonic partials (like a log/block of wood)
        body  = math.sin(2 * math.pi * 55  * t) * 0.95 * body_env
        body += math.sin(2 * math.pi * 110 * t) * 0.45 * body_env
        body += math.sin(2 * math.pi * 175 * t) * 0.18 * body_env
        # Very brief noise transient at the attack (muffled knock character)
        click_env = math.exp(-t * 150)
        noise = (rng.random() * 2 - 1) * 0.45 * click_env
        samples.append(body + noise)
    return samples

def make_mine_explode():
    """Mine explosion: mid-weight boom with metallic clang and noise burst, 800ms."""
    n = int(SAMPLE_RATE * 0.8)
    rng = random.Random(53)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        attack = min(1.0, t / 0.003)
        # Low boom
        boom   = math.sin(2 * math.pi * 70  * t) * math.exp(-t * 6.0) * 0.75
        # Metallic clang (high harmonics decay quickly)
        clang  = math.sin(2 * math.pi * 420 * t) * math.exp(-t * 25.0) * 0.35
        clang += math.sin(2 * math.pi * 780 * t) * math.exp(-t * 35.0) * 0.15
        # Noise burst
        noise  = (rng.random() * 2 - 1) * math.exp(-t * 14.0) * 0.65
        # Trailing rumble
        rumble = (rng.random() * 2 - 1) * math.exp(-t * 4.0) * 0.15
        samples.append((boom + clang + noise + rumble) * attack)
    return samples


def make_missile_explode():
    """Missile impact explosion: sharp attack, deep boom, noise burst, trailing rumble, 1s."""
    n = int(SAMPLE_RATE * 1.0)
    rng = random.Random(77)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Very sharp attack transient (2ms rise)
        attack = min(1.0, t / 0.002)
        # Low boom
        boom   = math.sin(2 * math.pi * 55  * t) * math.exp(-t * 5.0) * 0.85
        # Mid thud (decays faster)
        thud   = math.sin(2 * math.pi * 120 * t) * math.exp(-t * 10.0) * 0.5
        # Heavy noise burst at front
        noise  = (rng.random() * 2 - 1) * math.exp(-t * 18.0) * 0.75
        # Trailing rumble
        rumble = (rng.random() * 2 - 1) * math.exp(-t * 3.5) * 0.18
        samples.append((boom + thud + noise + rumble) * attack)
    return samples

def make_missile_fly():
    """Missile in-flight: wailing screamer with air rush, 500ms (loopable)."""
    n = int(SAMPLE_RATE * 0.5)
    samples = []
    phase = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        # 350 Hz tone, amplitude-modulated at 8 Hz for a wailing/screaming quality
        # Both 8 Hz and 350 Hz divide evenly into 0.5s -> seamless loop
        am = 0.55 + 0.45 * math.sin(2 * math.pi * 8 * t)
        phase += 2 * math.pi * 350 / SAMPLE_RATE
        tone  = math.sin(phase) * 0.28 * am
        tone += math.sin(phase * 2) * 0.10 * am
        samples.append(tone)
    return samples

def make_tic():
    """Heat warning high: short 880 Hz beep, 250ms."""
    n = int(SAMPLE_RATE * 0.25)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = min(1.0, t / 0.005) * math.exp(-t * 20)
        samples.append(math.sin(2 * math.pi * 880 * t) * env * 0.9)
    return samples

def make_tic_low():
    """Heat warning low: short 440 Hz beep, 250ms."""
    n = int(SAMPLE_RATE * 0.25)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = min(1.0, t / 0.005) * math.exp(-t * 20)
        samples.append(math.sin(2 * math.pi * 440 * t) * env * 0.9)
    return samples

def make_shield_hum():
    """Shield active: pulsing high-energy electromagnetic hum, 1s (loopable)."""
    n = int(SAMPLE_RATE * 1.0)
    samples = []
    phase1 = 0.0
    phase2 = 0.0
    phase3 = 0.0
    phase4 = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        # 175/350/525/700 Hz harmonics – all divide 44100 evenly -> seamless loop, no click
        # Higher fundamental (175 vs 105) gives a more energetic "active barrier" quality
        phase1 += 2 * math.pi * 175 / SAMPLE_RATE
        phase2 += 2 * math.pi * 350 / SAMPLE_RATE
        phase3 += 2 * math.pi * 525 / SAMPLE_RATE
        phase4 += 2 * math.pi * 700 / SAMPLE_RATE
        # 2 Hz amplitude pulse (2 full cycles per 1 s loop) -> energetic shield throb
        pulse = 0.75 + 0.25 * math.sin(2 * math.pi * 2 * t)
        s  = math.sin(phase1) * 0.35 * pulse
        s += math.sin(phase2) * 0.18
        s += math.sin(phase3) * 0.08
        s += math.sin(phase4) * 0.03
        samples.append(s * 0.80)
    return samples

def make_boost():
    """Engine rumble: loopable low-frequency hum, 1s."""
    n = int(SAMPLE_RATE * 1.0)
    rng = random.Random(123)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        s  = math.sin(2 * math.pi * 50  * t) * 0.4
        s += math.sin(2 * math.pi * 100 * t) * 0.2
        s += math.sin(2 * math.pi * 150 * t) * 0.1
        s += (rng.random() * 2 - 1) * 0.1
        samples.append(s * 0.7)
    return samples

def make_giga_mine_explode():
    """Giga-mine explosion: massive deep boom with rumble, 2s."""
    n = int(SAMPLE_RATE * 2.0)
    rng = random.Random(7)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Sharp transient at the front
        attack = min(1.0, t / 0.003)
        env = attack * math.exp(-t * 2.5)
        # Sub bass (30 Hz) – felt more than heard
        sub   = math.sin(2 * math.pi * 30  * t) * math.exp(-t * 3.0) * 0.9
        # Low boom (60 Hz)
        boom  = math.sin(2 * math.pi * 60  * t) * math.exp(-t * 4.0) * 0.7
        # Mid thud (120 Hz), decays faster
        thud  = math.sin(2 * math.pi * 120 * t) * math.exp(-t * 8.0) * 0.4
        # Noise burst (heavy at start, fades fast)
        noise = (rng.random() * 2 - 1) * math.exp(-t * 12.0) * 0.6
        # Trailing rumble noise
        rumble = (rng.random() * 2 - 1) * math.exp(-t * 1.5) * 0.15
        samples.append((sub + boom + thud + noise + rumble) * env)
    return samples


def make_ting():
    """Bullet hits reflective asteroid: sharp high metallic ping with inharmonic partials, 400ms."""
    n = int(SAMPLE_RATE * 0.4)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        envelope = math.exp(-t * 12.0)
        s  = 0.6 * math.sin(2 * math.pi * 2800 * t) * math.exp(-t * 10.0)
        s += 0.3 * math.sin(2 * math.pi * 4600 * t) * math.exp(-t * 14.0)
        s += 0.1 * math.sin(2 * math.pi * 7200 * t) * math.exp(-t * 18.0)
        samples.append(s * envelope * 0.85)
    return samples

def make_asteroid_ting():
    """Asteroid hits reflective asteroid: deeper resonant metallic clang, 600ms."""
    n = int(SAMPLE_RATE * 0.6)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        envelope = math.exp(-t * 7.0)
        s  = 0.55 * math.sin(2 * math.pi * 650  * t) * math.exp(-t * 5.0)
        s += 0.30 * math.sin(2 * math.pi * 1060 * t) * math.exp(-t * 8.0)
        s += 0.15 * math.sin(2 * math.pi * 1750 * t) * math.exp(-t * 12.0)
        samples.append(s * envelope * 0.85)
    return samples

def make_pickup():
    """Item pickup: cheerful rising chime, 200ms."""
    n = int(SAMPLE_RATE * 0.2)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        freq = 600 + 800 * (i / n)
        env = math.exp(-t * 10)
        s  = math.sin(2 * math.pi * freq * t) * 0.6 * env
        s += math.sin(2 * math.pi * freq * 2 * t) * 0.2 * env
        samples.append(s)
    return samples


def make_title():
    """Space title music: A-minor synth melody with vibrato, pad chords, and bass, 8s (loopable)."""
    dur = 8.0
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n

    note_freqs = {
        'D3': 146.83, 'E3': 164.81, 'F3': 174.61, 'G3': 196.00,
        'A3': 220.00, 'B3': 246.94,
        'C4': 261.63, 'D4': 293.66, 'E4': 329.63, 'F4': 349.23,
        'G4': 392.00, 'A4': 440.00, 'B4': 493.88,
        'C5': 523.25,
    }

    def add_note(freq, start, length, vol=0.22, vibrato=True):
        """Lead synth: 3 harmonics + optional vibrato for a warm, expressive tone."""
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        phase = 0.0
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            attack  = min(1.0, t / 0.04)
            release = min(1.0, (length - t) / 0.10) if length > 0.10 else 1.0
            env = attack * max(0.0, release)
            vib = (1.0 + 0.012 * math.sin(2 * math.pi * 5.5 * t)) if vibrato else 1.0
            phase += 2 * math.pi * freq * vib / SAMPLE_RATE
            s  = math.sin(phase)         * 0.55 * env
            s += math.sin(phase * 2)     * 0.25 * env
            s += math.sin(phase * 3)     * 0.10 * env
            samples[i] += s * vol

    def add_pad(freq, start, length, vol=0.09):
        """Soft pad: slow attack/release, two harmonics for an airy chord texture."""
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            attack  = min(1.0, t / 0.18)
            release = min(1.0, (length - t) / 0.28) if length > 0.28 else 1.0
            env = attack * max(0.0, release)
            s  = math.sin(2 * math.pi * freq * t)     * 0.60 * env
            s += math.sin(2 * math.pi * freq * 2 * t) * 0.20 * env
            samples[i] += s * vol

    # Melody in A minor (A B C D E F G), moderately paced with longer note durations
    melody = [
        ('A4', 0.00, 0.45), ('C5', 0.50, 0.45),
        ('B4', 1.00, 0.90),
        ('G4', 2.00, 0.65), ('E4', 2.75, 0.45),
        ('F4', 3.25, 0.95),
        ('E4', 4.25, 0.45), ('D4', 4.75, 0.45),
        ('C4', 5.25, 0.45), ('D4', 5.75, 0.20),
        ('E4', 6.00, 0.45),
        ('A4', 6.50, 0.90),
        ('G4', 7.50, 0.45),
    ]

    # Sustained bass line – one note per 2-bar phrase
    bass = [
        ('A3', 0.0, 1.85),
        ('F3', 2.0, 1.85),
        ('D3', 4.0, 1.85),
        ('E3', 6.0, 1.85),
    ]

    # Pad chords: Am | Fmaj | Dm | Em (one per phrase)
    pad_notes = [
        ('A3', 0.0, 2.0), ('C4', 0.0, 2.0), ('E4', 0.0, 2.0),   # Am
        ('F3', 2.0, 2.0), ('A3', 2.0, 2.0), ('C4', 2.0, 2.0),   # Fmaj
        ('D3', 4.0, 2.0), ('F3', 4.0, 2.0), ('A3', 4.0, 2.0),   # Dm
        ('E3', 6.0, 2.0), ('G3', 6.0, 2.0), ('B3', 6.0, 2.0),   # Em
    ]

    for note, start, length in melody:
        add_note(note_freqs[note], start, length, 0.25)
    for note, start, length in bass:
        add_note(note_freqs[note], start, length, 0.22, vibrato=False)
    for note, start, length in pad_notes:
        add_pad(note_freqs[note], start, length, 0.09)

    peak = max(abs(s) for s in samples)
    if peak > 0:
        samples = [s / peak * 0.85 for s in samples]
    return samples


if __name__ == '__main__':
    repo_root = os.path.dirname(os.path.abspath(__file__))
    os.makedirs(os.path.join(repo_root, 'audio'), exist_ok=True)
    os.chdir(os.path.join(repo_root, 'audio'))

    sounds = {
        'shoot.wav':           make_shoot,
        'empty.wav':           make_empty,
        'click.wav':           make_click,
        'mine.wav':            make_mine,
        'explode.wav':         make_explode,
        'thud.wav':            make_thud,
        'mine_explode.wav':     make_mine_explode,
        'missile_explode.wav': make_missile_explode,
        'missile_fly.wav':     make_missile_fly,
        'tic.wav':             make_tic,
        'tic_low.wav':         make_tic_low,
        'shield_hum.wav':      make_shield_hum,
        'boost.wav':           make_boost,
        'title.wav':           make_title,
        'pickup.wav':          make_pickup,
        'giga_mine_explode.wav': make_giga_mine_explode,
        'ting.wav':              make_ting,
        'asteroid_ting.wav':     make_asteroid_ting,
    }

    for filename, fn in sounds.items():
        print(f'Generating {filename}...')
        write_wav(filename, fn())

    print('Done! All sound files generated.')
