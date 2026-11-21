#!/usr/bin/env python3
"""Writes the course's sounds (Data/Sound/fo4minigolf/<bank>_<n>.wav, mono 16-bit 44.1 kHz), all synthesised here:

    python tools/make_sounds.py <out Data folder>

  putt    the putter's face on the ball: a sharp tick with a short metallic ring
  wall    the ball off a rail or an obstacle: a hollow knock
  rim     the ball catching the cup's rim: a little plastic rattle
  cup     dropping in: a thunk and the rattle settling
  splash  into the water
  land    landing after a jump: a soft thud on the carpet
  holein  a short chime (the plugin plays it louder for a hole in one)
Several takes of each (the plugin picks one at random, with a small pitch change).
"""

import os
import sys
import wave

import numpy as np

RATE = 44100
rng = np.random.default_rng(11)


def t(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def bandpass(x, lo, hi):
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / RATE)
    X = np.fft.rfft(x)
    mask = 1 / (1 + ((lo / np.maximum(f, 1)) ** 4)) * 1 / (1 + ((f / hi) ** 4))
    return np.fft.irfft(X * mask, n)


def tone(freq, decay, length, phase=None):
    tt = t(length)
    return np.sin(2 * np.pi * freq * tt + (rng.uniform(0, 6.28) if phase is None else phase)) * np.exp(-tt / decay)


def noise(length, lo, hi, decay):
    tt = t(length)
    return bandpass(rng.normal(0, 1, len(tt)), lo, hi) * np.exp(-tt / decay)


def mix(*parts):
    """sounds laid over each other from the start (padded to the longest)"""
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def place(buf, sound, at, gain=1.0):
    i = int(at * RATE)
    n = min(len(sound), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sound[:n] * gain


def write(path, x, peak=0.85):
    x = x - np.mean(x)
    x = x / max(1e-9, np.max(np.abs(x))) * peak
    fade = min(len(x), int(0.005 * RATE))
    x[-fade:] *= np.linspace(1, 0, fade)
    data = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())


def putt(k):
    x = np.zeros(int(0.35 * RATE))
    place(x, noise(0.02, 2500, 9000, 0.002), 0.0, 1.0)
    for f, g in ((2900 + k * 90, 0.5), (4700 + k * 60, 0.3), (7300, 0.15)):
        place(x, tone(f, 0.035, 0.3), 0.0, g)
    place(x, tone(380 + k * 15, 0.02, 0.1), 0.0, 0.4)
    return x


def wall(k):
    x = np.zeros(int(0.3 * RATE))
    place(x, noise(0.03, 600, 3500, 0.006), 0.0, 1.0)
    for f, g in ((520 + k * 40, 0.6), (1180 + k * 70, 0.35), (2300, 0.15)):
        place(x, tone(f, 0.04, 0.25), 0.0, g)
    return x


def rim(k):
    x = np.zeros(int(0.35 * RATE))
    for i in range(3 + k % 2):
        at = i * (0.045 + 0.01 * rng.random())
        place(x, mix(noise(0.015, 2000, 8000, 0.003), tone(3300 + 200 * rng.random(), 0.01, 0.05)), at, 0.9 * 0.7 ** i)
    return x


def cup(k):
    x = np.zeros(int(0.8 * RATE))
    place(x, mix(tone(190 + 10 * k, 0.05, 0.3), noise(0.08, 150, 1200, 0.02) * 0.6), 0.0, 1.0)
    at = 0.06
    for i in range(6):
        at += 0.05 * 0.8 ** i + 0.01 * rng.random()
        place(x, mix(noise(0.012, 1500, 7000, 0.003), tone(2600 + 400 * rng.random(), 0.008, 0.04) * 0.6), at, 0.6 * 0.72 ** i)
    return x


def splash(k):
    x = np.zeros(int(0.9 * RATE))
    place(x, noise(0.9, 300, 5000, 0.18), 0.0, 1.0)
    place(x, noise(0.3, 120, 600, 0.06), 0.0, 0.6)
    for i in range(10):
        place(x, tone(900 + 1400 * rng.random(), 0.02, 0.06), 0.08 + 0.5 * rng.random(), 0.15)
    return x


def land(k):
    x = np.zeros(int(0.25 * RATE))
    place(x, noise(0.12, 80, 900, 0.025), 0.0, 1.0)
    place(x, tone(140 + 15 * k, 0.03, 0.15), 0.0, 0.5)
    return x


def holein(k):
    notes = [(523.25, 659.25, 783.99, 1046.5), (587.33, 739.99, 880.0, 1174.7), (523.25, 698.46, 880.0, 1046.5)][k % 3]
    x = np.zeros(int(1.2 * RATE))
    for i, f in enumerate(notes):
        s = mix(tone(f, 0.35, 0.9, 0.0), tone(f * 2, 0.18, 0.6, 0.0) * 0.3, tone(f * 3.01, 0.08, 0.3, 0.0) * 0.12)
        place(x, s, i * 0.09, 0.8)
    return x


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "dist", "Data")
    out = os.path.join(root, "Sound", "fo4minigolf")
    os.makedirs(out, exist_ok=True)
    for name, fn, takes in (("putt", putt, 4), ("wall", wall, 4), ("rim", rim, 3), ("cup", cup, 3), ("splash", splash, 2),
                            ("land", land, 3), ("holein", holein, 3)):
        for k in range(takes):
            write(os.path.join(out, f"{name}_{k + 1}.wav"), fn(k))
    print("wrote", out)


if __name__ == "__main__":
    main()
