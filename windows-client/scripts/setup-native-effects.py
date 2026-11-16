"""Restore fixed build dependencies for the native Windows voice/video effects."""
import hashlib
from pathlib import Path
import shutil
import tarfile
from urllib.request import urlopen
import json
import zipfile
root=Path(__file__).resolve().parents[1]
cache=root/".tools/downloads";media=root/".tools/media"
def fetch(name,url,sha):
    path=cache/name
    if not path.is_file():
        with urlopen(url,timeout=60) as response,path.open("wb") as dst:shutil.copyfileobj(response,dst)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=sha:raise RuntimeError(f"SHA-256 differs for {name}")
    return path
def main():
    cache.mkdir(exist_ok=True,parents=True);media.mkdir(exist_ok=True,parents=True)
    for name,url,digest in [
        ("wabt-1.0.39-windows-x64.tar.gz","https://github.com/WebAssembly/wabt/releases/download/1.0.39/wabt-1.0.39-windows-x64.tar.gz","c2215e52d3beff6cc0b6496aaef652399327084b9709ac5ec037b910c311a6c9"),
        ("simde-0.8.2.tar.gz","https://codeload.github.com/simd-everywhere/simde/tar.gz/refs/tags/v0.8.2","ed2a3268658f2f2a9b5367628a85ccd4cf9516460ed8604eed369653d49b25fb")]:
        with tarfile.open(fetch(name,url,digest)) as tar:tar.extractall(media,filter="data")
    wheel="mediapipe-0.10.32-py3-none-win_amd64.whl"
    url=""
    if not (cache/wheel).is_file():
        with urlopen("https://pypi.org/pypi/mediapipe/0.10.32/json",timeout=30) as response:files=json.load(response)["urls"]
        url=next(f["url"] for f in files if f["filename"]==wheel)
    with zipfile.ZipFile(fetch(wheel,url,"438d98977a4e1b0f88070ad16537a328912f847ce9debf236da047b0b483cbfd")) as z:
        target=media/"mediapipe";target.mkdir(exist_ok=True)
        (target/"libmediapipe.dll").write_bytes(z.read("mediapipe/tasks/c/libmediapipe.dll"))
if __name__=="__main__":main()
