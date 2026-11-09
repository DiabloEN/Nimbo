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
    onset = math.sin(math.pi * .5 * min(1, t / attack)) ** 2
    return onset * sum(
        level * math.exp(-t / (decay * sustain)) * math.sin(math.tau * pitch * ratio * t)
        for ratio, level, sustain in ((1, 1, 1), (2, .18 * presence, .72), (3, .045 * presence, .52)))


def render(name, seconds, target_rms):
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        if name == "join":
            # Approach: the far, lower touch resolves into a warmer, nearer one.
            sample = .50 * bloom(t, 293.66, .030, .066, .55) + bloom(t - .126, 392, .024, .096, 1)
        elif name == "leave":
            # Recede: the near touch leaves a softer, lower, more distant answer.
            sample = .89 * bloom(t, 392, .028, .069, 1) + .48 * bloom(t - .146, 293.66, .038, .134, .42)
        else:
            notes = MEDIA[name]
            sample = sum(level * bloom(t - start, pitch, .015, .067, .58)
                         for start, pitch, level in notes)
        tail = math.sin(math.pi * .5 * min(1, (seconds - t) / .055)) ** 2
        samples.append(sample * tail)
    rms = math.sqrt(sum(x*x for x in samples) / len(samples))
    scale = min(target_rms / rms, .105 / max(map(abs, samples)))
    with wave.open(str(DEST / f"voice-{name}.wav"), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(x * scale * 32767)) for x in samples))


# Short, quieter confirmations, distinct from room arrival and departure.
MEDIA = {
    "mute": [(0, 220, .72)],
    "unmute": [(0, 294, .56), (.102, 440, .68)],
    "video-on": [(0, 415, .66), (.040, 523, .43)],
    "video-off": [(0, 523, .48), (.083, 370, .62)],
}

if __name__ == "__main__":
    render("join", .486, .022)
    render("leave", .604, .019)
    for name in MEDIA:
        render(name, .32, .014)
