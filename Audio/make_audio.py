"""Synthesise seamless audio loops for the Roadster (no third-party samples).
Every loop is built in the frequency domain (integer cycles per loop), so it repeats with no click.
Run: python -I make_audio.py <out_dir>
"""
import sys, os, wave
import numpy as np

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
SR, DUR = 48000, 4.0
N = int(SR * DUR)
rng = np.random.default_rng(3)
freqs = np.fft.rfftfreq(N, 1 / SR)


def shaped_noise(shape):
    """Periodic noise with magnitude spectrum `shape(f)` and random phase."""
    mag = shape(freqs)
    spec = mag * np.exp(1j * rng.uniform(0, 2 * np.pi, len(freqs)))
    spec[0] = 0
    x = np.fft.irfft(spec, N)
    return x / (np.abs(x).max() + 1e-9)


def tone(f, amp=1.0, phase=None):
    f = round(f * DUR) / DUR          # integer cycles per loop
    p = rng.uniform(0, 2 * np.pi) if phase is None else phase
    t = np.arange(N) / SR
    return amp * np.sin(2 * np.pi * f * t + p)


def band(f0, bw):
    return lambda f: np.exp(-0.5 * ((f - f0) / bw) ** 2)


def save(name, x, gain=0.8):
    x = x / (np.abs(x).max() + 1e-9) * gain
    pcm = (x * 32767).astype(np.int16)
    with wave.open(os.path.join(OUT, name + ".wav"), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(pcm.tobytes())


t = np.arange(N) / SR
# Motor: inverter carrier whine + motor electrical orders + gear mesh, slow wobble (2 cycles per loop)
f0 = 180.0
motor = sum(tone(f0 * k, a) for k, a in ((1, 1.0), (2, 0.45), (3, 0.35), (4.5, 0.2), (6, 0.15), (8, 0.08)))
motor += tone(f0 * 11.3, 0.12)                                   # gear mesh
motor += 0.25 * shaped_noise(band(f0 * 6, f0 * 2))
motor *= 1 + 0.08 * np.sin(2 * np.pi * t * 2 / DUR * 4)
save("S_Motor", motor, 0.7)

# Fan: broadband suction roar + 7-blade pass tone and harmonics
fan = shaped_noise(lambda f: 1 / (1 + (f / 900) ** 2) * (f > 40))
bp = 7 * 160.0
fan += sum(tone(bp * k, 0.35 / k) for k in (1, 2, 3))
save("S_Fan", fan, 0.75)

# Thrust: high-pressure cold-gas jet - bright hiss with a low roar underneath and turbulent flutter
hiss = shaped_noise(lambda f: (f / 3000) ** 0.5 / (1 + (f / 9000) ** 4))
roar = shaped_noise(lambda f: 1 / (1 + (f / 180) ** 2) * (f > 25))
thrust = 0.8 * hiss + 0.9 * roar
thrust *= 1 + 0.15 * shaped_noise(lambda f: (f < 30) * 1.0)
save("S_Thrust", thrust, 0.85)

# Wind: low-passed noise with gusts
wind = shaped_noise(lambda f: 1 / (1 + (f / 450) ** 2) * (f > 20))
wind *= 1 + 0.3 * shaped_noise(lambda f: (f < 4) * 1.0)
save("S_Wind", wind, 0.7)

# Tire squeal: narrow-band noise around 1.1 kHz plus a wavering tonal component
sq = shaped_noise(band(1100, 120)) + 0.6 * shaped_noise(band(2250, 160))
sq += 0.4 * tone(1150) * (1 + 0.5 * shaped_noise(lambda f: (f < 12) * 1.0))
save("S_Tire", sq, 0.6)
print("audio ->", OUT)
