"""Render Nimbo's two felt-like voice-room cues (stdlib only, no sampled audio)."""
import math
from pathlib import Path
import struct
import wave

RATE = 44100
DEST = Path(__file__).resolve().parents[1] / "web/src/assets/sounds"


def bloom(t, pitch, attack, decay):
    if t <= 0:
        return 0.0
    # Rounded onset, warm fundamental, two quiet partials that fade first.
    onset = math.sin(math.pi * .5 * min(1, t / attack)) ** 2
    return onset * sum(
        level * math.exp(-t / (decay * sustain)) * math.sin(math.tau * pitch * ratio * t)
        for ratio, level, sustain in ((1, 1, 1), (2, .18, .72), (3, .045, .52)))


def render(name, seconds, target_rms):
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        if name == "join":
            # Same pitch twice: a gentle pickup, then a fuller touch.
            sample = .57 * bloom(t, 392, .021, .070) + bloom(t - .121, 392, .029, .089)
        else:
            # A single lower, longer resonance. No friction or noise source.
            sample = bloom(t, 293.66, .040, .132)
        tail = math.sin(math.pi * .5 * min(1, (seconds - t) / .055)) ** 2
        samples.append(sample * tail)
    rms = math.sqrt(sum(x*x for x in samples) / len(samples))
    scale = min(target_rms / rms, .105 / max(map(abs, samples)))
    with wave.open(str(DEST / f"voice-{name}.wav"), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(x * scale * 32767)) for x in samples))


if __name__ == "__main__":
    render("join", .480, .022)
    render("leave", .515, .019)
