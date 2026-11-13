"""Owner-requested server updates. Run as a host timer, outside the API container."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import stat
import tarfile
import tempfile
import time
import urllib.error
import urllib.request

REPO = "DiabloEN/Nimbo"
ASSET = "nimbo-server-linux-amd64.tar.gz"
TAG = re.compile(r"(?:server-)?v(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})\.(0|[1-9][0-9]{0,5})")
LIMIT = 128 * 1024 * 1024


def atomic(path, data, mode=0o600):
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        tmp = Path(f.name)
        try:
            os.fchmod(f.fileno(), mode)
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        except BaseException:
            tmp.unlink()
            raise
    tmp.replace(path)


def write_json(path, value, mode=0o600):
    atomic(path, json.dumps(value, ensure_ascii=False).encode(), mode)


def read_json(path, default=None):
    return json.loads(path.read_bytes()) if path.exists() else default


def version(tag):
    match = TAG.fullmatch(tag)
    return tuple(map(int, match.groups())) if match else None


def check_releases(cached):
    best = None
    deadline = time.monotonic() + 10
    for page in range(1, 101):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Release check timed out")
        req = urllib.request.Request(
            f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}",
            headers={"Accept": "application/vnd.github+json", "User-Agent": "Nimbo-server-updater"},
        )
        if page == 1 and cached.get("etag"):
            req.add_header("If-None-Match", cached["etag"])
        try:
            response = urllib.request.urlopen(req, timeout=min(6, remaining))
        except urllib.error.HTTPError as error:
            with error:
                if error.code == 304 and req.has_header("If-none-match"):
                    return cached["release"], cached["etag"]
                raise
        with response:
            rows = json.loads(response.read(8 * 1024 * 1024))
            etag = response.headers.get("ETag") if page == 1 else None
        for row in rows:
            tag = row["tag_name"]
            if not version(tag) or row["draft"] or row["prerelease"]:
                continue
            for asset in row["assets"]:
                if asset["name"] != ASSET or asset["state"] != "uploaded":
                    continue
                url = f"https://github.com/{REPO}/releases/download/{tag}/{ASSET}"
                digest = asset.get("digest") or ""
                trusted = (row.get("immutable") is True and asset["browser_download_url"] == url
                           and re.fullmatch(r"sha256:[0-9a-f]{64}", digest) and 0 < asset["size"] <= LIMIT)
                if best is None or version(tag) > version(best["tag"]):
                    best = dict(tag=tag, url=url, digest=digest, size=asset["size"], trusted=bool(trusted))
        if len(rows) < 100:
            # A validator for page one only covers the full list when it fits on one page.
            return best, etag
    raise ValueError("Слишком много страниц релизов GitHub")


def find_release():
    release, _ = check_releases({})
    return release


def stage_binary(release, stage):
    archive = stage / ASSET
    digest = hashlib.sha256()
    size = 0
    with urllib.request.urlopen(release["url"], timeout=25) as response, archive.open("xb") as out:
        while block := response.read(256 * 1024):
            size += len(block)
            if size > release["size"] or size > LIMIT:
                raise ValueError("Unexpected release size")
            digest.update(block)
            out.write(block)
    if size != release["size"] or "sha256:" + digest.hexdigest() != release["digest"]:
        raise ValueError("Release digest mismatch")
    with tarfile.open(archive, "r|gz") as bundle:
        for entry in bundle:
            if entry.name != "nimbo-server":
                continue
            if not entry.isfile() or not 0 < entry.size <= LIMIT:
                raise ValueError("Invalid server binary")
            with bundle.extractfile(entry) as src, (stage/"nimbo-server").open("xb") as out:
                shutil.copyfileobj(src, out)
            (stage/"nimbo-server").chmod(0o555)
            return
    raise ValueError("Missing server binary")


def call(*args, capture=False):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE if capture else None)


class Instance:
    def __init__(self, root):
        self.root = root
        self.share = root/"updates"
        self.pending = self.share/"requests/install"
        self.cache = root/"private/update-cache.json"
        self.journal = root/"private/update-transaction.json"
        self.gate = self.share/"maintenance"
        self.config = read_json(root/"installation.json")
        self.composefile = root/self.config.get("composeFile","compose.json")
        self.compose_format_json = self.composefile.name=="compose.json"
        self.data = (root/"data").resolve(strict=True)
        self.health = f"http://127.0.0.1:{self.config['apiPort']}/healthz"
        self.current = ""
        self.release = None
        self.checked_at = 0

    def compose(self,*args):
        call("docker","compose","--project-directory",str(self.root),"-f",str(self.composefile),*args)

    def status(self, phase, detail="", install=False):
        write_json(self.share/"status.json", {"phase":phase,"detail":detail,"installed":self.current,
                   "available":self.release["tag"] if self.release else "", "canInstall":install,
                   "checkedAt":self.checked_at}, 0o644)

    def running_version(self):
        with urllib.request.urlopen(self.health,timeout=3) as response:
            data=json.load(response)
        if data["status"]!="ok" or not data.get("version"):
            raise ValueError("The installed server does not support panel updates")
        return data["version"]

    def ready(self, expected):
        for _ in range(30):
            try:
                if self.running_version()==expected:
                    return
            except (OSError,ValueError):
                pass
            time.sleep(1)
        raise ValueError("Server did not start with the expected version")

    def restore(self, tx):
        backup = Path(tx["backup"])
        self.compose("stop","nimbo")
        if tx["snapshot"]:
            # Keep the failed generation; the mounted data directory itself stays in place.
            failed = backup/"failed-data"
            failed.mkdir(exist_ok=True)
            for child in self.data.iterdir():
                dest=failed/child.name
                if dest.exists(): dest=failed/(child.name+"-"+str(time.time_ns()))
                shutil.move(str(child),dest)
            snapshot_tree(backup/"data",self.data)
        atomic(self.composefile,(backup/self.composefile.name).read_bytes())
        if not self.compose_format_json: atomic(self.root/".env",(backup/".env").read_bytes())
        atomic(self.root/"nimbo-server",(backup/"nimbo-server").read_bytes(),0o755)
        self.compose("up","-d","--no-deps","--no-build","nimbo")
        self.ready(tx["previous"])
        self.finish()
        self.current=tx["previous"]
        self.status("restored")

    def finish(self):
        self.pending.unlink(missing_ok=True)
        self.journal.unlink(missing_ok=True)
        self.gate.unlink(missing_ok=True)

    def install(self, stage):
        expected=self.release["tag"]
        image=self.config["project"]+"-api:update-"+expected
        # The trusted host supplies the Dockerfile; the release supplies a single binary.
        build=stage/"image"; build.mkdir()
        os.link(stage/"nimbo-server",build/"nimbo-server")
        (build/"Dockerfile").write_text("FROM scratch\nCOPY nimbo-server /nimbo-server\nUSER 65532:65532\nENTRYPOINT [\"/nimbo-server\"]\n")
        call("docker","build","-t",image,str(build))
        actual=call("docker","run","--rm","--network","none","--read-only","--cap-drop","ALL",
                    "--security-opt","no-new-privileges","--memory","128m","--pids-limit","32",image,"--version",capture=True)
        if actual.stdout.strip()!=expected:
            raise ValueError("Release tag does not match executable version")
        backup=Path(tempfile.mkdtemp(prefix="before-update-",dir=self.root/"private"))
        for name in ("nimbo-server",self.composefile.name):
            shutil.copy2(self.root/name,backup/name)
        if not self.compose_format_json: shutil.copy2(self.root/".env",backup/".env")
        tx=dict(backup=str(backup),snapshot=False,previous=self.current)
        write_json(self.journal,tx)
        atomic(self.gate,b"1",0o644)
        try:
            self.status("installing")
            self.compose("stop","nimbo")
            snapshot_tree(self.data,backup/"data")
            tx["snapshot"]=True
            write_json(self.journal,tx)
            if self.compose_format_json:
                config=read_json(self.composefile)
                config["services"]["nimbo"]["image"]=image
            else:
                # Legacy dev Compose uses an image tag interpolated from .env.
                # Keep the original YAML and secrets in their existing files.
                content=(self.root/".env").read_text()
                if not re.search(r"(?m)^NIMBO_RELEASE=[^\n]+$",content):
                    raise ValueError("Legacy instance has no release selector")
                atomic(self.root/".env",re.sub(r"(?m)^NIMBO_RELEASE=[^\n]+$","NIMBO_RELEASE=update-"+expected,content).encode())
            atomic(self.root/"nimbo-server",(stage/"nimbo-server").read_bytes(),0o755)
            if self.compose_format_json: write_json(self.composefile,config)
            self.compose("up","-d","--no-deps","--no-build","nimbo")
            self.ready(expected)
        except Exception:
            self.restore(tx)
            return
        self.finish()
        self.current=expected
        self.status("current")

    def tick(self):
        if self.journal.exists():
            self.restore(read_json(self.journal)); return
        self.gate.unlink(missing_ok=True)
        self.current=self.running_version()
        requested=self.pending.exists()
        cached=read_json(self.cache,{})
        self.checked_at = int(cached.get("time", 0))
        self.release = cached.get("release")
        check = self.share / "requests/check"
        retry_due = time.time() >= cached.get("retryAfter", 0)
        if check.exists() or (retry_due and time.time() - self.checked_at > 21600):
            self.status("checking")
            try:
                self.release, etag = check_releases(cached)
            except (OSError, ValueError):
                self.status("error", "Не удалось связаться с GitHub. Попробуйте ещё раз.")
                check.unlink(missing_ok=True)
                write_json(self.cache, {**cached, "retryAfter": int(time.time()) + 900})
                return
            self.checked_at = int(time.time())
            write_json(self.cache, {"time": self.checked_at, "release": self.release, "etag": etag})
            check.unlink(missing_ok=True)
        elif cached.get("retryAfter") and not requested:
            self.status("error", "Не удалось связаться с GitHub. Попробуйте ещё раз.")
            return
        release=self.release
        can=bool(release and release["trusted"] and (version(self.current) is None or version(self.current)<version(release["tag"])))
        if requested:
            # The only writable container input is an opaque tag, never a URL, executable or path.
            fd=os.open(self.pending,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
            with os.fdopen(fd,"rb") as f:
                if not stat.S_ISREG(os.fstat(f.fileno()).st_mode):
                    raise ValueError("Invalid update request")
                tag=f.read(96).decode("ascii")
            if not can or tag!=release["tag"]:
                self.pending.unlink(); self.status("error","Выбранный релиз больше недоступен."); return
            if find_release()!=release:
                self.cache.unlink(missing_ok=True)
                self.pending.unlink()
                self.status("error","Релиз изменился. Проверьте новую версию.")
                return
            self.status("installing")
            with tempfile.TemporaryDirectory(dir=self.root/"private") as work:
                stage=Path(work)
                stage_binary(release,stage)
                self.install(stage)
        elif not release:
            self.status("empty")
        elif not release["trusted"]:
            self.status("error","У релиза нет подтверждённого неизменяемого пакета.")
        else:
            previous=read_json(self.share/"status.json",{})
            if previous.get("phase")=="restored" and previous.get("available")==release["tag"]:
                self.status("restored",install=can)
            else:
                self.status("available" if can else "current",install=can)


def snapshot_tree(source, destination):
    shutil.copytree(source,destination,dirs_exist_ok=True,symlinks=True,copy_function=copy_owned)
    for directory, children, _ in os.walk(source):
        target=destination/Path(directory).relative_to(source)
        st=os.stat(directory)
        os.chown(target,st.st_uid,st.st_gid)
        for name in children:
            item=Path(directory)/name
            if item.is_symlink():
                raise ValueError("Unexpected link in Nimbo storage")


def copy_owned(src,dst):
    shutil.copy2(src,dst)
    st=os.stat(src)
    os.chown(dst,st.st_uid,st.st_gid)
    return dst


def prepare(root,project):
    share=root/"updates"
    share.mkdir(exist_ok=True,mode=0o755); share.chmod(0o755)
    inbox=share/"requests"; inbox.mkdir(exist_ok=True,mode=0o700); os.chown(inbox,65532,65532)
    if not (share/"status.json").exists():
        write_json(share/"status.json",{"phase":"checking"},0o644)
    if not re.fullmatch(r"[a-z][a-z0-9-]{2,39}",project) or not re.fullmatch(r"/[\w./-]+",str(root)):
        raise ValueError("Invalid installation path")
    unit="nimbo-"+project+"-updates"
    units=Path("/etc/systemd/system")
    atomic(units/(unit+".service"),f"[Unit]\nAfter=docker.service network-online.target\n[Service]\nType=oneshot\nUMask=0077\nTimeoutStartSec=30min\nExecStart=/usr/bin/python3 {root}/server_update.py\n".encode(),0o644)
    atomic(units/(unit+".timer"),f"[Unit]\nDescription=Nimbo server updates ({project})\n[Timer]\nOnBootSec=25s\nOnUnitInactiveSec=20s\n[Install]\nWantedBy=timers.target\n".encode(),0o644)
    atomic(units/(unit+".path"),f"[Unit]\nDescription=Nimbo manual release check ({project})\n[Path]\nPathChanged={inbox}/check\n[Install]\nWantedBy=multi-user.target\n".encode(),0o644)
    call("systemctl","daemon-reload")
    call("systemctl","enable","--now",unit+".timer",unit+".path")


if __name__=="__main__":
    import fcntl
    root=Path(__file__).resolve().parent
    with (root/".control.lock").open("a") as lock:
        try: fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError: sys.exit(0)
        instance=Instance(root)
        try: instance.tick()
        except Exception as error:
            if not instance.journal.exists(): instance.pending.unlink(missing_ok=True)
            instance.status("error","Не удалось проверить или применить обновление. Подробности в журнале службы.")
            raise
