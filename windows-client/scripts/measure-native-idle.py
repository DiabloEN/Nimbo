"""Measure one standalone portable EXE at rest on disposable, repeatable chat data."""
import argparse
import json
from pathlib import Path
import secrets
import uuid
import shutil
import subprocess
import tempfile
from urllib.request import Request, urlopen

from native_qa_services import isolated

ROOT = Path(__file__).resolve().parents[1]


def working_set_map(process_id):
    """Classify resident pages of the test process by mapping, without reading their contents."""
    import ctypes as c
    from collections import Counter
    from ctypes import wintypes as w

    kernel, psapi = c.WinDLL("kernel32", use_last_error=True), c.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = (w.DWORD, w.BOOL, w.DWORD)
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = (w.HANDLE,)
    psapi.QueryWorkingSet.argtypes = (w.HANDLE, c.c_void_p, w.DWORD)
    psapi.GetMappedFileNameW.argtypes = (w.HANDLE, c.c_void_p, w.LPWSTR, w.DWORD)
    class Region(c.Structure):
        _fields_ = [("base", c.c_void_p), ("allocation", c.c_void_p), ("protect", w.DWORD),
                    ("partition", w.WORD), ("size", c.c_size_t), ("state", w.DWORD),
                    ("currentProtect", w.DWORD), ("kind", w.DWORD)]
    kernel.VirtualQueryEx.argtypes = (w.HANDLE, c.c_void_p, c.POINTER(Region), c.c_size_t)
    kernel.VirtualQueryEx.restype = c.c_size_t
    handle = kernel.OpenProcess(0x410, False, process_id)
    if not handle:
        raise c.WinError(c.get_last_error())
    try:
        from mmap import PAGESIZE
        capacity = 65536
        while True:
            pages = (c.c_size_t * capacity)()
            if psapi.QueryWorkingSet(handle, pages, c.sizeof(pages)):
                break
            if c.get_last_error() != 24 or capacity > 4000000:
                raise c.WinError(c.get_last_error())
            capacity = max(capacity * 2, pages[0] + 1)
        totals, group, end = Counter(), "", 0
        for page in sorted(pages[1:pages[0]+1]):
            address = page & ~(PAGESIZE - 1)
            if address >= end:
                region = Region()
                if not kernel.VirtualQueryEx(handle, address, c.byref(region), c.sizeof(region)):
                    continue
                end = region.base + region.size
                if region.kind == 0x1000000:
                    path = c.create_unicode_buffer(512)
                    psapi.GetMappedFileNameW(handle, region.allocation, path, len(path))
                    file = Path(path.value).name.lower()
                    if file.startswith(("qt6", "qwindows")): group = "Qt code and data"
                    elif file == "nimbodesktop.exe": group = "Nimbo executable"
                    elif file.startswith(("avcodec","avutil","avformat","swresample","swscale","ffmpeg","livekit","libmediapipe")): group = "Media libraries"
                    else: group = "Other loaded images"
                elif region.kind == 0x20000: group = "Private allocations including heaps and stacks"
                else: group = "Other mappings"
            totals[group] += PAGESIZE
        # Group surviving threads by their OS description. Read only metadata
        # from this fixture's process, never stacks or captured media.
        import psutil
        kernel.OpenThread.argtypes=(w.DWORD,w.BOOL,w.DWORD)
        kernel.OpenThread.restype=w.HANDLE
        kernel.GetThreadDescription.argtypes=(w.HANDLE,c.POINTER(c.c_void_p))
        kernel.LocalFree.argtypes=(c.c_void_p,)
        query_thread=c.WinDLL("ntdll").NtQueryInformationThread
        query_thread.argtypes=(w.HANDLE,w.ULONG,c.c_void_p,w.ULONG,c.c_void_p)
        names=Counter()
        for thread in psutil.Process(process_id).threads():
            h=kernel.OpenThread(0x0040 | 0x0800,False,thread.id)
            if not h:continue
            address=c.c_void_p()
            try:
                name=""
                if kernel.GetThreadDescription(h,c.byref(address))>=0 and address.value:
                    name=c.wstring_at(address)
                entry=c.c_void_p()
                path=c.create_unicode_buffer(512)
                if query_thread(h,9,c.byref(entry),c.sizeof(entry),None)>=0:
                    psapi.GetMappedFileNameW(handle,entry,path,len(path))
                names[(Path(path.value).name or "?")+" / "+(name or "(unnamed)")]+=1
            finally:
                if address.value:kernel.LocalFree(address)
                kernel.CloseHandle(h)
        return {**totals,"threadCounts":dict(names)}
    finally:
        kernel.CloseHandle(handle)

def reserve_trace_directory(requested):
    requested.parent.mkdir(parents=True, exist_ok=True)
    candidate = requested
    while True:
        try:
            candidate.mkdir()
            return candidate
        except FileExistsError:
            candidate = requested.with_name(f"{requested.name}-run-{uuid.uuid4().hex}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--client", required=True, type=Path)
    parser.add_argument("--server", type=Path, help="API executable for the isolated fixture")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--soak-seconds", type=int, default=120)
    parser.add_argument("--audio-photo", action="store_true", help="Measure audio pause/end and photo open/close in a standalone client")
    parser.add_argument("--workspace", action="store_true", help="After the audio/photo run also repeat settings navigation and voice joins")
    parser.add_argument("--cold-calls", action="store_true", help="Measure five calls separated by full media shutdown")
    parser.add_argument("--voice-cycles", action="store_true", help="Verify worker cleanup across repeated joins and room changes")
    parser.add_argument("--share-picker", action="store_true", help="Measure monitor and program previews across repeated open/cancel cycles")
    parser.add_argument("--share-picker-control", action="store_true", help="Compare the same call and idle intervals without opening the picker")
    parser.add_argument("--large-chat", action="store_true", help="Scroll mixed attachments, then leave the channel and switch server without playback")
    parser.add_argument("--settings-cycles", action="store_true", help="Measure each of the six settings tabs and the return to chat over three rounds")
    parser.add_argument("--settings-only", action="store_true", help="End the settings measurement before the mixed chat and call scenario")
    locale_mode=parser.add_mutually_exclusive_group()
    locale_mode.add_argument("--language-control", action="store_true", help="Repeat the same popup interactions while keeping Russian selected")
    locale_mode.add_argument("--language-cycles", action="store_true", help="Switch the running Portable between English and Russian and sample private memory")
    parser.add_argument("--chat-call-close", action="store_true", help="Switch between a quiet server, mixed chat and voice, then close the window during the call")
    parser.add_argument("--active-capture", action="store_true", help="During each local call, start and stop the first monitor and the default camera")
    parser.add_argument("--play-chat-video", action="store_true", help="Play and pause the last clip during each large-chat pass")
    parser.add_argument("--player-smoke", action="store_true", help="Play a real MP4 and YouTube in the supplied portable EXE on an isolated server")
    parser.add_argument("--youtube-id",default="umogLehRxgc",help="YouTube video used in the disposable player test")
    parser.add_argument("--verify-playback", "--verify-playback-worker", dest="verify_playback", action="store_true", help="Check the in-process decoder during playback and after pause")
    parser.add_argument("--capture-cycles", choices=("control","camera","screen","both"), help="Measure five capture toggles in one uninterrupted call")
    parser.add_argument("--capture-counters", action="store_true", help="Include the physical capture fixture's in-process heap and media counters")
    parser.add_argument("--wpr-heap", action="store_true", help="Record Windows heap allocation stacks for the fixture PID (requires WPR tracing rights)")
    parser.add_argument("--attachment-cycles", action="store_true", help="Repeat playback, pause and closing the photo viewer five times in the same chat")
    parser.add_argument("--heap-profiler", type=Path, help="Collect allocation stacks with heob64 on the local fixture")
    parser.add_argument("--heap-stages", nargs="+", help="Stages at which to report live allocations made since the previous snapshot")
    parser.add_argument("--pause-rest", action="store_true", help="Measure 30 seconds after the first video pause without opening another attachment")
    args = parser.parse_args()
    if args.capture_counters and not args.capture_cycles:
        parser.error("--capture-counters requires --capture-cycles")
    if args.wpr_heap and (not args.capture_cycles or args.heap_profiler):
        parser.error("--wpr-heap requires --capture-cycles and a separate run from --heap-profiler")
    if args.attachment_cycles:
        args.large_chat = args.play_chat_video = args.verify_playback = True
    if args.settings_only:args.settings_cycles=True
    if args.language_cycles or args.language_control:args.audio_photo=True
    if args.settings_cycles:args.chat_call_close=True
    if args.chat_call_close:
        args.large_chat = True
    if args.active_capture and not args.chat_call_close:
        parser.error("--active-capture requires --chat-call-close")
    if args.verify_playback and not args.play_chat_video:
        parser.error("--verify-playback-worker requires --play-chat-video")
    if args.play_chat_video and not args.large_chat:
        parser.error("--play-chat-video requires --large-chat")
    if args.pause_rest and not args.play_chat_video:
        parser.error("--pause-rest requires a video playback scenario")
    if args.heap_stages and not args.heap_profiler:
        parser.error("--heap-stages requires --heap-profiler")
    if args.workspace and not args.audio_photo:
        parser.error("--workspace requires --audio-photo")
    if args.soak_seconds < 120:
        parser.error("Use at least 120 seconds for the media sample")
    if args.wpr_heap:
        args.output = reserve_trace_directory(args.output)
        print(f"WPR output: {args.output.resolve()}", flush=True)
    else:
        args.output.mkdir(parents=True, exist_ok=True)
        if (args.output / "idle-memory.csv").exists():
            parser.error("Use a new output directory for each measurement")
    with tempfile.TemporaryDirectory(dir=args.output) as directory:
        temp = Path(directory)
        api = args.server.resolve(strict=True) if args.server else ROOT / ("artifacts/native-desktop/api-web67.exe" if args.player_smoke else "artifacts/native-desktop/api-ff2b773.exe")
        with isolated(ROOT, temp, api, suffixes=("", "2") if args.large_chat else ("",)) as env:
            if args.heap_profiler:
                env["NIMBO_QA_HEOB"] = str(args.heap_profiler.resolve(strict=True))
                env["NIMBO_QA_HEAP_STAGES"] = ",".join(args.heap_stages or ("idle","voice","capture-rest","left"))
            if args.pause_rest:
                env["NIMBO_QA_PAUSE_REST"]="1"
            env["NIMBO_QA_SILENT_OUTPUT"]="1"
            origin = env["NIMBO_NATIVE_TEST_URL"]
            headers = {"Origin": "https://desktop.nimbo.invalid", "X-Nimbo-Client": "1"}

            def send(method, path, data=None, content_type="application/json"):
                if data is not None and not isinstance(data, bytes):
                    data = json.dumps(data).encode()
                req = Request(origin + path, data, {**headers, "Content-Type": content_type}, method=method)
                with urlopen(req, timeout=15) as response:
                    return json.load(response)

            key = secrets.token_urlsafe(32)
            auth = send("POST", "/api/auth", {"profileKey": key, "name": "Тестер", "password": env["NIMBO_NATIVE_TEST_CODE"]})
            headers["Authorization"] = "Bearer " + auth["session"]
            profile = {"format": "nimbo-profile", "version": 1, "name": "Тестер", "avatar": "", "hotkeys": {},
                       "servers": [{"name": "Свои люди", "url": origin, "displayName": "Тестер",
                                    "profileKey": auth.get("profileKey", key)}]}
            profile["language"]="ru"  # Existing UI Automation scenarios use Russian control names.
            if args.language_cycles or args.language_control:
                env["NIMBO_QA_LANGUAGE_CYCLES"]="1"
                env["NIMBO_QA_CAPTURE_COUNTERS"]="1"
            if args.language_control:env["NIMBO_QA_LANGUAGE_CONTROL"]="1"
            def create_channel(label):
                send("POST", "/api/channels", {"kind": "text", "section": "text", "name": label})
                room = next(c for c in send("GET", "/api/workspace")["channels"] if c["name"] == label)
                path = "/api/channels/" + room["id"] + "/files"
                send("PATCH", path, {"enabled": True, "scan": False})
                return path

            def post_files(path, message, attachments):
                boundary = secrets.token_hex(24)
                parts = []
                for field, value in [("text", message), ("clientId", secrets.token_urlsafe(32))]:
                    parts.append((f'--{boundary}\r\nContent-Disposition: form-data; name="{field}"\r\n\r\n'
                                  + value + "\r\n").encode())
                for name, mime, data in attachments:
                    parts.extend([(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{name}"'
                                   f'\r\nContent-Type: {mime}\r\n\r\n').encode(), data, b"\r\n"])
                parts.append(("--" + boundary + "--\r\n").encode())
                send("POST", path, b"".join(parts), "multipart/form-data; boundary=" + boundary)

            def seed_channel(label, message, attachments):
                post_files(create_channel(label), message, attachments)

            if args.player_smoke:
                env.update(NIMBO_QA_AUDIO_PHOTO="1",NIMBO_QA_VERIFY_PLAYBACK="1",NIMBO_QA_PLAYER_SMOKE="1")
                channel=next(c["id"] for c in send("GET","/api/workspace")["channels"] if c["name"]=="общий")
                path="/api/channels/"+channel+"/files"
                send("PATCH",path,{"enabled":True,"scan":False})
                post_files(path,"Клип", [("flower.mp4","video/mp4",(ROOT/"artifacts/oct30-qa/preview-flower.mp4").read_bytes())])
                page=create_channel("YouTube")
                import re
                if not re.fullmatch(r"[A-Za-z0-9_-]{11}",args.youtube_id):parser.error("Invalid YouTube ID")
                env["NIMBO_QA_PLAYER_LINK"]="https://www.youtube.com/watch?v="+args.youtube_id
                env["NIMBO_QA_SECOND_LINK"]="https://www.youtube.com/watch?v=akbY0mkArMI"
                history=[f"История {i}" for i in range(30)]
                history += [env["NIMBO_QA_SECOND_LINK"],env["NIMBO_QA_PLAYER_LINK"]]
                history += [f"После ссылок {i}" for i in range(5)]
                for body in history:
                    send("POST","/api/messages",{"channel":page.split("/")[3],"text":body,"clientId":secrets.token_urlsafe(32)})
            elif args.large_chat:
                import io
                import wave
                from PIL import Image
                env["NIMBO_QA_AUDIO_PHOTO"] = env["NIMBO_QA_LARGE_CHAT"] = "1"
                if args.attachment_cycles:env["NIMBO_QA_ATTACHMENT_CYCLES"]="1"
                if args.chat_call_close:
                    env["NIMBO_QA_CHAT_CALL_CLOSE"]="1"
                    if args.settings_cycles:env["NIMBO_QA_SETTINGS_CYCLES"]="1"
                    if args.settings_only:env["NIMBO_QA_SETTINGS_ONLY"]="1"
                    if args.active_capture:env["NIMBO_QA_ACTIVE_CAPTURE"]="1"
                    invite=send("POST","/api/invitations",{"seconds":600,"singleUse":True})
                    observer=send("POST","/api/auth",{"name":"Наблюдатель","password":invite["code"],"profileKey":secrets.token_urlsafe(32)})
                    env["NIMBO_QA_OBSERVER_SESSION"]=observer["session"]
                if args.play_chat_video:env["NIMBO_QA_PLAY_CHAT_VIDEO"]="1"
                if args.verify_playback:env["NIMBO_QA_VERIFY_PLAYBACK"]="1"
                other = env["NIMBO_NATIVE_TEST_URL2"]
                payload = json.dumps({"profileKey": key, "name": "Тестер", "password": env["NIMBO_NATIVE_TEST_CODE2"]}).encode()
                with urlopen(Request(other+"/api/auth", payload, {"Content-Type":"application/json","X-Nimbo-Client":"1",
                                "Origin":"https://desktop.nimbo.invalid"}), timeout=15) as response:
                    second = json.load(response)
                profile["servers"].append({"name":"Второй","url":other,"displayName":"Тестер","profileKey":second.get("profileKey",key)})
                path = create_channel("Большая история")
                room = path.split("/")[3]
                photo = io.BytesIO()
                Image.effect_noise((2400,1600),45).convert("RGB").save(photo,"JPEG",quality=82)
                audio = io.BytesIO()
                with wave.open(audio,"wb") as wav:
                    wav.setparams((1,2,48000,0,"NONE",""))
                    wav.writeframes(bytes(48000*2*6))
                clip = (ROOT/"artifacts/oct30-qa/preview-flower.mp4").read_bytes()
                for group in range(5):
                    for i in range(6):
                        send("POST","/api/messages",{"channel":room,"text":f"Текст {group}-{i}","clientId":secrets.token_urlsafe(32)})
                    post_files(path,f"Галерея {group}",[(f"photo-{group}-{i}.jpg","image/jpeg",photo.getvalue()) for i in range(3)])
                    post_files(path,f"Запись {group}",[(f"audio-{group}.wav","audio/wav",audio.getvalue())])
                    post_files(path,f"Видео {group}",[(f"clip-{group}.mp4","video/mp4",clip)])
            elif args.capture_cycles:
                if args.wpr_heap:
                    env["NIMBO_QA_WPR_HEAP"] = "1"
                if args.capture_counters:
                    env["NIMBO_QA_CAPTURE_COUNTERS"] = "1"
                env["NIMBO_QA_AUDIO_PHOTO"]="1"
                env["NIMBO_QA_CAPTURE_CYCLES"]=args.capture_cycles
            elif args.share_picker or args.share_picker_control:
                env["NIMBO_QA_AUDIO_PHOTO"]=env["NIMBO_QA_SHARE_PICKER"]="1"
                if args.share_picker_control:env["NIMBO_QA_PICKER_CONTROL"]="1"
            elif args.cold_calls or args.voice_cycles:
                env["NIMBO_QA_VOICE_CYCLES" if args.voice_cycles else "NIMBO_QA_COLD_CALLS"]="1"
                env["NIMBO_QA_AUDIO_PHOTO"]="1"
            elif args.audio_photo:
                import io
                import wave
                from PIL import Image
                env["NIMBO_QA_AUDIO_PHOTO"] = "1"
                if args.workspace:
                    env["NIMBO_QA_WORKSPACE_MEMORY"] = "1"
                audio = io.BytesIO()
                with wave.open(audio, "wb") as sound:
                    sound.setparams((1, 2, 48000, 0, "NONE", ""))
                    sound.writeframes(bytes(48000 * 2 * 6))
                photo = io.BytesIO()
                Image.effect_noise((3200, 2100), 45).convert("RGB").save(photo, "JPEG", quality=88)
                seed_channel("Замер аудио", "Замер аудио", [("probe.wav", "audio/wav", audio.getvalue())])
                seed_channel("Замер фото", "Замер фото", [("probe.jpg", "image/jpeg", photo.getvalue())])
            else:
                clip = (ROOT / "artifacts/oct30-qa/preview-flower.mp4").read_bytes()
                seed_channel("Замер вложений", "Клипы для замера памяти",
                             [(f"clip-{i+1}.mp4", "video/mp4", clip) for i in range(4)])
            import sys
            (temp / "test-profile.json").write_text(json.dumps(profile), encoding="utf-8")
            env["NIMBO_QA_PYTHON"] = sys.executable
            env["NIMBO_QA_IDLE_SECONDS"] = str(args.soak_seconds)
            shell = shutil.which("pwsh") or shutil.which("powershell.exe")
            if not shell:
                raise RuntimeError("PowerShell is required for Windows UI measurements")
            command = [shell, "-NoProfile", "-File", ROOT / "scripts/compare-windows.ps1", "-IdleMemory",
                       "-Client", args.client.resolve(), "-Fixture", temp.resolve(), "-OutputDir", args.output.resolve()]
            if not args.large_chat:
                subprocess.run(command, env=env, check=True, timeout=args.soak_seconds+150)
                if args.player_smoke and args.youtube_id == "FxiqssoRBOg":
                    from PIL import Image
                    # This clip starts with a colored illustrated face. Exclude
                    # YouTube's title, central pause control and bottom buttons.
                    for frame in ("a", "b", "resumed"):
                        path = args.output / f"youtube-{frame}.png"
                        with Image.open(path) as image:
                            patch = image.convert("RGB").crop((25, 42, 155, 116))
                            colored = sum(max(rgb) - min(rgb) > 24 for rgb in patch.get_flattened_data())
                            if colored < 200:
                                raise RuntimeError(f"No visible video content in {path}")
                if args.capture_cycles:
                    counts = json.loads((args.output / "capture-rest-map.json").read_text(encoding="utf-8-sig"))["threadCounts"]
                    queues = sum(n for name,n in counts.items() if name.endswith(" / VideoEncoderQueue"))
                    limit = {"control":0, "camera":1, "screen":1, "both":2}[args.capture_cycles]
                    if queues > limit:
                        raise RuntimeError(f"{queues} video encoder queues survived repeated capture (expected at most {limit})")
                    import csv
                    with (args.output/"media-private.csv").open(encoding="utf-8-sig") as sample:
                        stopped=[int(row["WorkingSetPrivate"]) for row in csv.DictReader(sample) if "-off-" in row["phase"]]
                    # Skip the cold start. The reported regression added 20–60 MiB every cycle.
                    if len(stopped)!=5 or all(b-a > 8*1024**2 for a,b in zip(stopped[1:],stopped[2:])):
                        raise RuntimeError(f"Missing samples or repeated post-stop growth: {stopped}")
                return
            import csv
            import psutil
            import time
            with subprocess.Popen(command,env=env) as runner, (args.output/"process-tree.csv").open("w",newline="") as stream:
                writer=csv.writer(stream)
                writer.writerow(("seconds","children","parentWorkingSet","childrenWorkingSet"))
                started=time.monotonic()
                try:
                    pidfile=temp/"client.pid"
                    while runner.poll() is None:
                        elapsed=time.monotonic()-started
                        if elapsed>args.soak_seconds+150:
                            raise TimeoutError("Large chat test timed out")
                        if pidfile.exists():
                            try:
                                client=psutil.Process(int(pidfile.read_text(encoding="utf-8-sig").strip()))
                                parent=client.memory_info().rss
                                sizes=[]
                                for child in client.children(recursive=True):
                                    try:sizes.append(child.memory_info().rss)
                                    except psutil.NoSuchProcess:pass
                                writer.writerow((round(elapsed,2),len(sizes),parent,sum(sizes)))
                            except (ValueError,psutil.NoSuchProcess):pass
                        time.sleep(0.1)
                finally:
                    if runner.poll() is None:runner.kill()
                if runner.returncode:raise subprocess.CalledProcessError(runner.returncode,command)


if __name__ == "__main__":
    import sys
    if len(sys.argv) == 3 and sys.argv[1] == "--map-pid":
        print(json.dumps(working_set_map(int(sys.argv[2]))))
    else:
        main()
