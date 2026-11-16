"""Install a pinned, project-local Qt/MinGW toolchain from Qt's HTTPS mirrors."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import argparse

import py7zr
import requests

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / ".tools" / "qt"
CACHE = ROOT / ".tools" / "downloads"
MIRRORS = ("https://qt.mirror.constant.com/", "https://ftp.fau.de/qtproject/")


def digest(path, algorithm):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def fetch(item):
    archive = CACHE / item["name"]
    if (archive.exists() and digest(archive, "sha1") == item["sha1"]
            and (not item.get("sha256") or digest(archive, "sha256") == item["sha256"])):
        return archive
    partial = archive.with_suffix(".part")
    errors = []
    for mirror in MIRRORS:
        try:
            print("Downloading " + item["name"], flush=True)
            with requests.get(mirror + item["path"], stream=True, timeout=(15, 90)) as response:
                response.raise_for_status()
                with partial.open("wb") as out:
                    for chunk in response.iter_content(1024 * 1024):
                        out.write(chunk)
            if digest(partial, "sha1") != item["sha1"]:
                raise RuntimeError("Archive checksum does not match the pinned Qt manifest")
            if item.get("sha256") and digest(partial, "sha256") != item["sha256"]:
                raise RuntimeError("Archive SHA-256 does not match the pinned manifest")
            partial.replace(archive)
            return archive
        except Exception as error:
            errors.append(str(error))
    raise RuntimeError(item["name"] + ": " + "; ".join(errors))


def main():
    global DEST
    parser = argparse.ArgumentParser()
    parser.add_argument("--msvc", action="store_true")
    args = parser.parse_args()
    manifest = "qt-msvc-manifest.json" if args.msvc else "toolchain-manifest.json"
    if args.msvc:
        DEST = ROOT / ".tools" / "qt-msvc"
    items = json.loads((ROOT / "scripts" / manifest).read_text(encoding="utf-8-sig"))
    CACHE.mkdir(parents=True, exist_ok=True)
    DEST.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        archives = list(pool.map(fetch, items))
    for archive in archives:
        print("Extracting " + archive.name, flush=True)
        with py7zr.SevenZipFile(archive) as package:
            for name in package.getnames():
                if not (DEST / name).resolve().is_relative_to(DEST.resolve()):
                    raise RuntimeError("Invalid archive path: " + name)
            package.extractall(DEST)
    qt_bin = DEST / "bin"
    (qt_bin / "qt.conf").write_text("[Paths]\nPrefix=..\n", encoding="utf-8")
    print("Toolchain ready: " + str(DEST), flush=True)


if __name__ == "__main__":
    main()
