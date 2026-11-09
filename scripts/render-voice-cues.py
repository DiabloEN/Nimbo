"""Render Nimbo's room and media cues (stdlib only, no sampled audio)."""

import math
from pathlib import Path
import struct
import wave

RATE = 44100
DEST = Path(__file__).resolve().parents[1] / "web/src/assets/sounds"


def bloom(t, pitch, attack, decay, presence):
    if t <= 0:
        return 0.0
    # Rounded onset, warm fundamental, two quiet partials that fade first.
    onset = math.sin(math.pi * 0.5 * min(1, t / attack)) ** 2
    return onset * sum(
        level * math.exp(-t / (decay * sustain)) * math.sin(math.tau * pitch * ratio * t)
        for ratio, level, sustain in (
            (1, 1, 1),
            (2, 0.18 * presence, 0.72),
            (3, 0.045 * presence, 0.52),
        )
    )


def render(name, seconds, target_rms):
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        if name == "join":
            # Approach: the far, lower touch resolves into a warmer, nearer one.
            sample = 0.50 * bloom(t, 293.66, 0.030, 0.066, 0.55) + bloom(
                t - 0.126, 392, 0.024, 0.096, 1
            )
        elif name == "leave":
            # Recede: the near touch leaves a softer, lower, more distant answer.
            sample = 0.89 * bloom(t, 392, 0.028, 0.069, 1) + 0.48 * bloom(
                t - 0.146, 293.66, 0.038, 0.134, 0.42
            )
        else:
            notes = MEDIA[name]
            sample = sum(
                level * bloom(t - start, pitch, 0.015, 0.067, 0.58) for start, pitch, level in notes
            )
        tail = math.sin(math.pi * 0.5 * min(1, (seconds - t) / 0.055)) ** 2
        samples.append(sample * tail)
    rms = math.sqrt(sum(x * x for x in samples) / len(samples))
    scale = min(target_rms / rms, 0.105 / max(map(abs, samples)))
    with wave.open(str(DEST / f"voice-{name}.wav"), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(x * scale * 32767)) for x in samples))


# Short, quieter confirmations, distinct from room arrival and departure.
MEDIA = {
    "mute": [(0, 220, 0.72)],
    "unmute": [(0, 294, 0.56), (0.102, 440, 0.68)],
    "video-on": [(0, 415, 0.66), (0.040, 523, 0.43)],
    "video-off": [(0, 523, 0.48), (0.083, 370, 0.62)],
}

if __name__ == "__main__":
    render("join", 0.486, 0.022)
    render("leave", 0.604, 0.019)
    for name in MEDIA:
        render(name, 0.32, 0.014)
