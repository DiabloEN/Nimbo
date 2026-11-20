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

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--server",type=Path,required=True)
    parser.add_argument("--platform",choices=["offscreen","windows","windows:fontengine=directwrite","windows:fontengine=freetype","direct2d"],default="offscreen")
    parser.add_argument("--online-youtube",action="store_true",help="Require an actual online YouTube clip to start with one click")
    parser.add_argument("--capture-window",action="store_true",help="Also capture the test's own colored Windows window and verify its pixels on a second client")
    parser.add_argument("--local-devices",action="store_true",help="Exercise default microphone, output and camera in an empty disposable localhost room; do not save recordings")
    parser.add_argument("--browser-share-voice",type=Path,metavar="BROWSER",help="Run only the muted Chromium-to-Windows microphone test during five screen shares")
    parser.add_argument("--playwright-module",default="playwright",help="Node module name or path for the browser fixture")
    parser.add_argument("--settings-shots",type=Path,help="Save owner settings at two native window widths and run the workspace scenario")
    args=parser.parse_args()
    plugin=ROOT/"build-media/platforms"/("q"+args.platform.split(":")[0]+".dll")
    if not plugin.is_file():
        parser.error(f"Qt platform plugin missing from the test deployment: {plugin.name}")
    if args.capture_window and not args.platform.startswith("windows"):
        parser.error("--capture-window needs the Windows platform")
    server=args.server.resolve()
    out=ROOT/"artifacts/native-desktop";out.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        temp=Path(tmp);db=temp/"workspace.json";fixture=temp/"fixture.nimbo"
        profile_fixture(fixture,"write")
        updates=temp/"qa-updates"
        (updates/"requests").mkdir(parents=True)
        (updates/"status.json").write_text(json.dumps({"phase":"available","available":"server-v1.2.3","canInstall":True}))
        with isolated(ROOT,temp,server,suffixes=("", "2", "3", "4", "5") if args.local_devices else ("", "2", "3", "4"),update_dir=updates) as env:
            from urllib.request import urlopen
            with urlopen(env["NIMBO_NATIVE_TEST_URL"]+"/healthz",timeout=3) as r: installed=json.load(r)["version"]
            (updates/"status.json").write_text(json.dumps({"phase":"available","installed":installed,"available":"server-v1.2.3","previous":"server-v1.2.2","canInstall":True}))
            if args.settings_shots:env["NIMBO_SETTINGS_SHOTS"]=str(args.settings_shots.resolve())
            env["NIMBO_QA_UPDATE_INBOX"]=str(updates/"requests")
            env.update(NIMBO_WEB_FIXTURE=str(fixture), NIMBO_NATIVE_TEST_SCREENSHOT=str(out/"workspace.png"),QT_QPA_PLATFORM=args.platform)
            env["NIMBO_QA_SILENT_OUTPUT"]="1"
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
            report=temp/"native-tests.txt"
            result=subprocess.run([ROOT/"build-media/nimbo_desktop_tests.exe",*(["scrolledAntivirusUpdate","settingsParity"] if args.settings_shots else []),"-v2","-o",str(report)+",txt"],env=env,timeout=115)
            raw=report.read_bytes()
            ((args.settings_shots/"native-tests.txt") if args.settings_shots and args.settings_shots.is_dir() else (out/"native-03-tests.txt")).write_bytes(raw)
            try: summary=raw.decode("utf-8")
            except UnicodeDecodeError: summary=raw.decode("mbcs",errors="replace")
            print(summary,flush=True)
            result.check_returncode()
            if list(temp.rglob(".upload-*")):raise RuntimeError("Server retained temporary attachment files after the scenario")
            if not args.settings_shots:profile_fixture(fixture,"read")
if __name__=="__main__":
    main()
