"""Writes Saved/Audio/A_Click_48k.wav: a 1-sample click (full scale) followed by 3 s of silence, mono 48 kHz 16-bit (M9).

The industry click recorder (UClickResponseRecorder, SPEC §3.8) plays it through the plugin source; the silence keeps the
source alive while the plugin's reflections ring out. Plain Python, no numpy:

  python Tools/Audio/make_click.py
"""
import os
import struct
import wave

SAMPLE_RATE = 48000
SILENCE_SECONDS = 3.0


def main():
    project = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    out_dir = os.path.join(project, "Saved", "Audio")
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "A_Click_48k.wav")
    frames = [32767] + [0] * int(SILENCE_SECONDS * SAMPLE_RATE)
    with wave.open(path, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(struct.pack("<%dh" % len(frames), *frames))
    print(f"{path}: {len(frames)} samples at {SAMPLE_RATE} Hz")


if __name__ == "__main__":
    main()
