"""Exercise native login/chat on a disposable real Go server and WebCrypto profile interchange."""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import secrets
import socket
import subprocess
import tempfile
import time
from urllib.request import urlopen

from native_qa_services import isolated, profile_fixture

ROOT=Path(__file__).resolve().parents[1]

# These scenarios each exercise a fresh owner's first sign-in. The bootstrap
# invitation is single use and the main workspace scenario claims its own.
OWNER_SCENARIOS={
    "serverLinksLive":("", "2"),
    "voiceMoveCaptureCycles":("",),
    "recoverStalledVoiceOutput":("",),
    "livePresenterInk":("",),
}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--server",type=Path,required=True)
    parser.add_argument("--platform",choices=["offscreen","windows","windows:fontengine=directwrite","windows:fontengine=freetype","direct2d"],default="offscreen")
    parser.add_argument("--online-youtube",action="store_true",help="Require an actual online YouTube clip to start with one click")
    parser.add_argument("--capture-window",action="store_true",help="Also capture the test's own colored Windows window and verify its pixels on a second client")
    parser.add_argument("--windows-share-audio",action="store_true",help="Verify native window and monitor audio using Steam Streaming Speakers as a virtual endpoint")
    parser.add_argument("--local-devices",action="store_true",help="Exercise default microphone, output and camera in an empty disposable localhost room; do not save recordings")
    parser.add_argument("--browser-share-voice",type=Path,metavar="BROWSER",help="Run only the muted Chromium-to-Windows microphone test during five screen shares")
    parser.add_argument("--playwright-module",default="playwright",help="Node module name or path for the browser fixture")
    parser.add_argument("--browser-member-presence",type=Path,metavar="OUTPUT",help="Exercise two-way presence with a real browser and two isolated servers")
    parser.add_argument("--settings-shots",type=Path,help="Save owner settings at two native window widths and run the workspace scenario")
    args=parser.parse_args()
    plugin=ROOT/"build-media/platforms"/("q"+args.platform.split(":")[0]+".dll")
    if not plugin.is_file():
        parser.error(f"Qt platform plugin missing from the test deployment: {plugin.name}")
    if (args.capture_window or args.windows_share_audio) and not args.platform.startswith("windows"):
        parser.error("Real screen capture needs the Windows platform")
    server=args.server.resolve()
    if args.browser_member_presence:
        output=args.browser_member_presence.resolve()
        output.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=output) as temporary:
            temp=Path(temporary)
            with isolated(ROOT,temp,server) as env:
                env.update(NIMBO_QA_PRESENCE_DIR=temporary,NIMBO_QA_PRESENCE_OUTPUT=str(output),
                           NIMBO_PLAYWRIGHT=args.playwright_module,QT_QPA_PLATFORM=args.platform,
                           NIMBO_QA_SILENT_OUTPUT="1")
                peer=subprocess.Popen(["node",ROOT/"scripts/browser-member-presence.cjs"],env=env,cwd=ROOT)
                try:
                    subprocess.run([ROOT/"build-media/nimbo_desktop_tests.exe","memberPresenceLegacyResponse","memberPresenceCrossClient",
                                    "-o",str(output/"native-presence.txt")+",txt"],env=env,timeout=150,check=True)
                    if peer.wait(timeout=10):
                        raise RuntimeError("Browser presence observer failed")
                finally:
                    if peer.poll() is None:
                        peer.terminate()
                        peer.wait(timeout=10)
        return
    out=ROOT/"artifacts/native-desktop";out.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        temp=Path(tmp);db=temp/"workspace.json";fixture=temp/"fixture.nimbo"
        profile_fixture(fixture,"write")
        updates=temp/"qa-updates"
        (updates/"requests").mkdir(parents=True)
        (updates/"status.json").write_text(json.dumps({"phase":"available","available":"server-v1.2.3","canInstall":True}))
        suffixes=("",) if args.windows_share_audio else ("", "2", "3", "4", "5") if args.local_devices else ("", "2", "3", "4")
        with isolated(ROOT,temp,server,suffixes=suffixes,update_dir=updates) as env:
            from urllib.request import urlopen
            with urlopen(env["NIMBO_NATIVE_TEST_URL"]+"/healthz",timeout=3) as r: installed=json.load(r)["version"]
            (updates/"status.json").write_text(json.dumps({"phase":"available","installed":installed,"available":"server-v1.2.3","previous":"server-v1.2.2","canInstall":True}))
            if args.settings_shots:env["NIMBO_SETTINGS_SHOTS"]=str(args.settings_shots.resolve())
            env["NIMBO_QA_UPDATE_INBOX"]=str(updates/"requests")
            env.update(NIMBO_WEB_FIXTURE=str(fixture), NIMBO_NATIVE_TEST_SCREENSHOT=str(out/"workspace.png"),QT_QPA_PLATFORM=args.platform)
            env["NIMBO_QA_SILENT_OUTPUT"]="1"
            if args.windows_share_audio:
                binary=ROOT/"build-media/nimbo_desktop_tests.exe"
                sound=temp/"phrase.wav"
                env["NIMBO_SHARE_AUDIO_TEST_WINDOW"]="Nimbo QA "+secrets.token_hex(6)
                subprocess.run([binary,"--write-test-microphone",sound],env=env,check=True)
                source=subprocess.Popen([binary,"--virtual-share-audio",sound],env=env)
                try:
                    subprocess.run([binary,"windowsShareSound","-o",str(out/"windows-share-audio.txt")+",txt"],env=env,check=True,timeout=150)
                finally:
                    source.terminate()
                    source.wait(timeout=10)
                return
            if args.browser_share_voice:
                env.update(NIMBO_BROWSER_AUDIO_FIXTURE=str(temp),NIMBO_QA_BROWSER=str(args.browser_share_voice.resolve()),
                           NIMBO_PLAYWRIGHT=args.playwright_module)
                peer=subprocess.Popen(["node",ROOT/"scripts/browser-share-voice.cjs"],cwd=ROOT,env=env)
                try:
                    subprocess.run([ROOT/"build-media/nimbo_desktop_tests.exe","browserScreenShareVoice"],env=env,timeout=150,check=True)
                    if peer.wait(timeout=10):
                        raise RuntimeError("Browser screen share fixture failed")
                finally:
                    if peer.poll() is None:
                        peer.terminate()
                        peer.wait(timeout=10)
                return
            if args.online_youtube:env["NIMBO_QA_ONLINE_YOUTUBE"]="1"
            if args.local_devices:env["NIMBO_QA_LOCAL_DEVICES"]="1"
            if args.capture_window:env["NIMBO_QA_CAPTURE_WINDOW"]="1"
            binary=ROOT/"build-media/nimbo_desktop_tests.exe"
            results=[]
            logs=[]
            if args.settings_shots:
                functions=["scrolledAntivirusUpdate","settingsParity"]
            else:
                listing=subprocess.check_output([binary,"-functions"],env=env,text=True)
                functions=[name for name in re.findall(r"^(\w+)\(\)\s*$",listing,re.MULTILINE) if name not in OWNER_SCENARIOS]
                if not functions or any(name+"()" not in listing for name in OWNER_SCENARIOS):
                    raise RuntimeError("Could not enumerate the native test suite")
                for name,servers in OWNER_SCENARIOS.items():
                    scenario=temp/name
                    scenario.mkdir()
                    report=scenario/"native-tests.txt"
                    with isolated(ROOT,scenario,server,suffixes=servers) as clean:
                        clean.update(QT_QPA_PLATFORM=args.platform,NIMBO_QA_SILENT_OUTPUT="1")
                        results.append(subprocess.run([binary,name,"-v2","-o",str(report)+",txt"],env=clean,timeout=150))
                    logs.append(report.read_bytes())
            report=temp/"native-tests.txt"
            results.append(subprocess.run([binary,*functions,"-v2","-o",str(report)+",txt"],env=env,timeout=180))
            raw=b"\n".join([*logs,report.read_bytes()])
            ((args.settings_shots/"native-tests.txt") if args.settings_shots and args.settings_shots.is_dir() else (out/"native-03-tests.txt")).write_bytes(raw)
            try: summary=raw.decode("utf-8")
            except UnicodeDecodeError: summary=raw.decode("mbcs",errors="replace")
            print(summary,flush=True)
            for result in results:result.check_returncode()
            if list(temp.rglob(".upload-*")):raise RuntimeError("Server retained temporary attachment files after the scenario")
            if not args.settings_shots:profile_fixture(fixture,"read")
if __name__=="__main__":
    main()
