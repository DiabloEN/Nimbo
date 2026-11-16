"""Build the per-user Windows installer from the Release binary and approved web assets."""
import argparse
import hashlib
import json
import math
import struct
import wave
import re
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import xml.etree.ElementTree as ET

from fontTools.merge import Merger
from fontTools.ttLib import TTFont
from resvg_py import svg_to_bytes
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.search(r'NIMBO_DESKTOP_VERSION "(\d+\.\d+\.\d+)"', (ROOT/"desktop/version.h").read_text()).group(1)
def write_chat_chime(path):
    # Same two sine oscillators and gain ramps as useMentionAlerts.ts.
    rate = 48000
    samples = [0.] * round(.34 * rate)
    for delay, frequency in ((0., 660), (.12, 880)):
        start = round(delay * rate)
        for n in range(round(.22 * rate)):
            t = n / rate
            gain = .045 * t / .012 if t < .012 else .045 * (.0001 / .045) ** (min(t - .012, .188) / .188)
            samples[start+n] += gain * math.sin(2 * math.pi * frequency * t)
    with wave.open(str(path), "wb") as audio:
        audio.setparams((1, 2, rate, 0, "NONE", "not compressed"))
        audio.writeframes(struct.pack(f"<{len(samples)}h", *(round(s * 32767) for s in samples)))


def assets(web, destination):
    destination.mkdir(exist_ok=True)
    module = (web / "src/composables/iconsax.ts").resolve().as_uri()
    switcher = (web/"src/components/ServerSwitcher.vue").read_text(encoding="utf-8")
    source_svg = re.search(r"<svg\b[^>]*>.*?</svg>",switcher,re.S).group()
    source_svg = re.sub(r"<svg\b", '<svg xmlns="http://www.w3.org/2000/svg" color="#aaa"',source_svg,count=1)
    (destination/"servers.png").write_bytes(svg_to_bytes(svg_string=source_svg,width=64,height=64))
    app = (web/"src/App.vue").read_text(encoding="utf-8")
    heading = re.search(r'class="channel-label text-channels-label"\s*>\s*<span\b[^>]*>\s*(<svg\b.*?</svg>)', app, re.S)
    if heading is None:
        raise ValueError("Original text-channel heading icon missing")
    section_svg = heading[1].replace("<svg ", '<svg xmlns="http://www.w3.org/2000/svg" color="#888" ', 1)
    (destination/"section-hash.png").write_bytes(svg_to_bytes(svg_string=section_svg,width=64,height=64))
    password = (web/"src/components/PasswordInput.vue").read_text(encoding="utf-8")
    eye = re.search(r"<svg\b[^>]*>.*?</svg>",password,re.S).group()
    eye = re.sub(r"<svg\b", '<svg xmlns="http://www.w3.org/2000/svg" color="#aaa"',eye,count=1)
    (destination/"password-eye.png").write_bytes(svg_to_bytes(svg_string=eye,width=64,height=64))
    source = "import {iconsax} from " + json.dumps(module) + "; process.stdout.write(JSON.stringify(iconsax));"
    icons = json.loads(subprocess.check_output(["node", "--input-type=module", "-e", source]))
    for name, geometry in icons.items():
        svg = ET.Element("svg", xmlns="http://www.w3.org/2000/svg", viewBox="0 0 24 24", fill="none", color="#ededed")
        for part in geometry:
            ET.SubElement(svg, part["tag"], part["attrs"])
        (destination / (name+".png")).write_bytes(svg_to_bytes(svg_string=ET.tostring(svg, encoding="unicode"), width=64, height=64))
    shutil.copy2(ROOT/"desktop/nimbo.ico",destination/"nimbo.ico")
    for weight in (400,500,700):
        subsets = [web/f"src/assets/fonts/roboto-{subset}-{weight}-normal.woff2" for subset in ("latin","cyrillic")]
        font = Merger().merge([str(path) for path in subsets])
        # Merger discards TrueType instructions in every subset after the first.
        # The shipped subsets share hint programs and control values, so glyph
        # instructions can be retained even when the merger renames a component.
        with TTFont(subsets[0]) as latin, TTFont(subsets[1]) as cyrillic:
            for tag in ("fpgm", "prep", "cvt ", "gasp"):
                if latin.getTableData(tag) != cyrillic.getTableData(tag):
                    raise ValueError(f"Incompatible Roboto hint table {tag} at weight {weight}")
            merged_names = font.getGlyphOrder()[len(latin.getGlyphOrder()):]
            for name, merged_name in zip(cyrillic.getGlyphOrder(), merged_names, strict=True):
                source_glyph = cyrillic["glyf"][name]
                if hasattr(source_glyph, "program"):
                    font["glyf"][merged_name].program = source_glyph.program
        font.flavor = None
        font.save(destination/f"Roboto-{weight}.ttf")
    photo = web/"public/effects/room-preview.png"
    if hashlib.sha256(photo.read_bytes()).hexdigest() != "71f7b6434bcae4846ee3055ea2c93500db457d3f07eba79a8082f0d66e606cf5":
        raise ValueError("Approved camera preview is missing or changed")
    shutil.copy2(photo, destination/photo.name)
    model=web/"public/effects/selfie_segmenter.tflite"
    if hashlib.sha256(model.read_bytes()).hexdigest()!="191ac9529ae506ee0beefa6b2c945a172dab9d07d1e802a290a4e4038226658b":raise ValueError("Approved camera model differs")
    shutil.copy2(model,destination/model.name)
    shutil.copytree(web/"src/assets/sounds",destination/"sounds",dirs_exist_ok=True)
    for cue in (ROOT/"desktop/sounds").glob("*.wav"):
        shutil.copy2(cue,destination/"sounds"/cue.name)
    for name in ("join","leave","mute","unmute","camera","screen","video-on","video-off"):
        if not (destination/"sounds"/f"voice-{name}.wav").is_file():
            raise ValueError(f"Approved voice cue missing from web assets: voice-{name}.wav")
    write_chat_chime(destination/"sounds/chat.wav")


def build_installer(compiler, work, stage, output):
    def path_value(path):
        return str(path).replace("$", "$$").replace('"', '$\\"')

    # NSIS copies the directory entries verbatim. Windows shell image lists
    # also need bitmap frames when extracting a small icon from its PE stub.
    installer_ico = work/"setup.ico"
    with Image.open(stage/"assets/nimbo.ico") as source:
        frames = [source.ico.getimage(size).convert("RGBA") for size in sorted(source.ico.sizes())]
        frames[-1].save(installer_ico, format="ICO", sizes=[f.size for f in frames],
                        append_images=frames[:-1], bitmap_format="bmp")
    icon = path_value(installer_ico)
    mui_icons = [f'!define MUI_ICON "{icon}"', f'!define MUI_UNICON "{icon}"']
    lines = [
        "Unicode true", "RequestExecutionLevel user", "SetCompressor /SOLID lzma",
        '!include "MUI2.nsh"', 'Name "Nimbo"',
        f'OutFile "{path_value(output)}"', r'InstallDir "$LOCALAPPDATA\Programs\Nimbo"',
        r'InstallDirRegKey HKCU "Software\Nimbo\DesktopPreview" "InstallDir"',
        *mui_icons,
        '!insertmacro MUI_PAGE_WELCOME', '!insertmacro MUI_PAGE_INSTFILES', '!define MUI_FINISHPAGE_RUN "$INSTDIR\\NimboDesktop.exe"',
        '!insertmacro MUI_PAGE_FINISH', '!insertmacro MUI_UNPAGE_CONFIRM', '!insertmacro MUI_UNPAGE_INSTFILES',
        '!insertmacro MUI_LANGUAGE "Russian"', '!insertmacro MUI_LANGUAGE "English"', f'VIProductVersion "{VERSION}.0"',
        f'VIAddVersionKey "FileVersion" "{VERSION}"', 'VIAddVersionKey "FileDescription" "Nimbo"',
        'VIAddVersionKey "ProductName" "Nimbo"',
        'VIAddVersionKey "LegalCopyright" "Nimbo contributors"', 'Section "Nimbo"',
        'SetShellVarContext current', 'SetOutPath "$INSTDIR"', f'File /r "{path_value(stage)}\\*"',
        'WriteUninstaller "$INSTDIR\\Uninstall.exe"',
        r'Delete "$SMPROGRAMS\Nimbo Desktop Preview.lnk"',
        r'Delete "$SMPROGRAMS\Nimbo Desktop.lnk"',
        r'CreateShortcut "$SMPROGRAMS\Nimbo.lnk" "$INSTDIR\NimboDesktop.exe" "" "$INSTDIR\assets\nimbo.ico"',
        r'WriteRegStr HKCU "Software\Nimbo\DesktopPreview" "InstallDir" "$INSTDIR"',
    ]
    uninstall_key = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\NimboDesktopPreview"
    for field, value in [("DisplayName","Nimbo"),("DisplayVersion",VERSION),("Publisher","Nimbo"),
                         ("DisplayIcon",r"$INSTDIR\assets\nimbo.ico"),("UninstallString",r'$\"$INSTDIR\Uninstall.exe$\"')]:
        lines.append(f'WriteRegStr HKCU "{uninstall_key}" "{field}" "{value}"')
    lines += ['SectionEnd','Section "Uninstall"','SetShellVarContext current']
    for path in sorted(stage.rglob("*"),key=lambda p:len(p.parts),reverse=True):
        command = "RMDir" if path.is_dir() else "Delete"
        lines.append(f'{command} "$INSTDIR\\{path_value(path.relative_to(stage))}"')
    lines += [r'Delete "$SMPROGRAMS\Nimbo.lnk"',r'Delete "$INSTDIR\uninstall-update.bin"',r'Delete "$INSTDIR\Uninstall.exe"',
              'RMDir "$INSTDIR"',r'DeleteRegKey HKCU "Software\Nimbo\DesktopPreview"',
              f'DeleteRegKey HKCU "{uninstall_key}"','SectionEnd']
    script=work/"setup.nsi"
    script.write_text("\n".join(lines),encoding="utf-8-sig")
    subprocess.run([compiler,"/V2",script],check=True)
    # Previous installed clients preserve their old uninstaller during a ZIP update.
    # Generate the matching NSIS uninstaller for the new client's first launch.
    emitter = work/"emit-uninstaller.exe"
    export = ['Unicode true', 'RequestExecutionLevel user', 'SilentInstall silent',
              '!include "MUI2.nsh"', 'Name "Nimbo"', f'OutFile "{path_value(emitter)}"',
              *mui_icons,
              '!insertmacro MUI_UNPAGE_CONFIRM', '!insertmacro MUI_UNPAGE_INSTFILES',
              '!insertmacro MUI_LANGUAGE "Russian"', '!insertmacro MUI_LANGUAGE "English"',
              'Section', f'WriteUninstaller "{path_value(stage/"uninstall-update.bin")}"', 'SectionEnd']
    script.write_text("\n".join(export + lines[lines.index('Section "Uninstall"'):]), encoding="utf-8-sig")
    subprocess.run([compiler,"/V2",script],check=True)
    subprocess.run([emitter],check=True)


def main():
    p=argparse.ArgumentParser()
    p.add_argument("--web",type=Path,required=True)
    p.add_argument("--nsis",type=Path,default=ROOT/".tools/nsis/Bin/makensis.exe")
    args=p.parse_args()
    dist=ROOT/"dist"
    dist.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=ROOT/"build-media") as tmp:
        work=Path(tmp); stage=work/"client"; stage.mkdir()
        runtime=ROOT/"build-media"
        shutil.copy2(runtime/"NimboDesktop.exe",stage)
        subprocess.run([ROOT/".tools/qt-msvc/bin/windeployqt.exe","--release","--no-translations","--no-compiler-runtime",
                        "--no-opengl-sw","--no-system-d3d-compiler","--dir",stage,stage/"NimboDesktop.exe"],check=True)
        shutil.copy2(ROOT/".tools/media/mediapipe/libmediapipe.dll",stage/"libmediapipe.dll")
        for pattern in ["livekit*.dll","msvcp*.dll","vcruntime*.dll","concrt*.dll","vccorlib*.dll","dxcompiler.dll","dxil.dll"]:
            for library in runtime.glob(pattern): shutil.copy2(library,stage)
        shutil.copy2(ROOT/"scripts/update-desktop.ps1",stage)
        assets(args.web,stage/"assets")
        shutil.copytree(ROOT/"desktop/player",stage/"assets/player")
        shutil.copytree(ROOT/"licenses",stage/"licenses")
        sdk = ROOT/".tools/webview2-1.0.3650.58"
        for name in ("LICENSE", "NOTICE"):
            shutil.copy2(sdk/(name+".txt"),stage/"licenses"/("WebView2-SDK-"+name+".txt"))
        shutil.copy2(args.web/"public/licenses/iconsax.txt",stage/"licenses/iconsax.txt")
        shutil.copy2(args.web/"src/assets/fonts/LICENSE.txt",stage/"licenses/Roboto.txt")
        shutil.copy2(ROOT/"LICENSE",stage)
        shutil.copy2(ROOT/"desktop/THIRD_PARTY.md",stage)
        for module in ("qtbase", "qtmultimedia"):
            shutil.copy2(ROOT/f".tools/qt-msvc/sbom/{module}-6.8.3.spdx.json",stage/"licenses/qt")
        binaries = sorted(p for p in stage.rglob("*") if p.suffix.lower() in (".exe", ".dll"))
        with (stage/"licenses/windows-binaries.sha256").open("w", encoding="utf-8", newline="\n") as inventory:
            for binary in binaries:
                with binary.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                inventory.write(f"{digest}  {binary.relative_to(stage).as_posix()}\n")
        out=dist/f"Nimbo-{VERSION}-Setup.exe"
        build_installer(args.nsis,work,stage,out)
        # The ZIP also permits a clean extracted-runtime test without an existing installation.
        shutil.make_archive(str(dist/f"Nimbo-Desktop-{VERSION}-portable"),"zip",stage)
        portable=dist/f"Nimbo-Desktop-{VERSION}-portable"
        # Some vendored license files are read-only, including their packaged copies.
        for source in stage.rglob("*"):
            target=portable/source.relative_to(stage)
            if target.is_file() and target.stat().st_file_attributes & stat.FILE_ATTRIBUTE_READONLY:
                target.chmod(stat.S_IWRITE | stat.S_IREAD)
        shutil.copytree(stage,portable,dirs_exist_ok=True)
        archive=dist/f"Nimbo-Desktop-{VERSION}-portable.zip"
        with archive.open("rb") as zipped:
            digest=hashlib.file_digest(zipped,"sha256").hexdigest()
        manifest={"schema":1,"platform":"windows-x64","version":VERSION,"size":archive.stat().st_size,"sha256":digest}
        (dist/"windows-x64.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
        print(out)


if __name__=="__main__":
    main()
