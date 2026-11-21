"""Compare a generated 1440p window against the pre-encode image and real browser decode."""

import argparse
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
import tempfile
import time
from urllib.request import Request, urlopen
import psutil
from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageStat
from native_qa_services import isolated

root = Path(__file__).resolve().parents[1]


def make_scene(directory):
    rng = random.Random(554)
    size = (2560, 1440)
    # Natural detail plus seeded, multi-scale texture; no user content.
    photo = Image.open(root / "web/public/effects/room-preview.png").convert("RGB")
    photo = photo.crop((0, 0, photo.width // 2, photo.height)).resize(
        size, Image.Resampling.BICUBIC
    )
    grain = Image.frombytes("L", (1280, 720), rng.randbytes(1280 * 720)).resize(
        size, Image.Resampling.BILINEAR
    )
    texture = Image.blend(photo, grain.convert("RGB"), 0.30)
    hud = Image.new("RGB", (820, 290), "#1e242e")
    draw = ImageDraw.Draw(hud)
    for i, px in enumerate([11, 12, 13, 15, 18, 20, 24]):
        font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", px)
        draw.text(
            (12, 8 + i * 37),
            f"Objective {i + 1}   Gather supplies 17/32   Северные ворота   0123456789",
            fill=["#e8dd83", "#eeeeee", "#68cbe8"][i % 3],
            font=font,
        )
    texture.save(directory / "texture.png")
    hud.save(directory / "hud.png")
    return texture, hud


def reference(texture, hud, sequence, size):
    img = ImageChops.offset(texture, -8 * sequence, 0)
    img.paste(hud, (30, 60))
    draw = ImageDraw.Draw(img)
    for bit in range(8):
        x = 32 + 32 * bit
        draw.rectangle((x, 12, x + 27, 39), fill="white" if sequence & (1 << bit) else "black")
    return img.resize(size, Image.Resampling.LANCZOS).convert("L")


def compare(file, texture, hud):
    im = Image.open(file).convert("L")
    sx = im.width / 2560
    sy = im.height / 1440
    sequence = sum(
        1 << i for i in range(8) if im.getpixel((round((46 + i * 32) * sx), round(25 * sy))) > 128
    )
    ref = reference(texture, hud, sequence, im.size)
    diff = ImageChops.difference(ref, im)
    crop = diff.crop((round(40 * sx), round(65 * sy), round(600 * sx), round(345 * sy)))

    def psnr(image):
        rms = ImageStat.Stat(image).rms[0]
        return round(20 * math.log10(255 / max(0.001, rms)), 2)

    return dict(sequence=sequence, hudDb=psnr(crop), frameDb=psnr(diff))


def run(a):
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        directory = Path(tmp)
        texture, hud = make_scene(directory)
        with isolated(root, directory, a.server.resolve(), suffixes=("",)) as env:
            api = env["NIMBO_NATIVE_TEST_URL"]

            def post(path, body, token=""):
                headers = {
                    "Content-Type": "application/json",
                    "Origin": "https://desktop.nimbo.invalid",
                    "X-Nimbo-Client": "1",
                }
                if token:
                    headers["Authorization"] = "Bearer " + token
                with urlopen(Request(api + path, json.dumps(body).encode(), headers)) as response:
                    return json.load(response)

            owner = post(
                "/api/auth", {"name": "Quality", "password": env["NIMBO_NATIVE_TEST_CODE"]}
            )
            invite = post("/api/invitations", {"seconds": 300, "singleUse": True}, owner["session"])
            guest = post("/api/auth", {"name": "Viewer", "password": invite["code"]})
            for name, member in [("sender", owner), ("viewer", guest)]:
                data = post("/api/join", {"room": "lounge", "name": name}, member["session"])
                (directory / (name + ".json")).write_text(
                    json.dumps({k: data[k] for k in ("url", "token")})
                )
            exe = a.probe.resolve()
            env.update(QT_QPA_PLATFORM="windows", NIMBO_PLAYWRIGHT=a.playwright)
            processes = []
            try:

                def start(args):
                    p = subprocess.Popen(args, cwd=root, env=env)
                    processes.append(p)
                    return p

                scene = start([a.scene.resolve() if a.scene else exe, "--quality-scene", directory])
                time.sleep(1)
                if scene.poll() is not None:
                    raise RuntimeError("Generated capture window did not start")
                viewer = start(
                    ["node", root / "scripts/share-quality-viewer.cjs", directory, out, a.browser]
                )
                sender = start(
                    [
                        exe,
                        "--invite",
                        directory / "sender.json",
                        "--quality-window",
                        "Nimbo QA " + directory.name,
                        "--raw-preview",
                        out / "pre-encode.png",
                        "--report",
                        directory / "sender-report.json",
                        "--seconds",
                        "70",
                    ]
                )
                proc = psutil.Process(sender.pid)
                if a.cores:
                    proc.cpu_affinity(proc.cpu_affinity()[: a.cores])
                proc.cpu_percent()
                cpu = []
                deadline = time.monotonic() + 58
                while (
                    viewer.poll() is None and time.monotonic() < deadline and sender.poll() is None
                ):
                    time.sleep(0.75)
                    sample = proc.cpu_percent()
                    if (directory / "motion").exists():
                        cpu.append(sample)
                if viewer.wait(timeout=2) != 0:
                    raise RuntimeError("Browser receiver failed")
            finally:
                for p in reversed(processes):
                    if p.poll() is None:
                        p.terminate()
                    p.wait(timeout=10)
        stats = json.loads((out / "receiver.json").read_bytes())
        b, e = stats["before"], stats["after"]
        seconds = (e["ts"] - b["ts"]) / 1000
        intervals = sorted(y - x for x, y in zip(stats["times"], stats["times"][1:]))
        report = dict(
            preEncode=compare(out / "pre-encode.png", texture, hud),
            static=compare(out / "received-static.png", texture, hud),
            motion=[compare(f, texture, hud) for f in sorted(out.glob("received-motion-*.png"))],
            fps=round((e["decoded"] - b["decoded"]) / seconds, 2),
            mbps=round((e["bytes"] - b["bytes"]) * 8 / seconds / 1e6, 2),
            presentationP95Ms=round(intervals[math.ceil(0.95 * len(intervals)) - 1], 2),
            lost=e["lost"] - b["lost"],
            dropped=e["dropped"] - b["dropped"],
            freezes=e["freezes"] - b["freezes"],
            width=e["width"],
            height=e["height"],
            senderCpuCorePercent=round(statistics.mean(cpu), 1),
            affinity=a.cores,
        )
        if len({sample["sequence"] for sample in report["motion"]}) < 2:
            raise RuntimeError("Decoded frame markers did not advance in the motion sample")
        (out / "report.json").write_text(json.dumps(report, indent=2))
        print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    for key in ("server", "probe", "output"):
        p.add_argument("--" + key, type=Path, required=True)
    p.add_argument("--scene", type=Path)
    p.add_argument("--cores", type=int, default=0)
    p.add_argument("--playwright", default="playwright")
    p.add_argument("--browser", default="C:/Program Files/Google/Chrome/Application/chrome.exe")
    run(p.parse_args())
