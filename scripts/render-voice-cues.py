"""Render Nimbo's two original voice-room cues (stdlib only, no sampled audio)."""
import math
from pathlib import Path
import random
import struct
import wave

RATE = 44100
DEST = Path(__file__).resolve().parents[1] / "web/src/assets/sounds"


def knock(t):
    if t < 0:
        return 0
    # A small hollow wooden body: all its modes ring at the same time.
    modes = ((510, .060, .76), (952, .034, .32), (1583, .016, .18))
    return (1 - math.exp(-t / .0024)) * sum(
        amp * math.exp(-t / decay) * math.sin(math.tau * hz * t)
        for hz, decay, amp in modes)


def render(name, seconds):
    rng = random.Random(81431)
    slow = fast = 0.0
    samples = []
    for i in range(round(seconds * RATE)):
        t = i / RATE
        if name == "join":
            sample = .42 * knock(t) + knock(t - .079)
        else:
            # Band-limited friction, with a soft landing at the end of the brush.
            noise = rng.uniform(-1, 1)
            slow += .064 * (noise - slow)
            fast += .26 * (noise - fast)
            stroke = min(1, t / .192)
            envelope = math.sin(math.pi * stroke) ** 1.6
            grain = .82 + .18 * math.sin(math.tau * (39*t + 42*t*t))
            sample = 2.4 * envelope * grain * (fast - slow) + .44 * knock(1.45 * (t - .145))
        samples.append(sample * min(1, (seconds - t) / .015))
    rms = math.sqrt(sum(x*x for x in samples) / len(samples))
    scale = min(.031 / rms, .17 / max(map(abs, samples)))
    with wave.open(str(DEST / f"voice-{name}.wav"), "wb") as output:
        output.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<h", round(x * scale * 32767)) for x in samples))


if __name__ == "__main__":
    render("join", .310)
    render("leave", .270)
