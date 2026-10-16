"""Ambient loops for the soundwalk (menu 1.5, S5: virtual sound sources at the monument): crowd murmur, birds, wind.

Synthetic placeholders, mono 48 kHz 16-bit PCM, 12 s each, seamless loops (the noise generators are circular and the
event patterns repeat with the loop). Plain Python (no numpy), like make_anechoic_clip.py.

    python Tools/Audio/make_ambient_clips.py [out_dir]     # default: Saved/Audio -> A_Ambient_Crowd/Birds/Wind.wav
"""
import math
import os
import struct
import sys
import wave

SAMPLE_RATE = 48000
SECONDS = 12.0
N = int(SAMPLE_RATE * SECONDS)
PEAK = 0.5


class Lcg:
    def __init__(self, seed):
        self.state = seed

    def uniform(self):
        self.state = (self.state * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
        return (self.state >> 11) / float(1 << 53)

    def signed(self):
        return 2.0 * self.uniform() - 1.0


def circular_lowpass(samples, cutoff_hz, passes=2):
    """One-pole low-pass run twice around the loop so that the end flows into the start."""
    a = math.exp(-2.0 * math.pi * cutoff_hz / SAMPLE_RATE)
    out = list(samples)
    for _ in range(passes):
        y = 0.0
        for _round in range(2):  # the second round starts from the state at the loop end
            for i, x in enumerate(out):
                y = (1.0 - a) * x + a * y
                if _round == 1:
                    out[i] = y
    return out


def normalize(samples, peak=PEAK):
    top = max(abs(v) for v in samples) or 1.0
    return [peak * v / top for v in samples]


def wind():
    rng = Lcg(7)
    noise = [rng.signed() for _ in range(N)]
    low = circular_lowpass(noise, 300.0)
    # Gusts: slow modulation with periods that divide the loop length.
    out = []
    for i, v in enumerate(low):
        t = i / SAMPLE_RATE
        gust = 0.55 + 0.30 * math.sin(2.0 * math.pi * t / 6.0) + 0.15 * math.sin(2.0 * math.pi * t / 4.0 + 1.3)
        out.append(v * gust)
    return normalize(out, 0.45)


def crowd():
    rng = Lcg(11)
    noise = [rng.signed() for _ in range(N)]
    band = circular_lowpass(noise, 900.0)
    high = circular_lowpass(noise, 180.0)
    voiced = [b - h for b, h in zip(band, high)]  # band-pass ~180-900 Hz: a murmur of distant voices
    out = []
    for i, v in enumerate(voiced):
        t = i / SAMPLE_RATE
        swell = 0.6 + 0.25 * math.sin(2.0 * math.pi * t / 12.0) + 0.15 * math.sin(2.0 * math.pi * t / 3.0 + 0.4)
        out.append(v * swell)
    return normalize(out, 0.5)


def birds():
    rng = Lcg(23)
    out = [0.0] * N
    # Short chirps (falling / rising sweeps) at pseudo-random times; the pattern repeats with the loop.
    t = 0.3
    while t < SECONDS - 0.4:
        f0 = 2500.0 + 2500.0 * rng.uniform()
        f1 = f0 * (0.6 + 0.8 * rng.uniform())
        length = 0.05 + 0.07 * rng.uniform()
        repeats = 1 + int(3 * rng.uniform())
        level = 0.4 + 0.6 * rng.uniform()
        for r in range(repeats):
            start = int((t + r * (length + 0.04)) * SAMPLE_RATE)
            n = int(length * SAMPLE_RATE)
            phase = 0.0
            for k in range(n):
                if start + k >= N:
                    break
                f = f0 + (f1 - f0) * k / n
                phase += 2.0 * math.pi * f / SAMPLE_RATE
                env = math.sin(math.pi * k / n) ** 2
                out[start + k] += level * env * math.sin(phase)
        t += 0.4 + 1.6 * rng.uniform()
    return normalize(out, 0.5)


def write_wav(path, samples):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for value in samples:
            frames += struct.pack("<h", int(round(max(-1.0, min(1.0, value)) * 32767.0)))
        handle.writeframes(bytes(frames))


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Saved", "Audio")
    for name, make in (("Crowd", crowd), ("Birds", birds), ("Wind", wind)):
        path = os.path.join(out_dir, f"A_Ambient_{name}.wav")
        samples = make()
        write_wav(path, samples)
        print(f"{path}: {len(samples)} samples, {len(samples) / SAMPLE_RATE:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
