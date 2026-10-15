"""Placeholder anechoic test signal for the auralization (SPEC §3.7): tone bursts, clicks, a noise burst and a sweep,
each followed by silence so the reverberation tail of the convolved room is audible between events.

Mono, 48 kHz, 16-bit PCM, about 6.4 s. Plain Python (no numpy). The owner replaces the imported asset
/Game/Audio/Anechoic/A_Anechoic_TestSignal with a real anechoic recording later.

    python Tools/Audio/make_anechoic_clip.py [out.wav]      # default: Saved/Audio/A_Anechoic_TestSignal.wav
"""
import math
import os
import struct
import sys
import wave

SAMPLE_RATE = 48000
PEAK = 0.7  # about -3 dBFS, matches the IR normalization


def hann(i, n):
    return 0.5 - 0.5 * math.cos(2.0 * math.pi * i / max(1, n - 1))


def tone_burst(freq_hz, seconds):
    n = int(seconds * SAMPLE_RATE)
    return [PEAK * hann(i, n) * math.sin(2.0 * math.pi * freq_hz * i / SAMPLE_RATE) for i in range(n)]


def silence(seconds):
    return [0.0] * int(seconds * SAMPLE_RATE)


def click_train(count, spacing_seconds, width_samples=2):
    out = []
    for k in range(count):
        out += [PEAK] * width_samples
        out += silence(spacing_seconds)[width_samples:]
    return out


def noise_burst(seconds, seed=12345):
    n = int(seconds * SAMPLE_RATE)
    state = seed
    out = []
    for i in range(n):
        state = (state * 6364136223846793005 + 1442695040888963407) & ((1 << 64) - 1)
        value = ((state >> 11) / float(1 << 53)) * 2.0 - 1.0
        out.append(PEAK * hann(i, n) * value)
    return out


def sweep(f0, f1, seconds):
    n = int(seconds * SAMPLE_RATE)
    out = []
    k = math.log(f1 / f0)
    for i in range(n):
        t = i / SAMPLE_RATE
        phase = 2.0 * math.pi * f0 * seconds / k * (math.exp(k * t / seconds) - 1.0)
        out.append(PEAK * hann(i, n) * math.sin(phase))
    return out


def build():
    signal = []
    signal += silence(0.25)
    signal += tone_burst(300.0, 0.06) + silence(0.8)
    signal += tone_burst(1000.0, 0.06) + silence(0.8)
    signal += tone_burst(3000.0, 0.06) + silence(0.8)
    signal += click_train(4, 0.15) + silence(0.8)
    signal += noise_burst(0.012) + silence(0.8)
    signal += sweep(200.0, 6000.0, 0.04) + silence(0.9)
    return signal


def write_wav(path, samples):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for value in samples:
            code = int(round(max(-1.0, min(1.0, value)) * 32767.0))
            frames += struct.pack("<h", code)
        handle.writeframes(bytes(frames))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Saved", "Audio", "A_Anechoic_TestSignal.wav")
    samples = build()
    write_wav(out, samples)
    peak = max(abs(v) for v in samples)
    print(f"{out}: {len(samples)} samples, {len(samples) / SAMPLE_RATE:.3f} s, peak {20.0 * math.log10(peak):.2f} dBFS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
