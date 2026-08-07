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

def make_beam():
    """Piercing lance: bright electric zap, downward sweep 1600->500 Hz with a
    buzzing harmonic, 180ms. Sharper and higher than the default pew."""
    n = int(SAMPLE_RATE * 0.18)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        p = i / n
        freq = 1600 - 1100 * p
        env = math.exp(-t * 16) * (1.0 - p * 0.3)
        base = math.sin(2 * math.pi * freq * t)
        # Square-ish harmonic buzz an octave up for an energetic, cutting edge.
        buzz = 0.3 * math.sin(2 * math.pi * freq * 2 * t)
        buzz += 0.15 * (1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0)
        samples.append((base + buzz) * env * 0.7)
    return samples

def make_lance():
    """Full-length pulse: heavy instantaneous zap — a deep 220 Hz body under a
    fast 2400->300 Hz crack, with a noise transient at the front, 300ms."""
    n = int(SAMPLE_RATE * 0.3)
    rng = random.Random(42)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        p = i / n
        env = math.exp(-t * 14)
        crack_freq = 2400 - 2100 * min(1.0, p * 3.0)
        crack = math.sin(2 * math.pi * crack_freq * t) * env * 0.5
        body = math.sin(2 * math.pi * 220 * t) * math.exp(-t * 8) * 0.45
        transient = (rng.random() * 2 - 1) * math.exp(-t * 90) * 0.5
        samples.append(crack + body + transient)
    return samples

def make_shock():
    """Electric zap: buzzy detuned high tones sweeping down under crackling
    amplitude-modulated noise, 200ms."""
    n = int(SAMPLE_RATE * 0.2)
    rng = random.Random(73)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 14)
        f1 = 1600 - 900 * (i / n)
        f2 = 2350 - 1400 * (i / n)
        tone = (math.sin(2 * math.pi * f1 * t) + 0.6 * math.sin(2 * math.pi * f2 * t)) * 0.4
        crackle = (rng.random() * 2 - 1) * (0.5 + 0.5 * math.sin(2 * math.pi * 90 * t)) * 0.5
        samples.append((tone + crackle) * env * 0.8)
    peak = max(abs(s) for s in samples) or 1.0
    return [s / peak * 0.85 for s in samples]

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

def make_station_explode():
    """Mini-station destruction: deep boom with tearing metal and debris, 1.4s."""
    n = int(SAMPLE_RATE * 1.4)
    rng = random.Random(2024)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Sharp attack then a long-ish decay for a weighty blast.
        attack = min(1.0, t / 0.004)
        env = attack * math.exp(-t * 3.2)
        # Sub/low boom for the body of the explosion.
        sub  = math.sin(2 * math.pi * 40  * t) * math.exp(-t * 3.5) * 0.8
        boom = math.sin(2 * math.pi * 80  * t) * math.exp(-t * 5.0) * 0.5
        # Inharmonic metallic partials — the ring structure tearing apart.
        metal  = math.sin(2 * math.pi * 430  * t) * math.exp(-t * 6.0) * 0.22
        metal += math.sin(2 * math.pi * 723  * t) * math.exp(-t * 7.5) * 0.16
        metal += math.sin(2 * math.pi * 1310 * t) * math.exp(-t * 9.0) * 0.10
        # Initial noise burst (the blast front).
        noise = (rng.random() * 2 - 1) * math.exp(-t * 9.0) * 0.5
        # Trailing debris/rumble that lingers after the boom.
        debris = (rng.random() * 2 - 1) * math.exp(-t * 2.0) * 0.14
        samples.append((sub + boom + metal + noise + debris) * env)
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

def make_warp():
    """Teleporting asteroid warp: sci-fi shimmer with pitch sweep and spatial whoosh, 700ms."""
    n = int(SAMPLE_RATE * 0.7)
    rng = random.Random(61)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        # Rising frequency sweep from 300 Hz to 1800 Hz for an upward teleport feeling
        sweep = 300 + 1500 * (i / n) ** 0.5
        # Phase modulation on second harmonic for a shimmering, unstable quality
        mod = 0.4 * math.sin(2 * math.pi * 18 * t)
        tone  = math.sin(2 * math.pi * sweep * t + mod) * 0.45
        tone += math.sin(2 * math.pi * sweep * 2 * t) * 0.20
        # Noise whoosh that peaks in the middle then fades
        whoosh_env = math.exp(-((t - 0.25) ** 2) / 0.02)
        noise = (rng.random() * 2 - 1) * 0.30 * whoosh_env
        # Overall envelope: fast attack, smooth decay
        env = min(1.0, t / 0.015) * math.exp(-t * 4.0)
        samples.append((tone + noise) * env * 0.85)
    return samples

def make_time_slow_start():
    """Time-slow engage: a tape-style pitch dive, 1400->170 Hz over 0.9s with
    a sub-octave under it and a shimmer that drags as it falls — the world's
    clock winding down. Phase-accumulated so the glide stays smooth."""
    dur = 0.9
    n = int(SAMPLE_RATE * dur)
    samples = []
    phase = 0.0
    phase_sub = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        p = i / n
        freq = 1400.0 * (170.0 / 1400.0) ** p
        phase += 2 * math.pi * freq / SAMPLE_RATE
        phase_sub += 2 * math.pi * (freq * 0.5) / SAMPLE_RATE
        env = min(1.0, t / 0.02) * (1.0 - p) ** 0.35
        s = math.sin(phase) * 0.55 + math.sin(phase_sub) * 0.25
        # Shimmer that slows with the sweep — 6 Hz down to 2 Hz.
        s *= 1.0 + 0.15 * math.sin(2 * math.pi * (6.0 - 4.0 * p) * t)
        samples.append(s * env * 0.8)
    return samples

def make_time_slow_end():
    """Time-slow release: the reverse sweep, 170->1400 Hz over 0.6s — the
    clock spinning back up to full speed."""
    dur = 0.6
    n = int(SAMPLE_RATE * dur)
    samples = []
    phase = 0.0
    phase_sub = 0.0
    for i in range(n):
        t = i / SAMPLE_RATE
        p = i / n
        freq = 170.0 * (1400.0 / 170.0) ** p
        phase += 2 * math.pi * freq / SAMPLE_RATE
        phase_sub += 2 * math.pi * (freq * 0.5) / SAMPLE_RATE
        env = min(1.0, t / 0.02) * min(1.0, (dur - t) / 0.08)
        s = math.sin(phase) * 0.55 + math.sin(phase_sub) * 0.25
        s *= 1.0 + 0.15 * math.sin(2 * math.pi * (2.0 + 4.0 * p) * t)
        samples.append(s * env * 0.8)
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


def make_god_mode_music():
    """God mode: driving E-minor arpeggio + bass pulses + power lead, 4s (loopable)."""
    dur = 4.0
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n

    # All frequencies are integers → phase completes exact cycles in 4s (48000*4 samples)
    def add_synth(freq, start, length, vol=0.20):
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        phase = 0.0
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            attack  = min(1.0, t / 0.008)
            release = min(1.0, (length - t) / 0.015) if length > 0.015 else 1.0
            env = attack * max(0.0, release)
            phase += 2 * math.pi * freq / SAMPLE_RATE
            s  = math.sin(phase) * 0.60 * env
            s += math.sin(phase * 2) * 0.25 * env
            s += math.sin(phase * 3) * 0.08 * env
            samples[i] += s * vol

    def add_bass(freq, start, vol=0.38):
        length = 0.32
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        phase = 0.0
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            env = min(1.0, t / 0.004) * math.exp(-t * 7.0)
            phase += 2 * math.pi * freq / SAMPLE_RATE
            s  = math.sin(phase) * 0.70 * env
            s += math.sin(phase * 2) * 0.22 * env
            samples[i] += s * vol

    # E minor pentatonic arpeggios: E4=330, G4=392, B4=494, E5=660, B4, G4
    arp = [330, 392, 494, 660, 494, 392]
    # 32 notes over 4s → 0.125s each (160 BPM 16th notes)
    step = 4.0 / 32
    for i in range(32):
        add_synth(arp[i % len(arp)], i * step, step * 0.88, 0.17)

    # Bass pulses: E2=82 Hz / B1=62 Hz every 0.25s (16th note bass hits)
    bass_pattern = [82, 82, 62, 82,  82, 82, 62, 82,
                    82, 82, 62, 82,  82, 82, 62, 82]
    for i, f in enumerate(bass_pattern):
        add_bass(f, i * 0.25, 0.40)

    # Power lead hitting key notes of the phrase (E4, G4, B4, E5)
    lead = [
        (660, 0.000, 0.110), (784, 0.125, 0.110), (660, 0.250, 0.110),
        (784, 0.500, 0.110), (988, 0.625, 0.110), (784, 0.750, 0.230),
        (660, 1.000, 0.110), (784, 1.125, 0.110), (660, 1.250, 0.110),
        (988, 1.500, 0.230), (660, 1.750, 0.110),
        (660, 2.000, 0.110), (784, 2.125, 0.110), (660, 2.250, 0.110),
        (784, 2.500, 0.110), (988, 2.625, 0.110), (784, 2.750, 0.230),
        (660, 3.000, 0.110), (784, 3.125, 0.110), (988, 3.250, 0.110),
        (784, 3.500, 0.110), (660, 3.625, 0.110), (494, 3.750, 0.230),
    ]
    for f, start, length in lead:
        add_synth(f, start, length, 0.21)

    peak = max(abs(s) for s in samples)
    if peak > 0:
        samples = [s / peak * 0.85 for s in samples]
    return samples


def make_god_mode_music_warn():
    """God mode warning: frantic octave-up version with tremolo, 2s (loopable) for last 3s."""
    dur = 2.0
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n

    def add_synth(freq, start, length, vol=0.20):
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        phase = 0.0
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            abs_t = start + t
            # 16 Hz tremolo (16*2 = 32 complete cycles in 2s → seamless loop)
            trem = 0.55 + 0.45 * math.sin(2 * math.pi * 16 * abs_t)
            attack  = min(1.0, t / 0.005)
            release = min(1.0, (length - t) / 0.008) if length > 0.008 else 1.0
            env = attack * max(0.0, release) * trem
            phase += 2 * math.pi * freq / SAMPLE_RATE
            s  = math.sin(phase) * 0.60 * env
            s += math.sin(phase * 2) * 0.25 * env
            samples[i] += s * vol

    def add_bass(freq, start, vol=0.38):
        length = 0.16
        s0 = int(start * SAMPLE_RATE)
        s1 = min(n, int((start + length) * SAMPLE_RATE))
        phase = 0.0
        for i in range(s0, s1):
            t = (i - s0) / SAMPLE_RATE
            abs_t = start + t
            # 12 Hz tremolo (12*2 = 24 complete cycles → seamless)
            trem = 0.50 + 0.50 * math.sin(2 * math.pi * 12 * abs_t)
            env = min(1.0, t / 0.003) * math.exp(-t * 12.0) * trem
            phase += 2 * math.pi * freq / SAMPLE_RATE
            s  = math.sin(phase) * 0.70 * env
            s += math.sin(phase * 2) * 0.22 * env
            samples[i] += s * vol

    # One octave up: E5=660, G5=784, B5=988, E6=1320, B5, G5
    arp = [660, 784, 988, 1320, 988, 784]
    # 32 notes over 2s → 0.0625s each (double speed vs main)
    step = 2.0 / 32
    for i in range(32):
        add_synth(arp[i % len(arp)], i * step, step * 0.85, 0.18)

    # Faster bass: E3=164, B2=124 every 0.125s
    bass_pattern = [164, 164, 124, 164,  164, 164, 124, 164,
                    164, 164, 124, 164,  164, 164, 124, 164]
    for i, f in enumerate(bass_pattern):
        add_bass(f, i * 0.125, 0.36)

    peak = max(abs(s) for s in samples)
    if peak > 0:
        samples = [s / peak * 0.85 for s in samples]
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


def make_intro():
    """New-threat intro music: same palette as the title theme (A-minor lead
    with vibrato over pad chords and bass) but shorter and more expectant —
    a rising herald phrase that hangs on a question, 4s (loopable)."""
    dur = 4.0
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n

    note_freqs = {
        'E3': 164.81, 'G3': 196.00, 'A3': 220.00, 'B3': 246.94,
        'C4': 261.63, 'E4': 329.63, 'G4': 392.00,
        'A4': 440.00, 'B4': 493.88, 'C5': 523.25,
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

    # Rising, expectant melody: climbs from A4 to C5 then falls away unresolved,
    # so the loop keeps asking the same question until the player fires.
    melody = [
        ('A4', 0.00, 0.40), ('E4', 0.50, 0.40),
        ('A4', 1.00, 0.40), ('B4', 1.50, 0.40),
        ('C5', 2.00, 0.70), ('B4', 2.75, 0.40),
        ('G4', 3.25, 0.28), ('E4', 3.58, 0.30),
    ]

    # One sustained bass note per 2s phrase
    bass = [
        ('A3', 0.0, 1.85),
        ('E3', 2.0, 1.85),
    ]

    # Pad chords: Am | Em (one per phrase)
    pad_notes = [
        ('A3', 0.0, 2.0), ('C4', 0.0, 2.0), ('E4', 0.0, 2.0),   # Am
        ('E3', 2.0, 2.0), ('G3', 2.0, 2.0), ('B3', 2.0, 2.0),   # Em
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


def make_pause():
    """Pause screen music: longer, mellower companion to the title theme —
    A-minor lead with vibrato over pad chords and bass, plus a soft
    music-box arpeggio to carry the longer form, 16s (loopable)."""
    dur = 16.0
    n = int(SAMPLE_RATE * dur)
    samples = [0.0] * n

    note_freqs = {
        'C3': 130.81, 'D3': 146.83, 'E3': 164.81, 'F3': 174.61,
        'G3': 196.00, 'A3': 220.00, 'B3': 246.94,
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

    # One chord per 2s bar: Am F C/G G | Am F Dm Em — wanders further than
    # the title's progression, then Em leads back to Am for the loop.
    chords = [
        ('A3', 'C4', 'E4'),   # Am
        ('F3', 'A3', 'C4'),   # F
        ('G3', 'C4', 'E4'),   # C (second inversion)
        ('G3', 'B3', 'D4'),   # G
        ('A3', 'C4', 'E4'),   # Am
        ('F3', 'A3', 'C4'),   # F
        ('D3', 'F3', 'A3'),   # Dm
        ('E3', 'G3', 'B3'),   # Em
    ]

    # Sustained bass root per bar
    bass = ['A3', 'F3', 'C3', 'G3', 'A3', 'F3', 'D3', 'E3']

    # Relaxed melody: one gentle phrase per bar, ending on B4 so the loop
    # resolves back onto the opening E4.
    melody = [
        ('E4',  0.00, 0.90), ('A4',  1.00, 0.45), ('B4',  1.50, 0.45),
        ('C5',  2.00, 0.90), ('A4',  3.00, 0.45), ('G4',  3.50, 0.45),
        ('G4',  4.00, 0.90), ('E4',  5.00, 0.45), ('F4',  5.50, 0.45),
        ('D4',  6.00, 0.90), ('G4',  7.00, 0.90),
        ('A4',  8.00, 0.90), ('C5',  9.00, 0.45), ('B4',  9.50, 0.45),
        ('A4', 10.00, 0.90), ('F4', 11.00, 0.45), ('G4', 11.50, 0.45),
        ('F4', 12.00, 0.90), ('E4', 13.00, 0.45), ('D4', 13.50, 0.45),
        ('E4', 14.00, 0.90), ('G4', 15.00, 0.45), ('B4', 15.50, 0.40),
    ]

    for note, start, length in melody:
        add_note(note_freqs[note], start, length, 0.24)
    for bar, note in enumerate(bass):
        add_note(note_freqs[note], bar * 2.0, 1.85, 0.20, vibrato=False)
    for bar, chord in enumerate(chords):
        for note in chord:
            add_pad(note_freqs[note], bar * 2.0, 2.0, 0.08)
    # Music-box arpeggio: four soft plucks per bar, an octave above the pad
    # chord, keeping the long form moving without breaking the calm.
    for bar, chord in enumerate(chords):
        order = (0, 1, 2, 1)
        for k in range(4):
            freq = note_freqs[chord[order[k]]] * 2
            add_note(freq, bar * 2.0 + k * 0.5, 0.28, 0.06, vibrato=False)

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
        'beam.wav':            make_beam,
        'lance.wav':           make_lance,
        'shock.wav':           make_shock,
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
        'intro.wav':           make_intro,
        'pause.wav':           make_pause,
        'god_mode_music.wav':       make_god_mode_music,
        'god_mode_music_warn.wav':  make_god_mode_music_warn,
        'pickup.wav':          make_pickup,
        'giga_mine_explode.wav': make_giga_mine_explode,
        'station_explode.wav':   make_station_explode,
        'ting.wav':              make_ting,
        'asteroid_ting.wav':     make_asteroid_ting,
        'warp.wav':              make_warp,
        'time_slow_start.wav':   make_time_slow_start,
        'time_slow_end.wav':     make_time_slow_end,
    }

    for filename, fn in sounds.items():
        print(f'Generating {filename}...')
        write_wav(filename, fn())

    print('Done! All sound files generated.')
