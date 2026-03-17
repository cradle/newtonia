#!/usr/bin/env python3
"""Generate placeholder WAV sound files for Newtonia game."""
import wave
import struct
import math
import random
import os

SAMPLE_RATE = 44100

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
    """Asteroid collision: impact thud, 250ms."""
    n = int(SAMPLE_RATE * 0.25)
    rng = random.Random(99)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 12)
        noise = (rng.random() * 2 - 1) * 0.3
        thud  = math.sin(2 * math.pi * 80 * t) * 0.7
        samples.append((noise + thud) * env)
    return samples

def make_missile_explode():
    """Missile explosion: sharper burst than asteroid, 600ms."""
    n = int(SAMPLE_RATE * 0.6)
    rng = random.Random(77)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 7)
        noise = (rng.random() * 2 - 1) * 0.6
        thud  = math.sin(2 * math.pi * 80 * t) * 0.4 * math.exp(-t * 12)
        samples.append((noise + thud) * env)
    return samples

def make_missile_fly():
    """Missile in-flight: modulated whoosh, 500ms (loopable)."""
    n = int(SAMPLE_RATE * 0.5)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        freq = 400 + 200 * math.sin(2 * math.pi * 3 * t)
        samples.append(math.sin(2 * math.pi * freq * t) * 0.5)
    return samples

def make_tic():
    """Heat warning high: short 880 Hz beep, 120ms."""
    n = int(SAMPLE_RATE * 0.12)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = min(1.0, t / 0.005) * math.exp(-t * 20)
        samples.append(math.sin(2 * math.pi * 880 * t) * env * 0.9)
    return samples

def make_tic_low():
    """Heat warning low: short 440 Hz beep, 120ms."""
    n = int(SAMPLE_RATE * 0.12)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = min(1.0, t / 0.005) * math.exp(-t * 20)
        samples.append(math.sin(2 * math.pi * 440 * t) * env * 0.9)
    return samples

def make_boost():
    """Engine rumble: loopable low-frequency hum, 1s."""
    n = int(SAMPLE_RATE * 1.0)
    rng = random.Random(123)
    dur = 1.0
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        s  = math.sin(2 * math.pi * 50  * t) * 0.4
        s += math.sin(2 * math.pi * 100 * t) * 0.2
        s += math.sin(2 * math.pi * 150 * t) * 0.1
        s += (rng.random() * 2 - 1) * 0.1
        # Fade in/out for clean loop
        fade = min(1.0, t * 10) * min(1.0, (dur - t) * 10)
        samples.append(s * fade * 0.7)
    return samples

def make_title():
    """Space ambient title music: simple synth melody + bass, 8s (loopable)."""
    dur = 8.0
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n

    note_freqs = {
        'C3': 130.81, 'F3': 174.61, 'G3': 196.00, 'A3': 220.00,
        'C4': 261.63, 'D4': 293.66, 'E4': 329.63, 'F4': 349.23,
        'G4': 392.00, 'A4': 440.00, 'B4': 493.88,
        'C5': 523.25,
    }

    def add_note(freq, start, length, vol=0.25):
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            env = min(1.0, t / 0.02) * min(1.0, (length - t) / 0.08)
            if env < 0:
                env = 0.0
            samples[i] += (
                math.sin(2 * math.pi * freq * t) * env * vol +
                math.sin(2 * math.pi * freq * 2 * t) * env * vol * 0.25
            )

    melody = [
        ('C4', 0.0), ('E4', 0.5), ('G4', 1.0), ('C5', 1.5),
        ('G4', 2.0), ('E4', 2.5), ('D4', 3.0), ('F4', 3.5),
        ('C4', 4.0), ('E4', 4.5), ('A4', 5.0), ('C5', 5.5),
        ('G4', 6.0), ('F4', 6.5), ('E4', 7.0), ('C4', 7.5),
    ]
    bass = [
        ('C3', 0.0, 1.9), ('G3', 2.0, 1.9),
        ('A3', 4.0, 1.9), ('F3', 6.0, 1.9),
    ]

    for (note, start) in melody:
        add_note(note_freqs[note], start, 0.4, 0.25)
    for (note, start, length) in bass:
        add_note(note_freqs[note], start, length, 0.20)

    peak = max(abs(s) for s in samples)
    if peak > 0:
        samples = [s / peak * 0.85 for s in samples]
    return samples


if __name__ == '__main__':
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    sounds = {
        'shoot.wav':           make_shoot,
        'empty.wav':           make_empty,
        'click.wav':           make_click,
        'mine.wav':            make_mine,
        'explode.wav':         make_explode,
        'thud.wav':            make_thud,
        'missile_explode.wav': make_missile_explode,
        'missile_fly.wav':     make_missile_fly,
        'tic.wav':             make_tic,
        'tic_low.wav':         make_tic_low,
        'boost.wav':           make_boost,
        'title.wav':           make_title,
    }

    for filename, fn in sounds.items():
        print(f'Generating {filename}...')
        write_wav(filename, fn())

    print('Done! All sound files generated.')
