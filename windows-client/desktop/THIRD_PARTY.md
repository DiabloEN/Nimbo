# Third-party components

The MIT license in LICENSE applies to Nimbo's own source. The libraries in the
Windows distribution retain their respective licenses and copyright notices.

- **Cisco OpenH264 2.6.0**. The Windows client builds the outgoing software
  encoder from source and links it into the executable. Its copyright notice
  and BSD license are in `licenses/livekit/LICENSE-openh264.txt`.
  [Upstream source](https://github.com/cisco/openh264/tree/v2.6.0).

- **Qt 6.8.3**: dynamically linked Core, Gui, Widgets, Network, Sql and Multimedia
  with the plugins selected by windeployqt. Qt is used under LGPLv3; applicable
  license texts are in `licenses/qt`. Users may replace the shared Qt libraries
  with compatible modified builds. Nimbo does not restrict reverse engineering
  for debugging modifications to these LGPL components.
  Source: <https://download.qt.io/archive/qt/6.8/6.8.3/single/>
  and <https://code.qt.io/cgit/qt/> (v6.8.3 tags).
  Qt's component and third-party notices: <https://doc.qt.io/qt-6.8/licenses-used-in-qt.html>.
- **FFmpeg libraries supplied with Qt Multimedia**: LGPLv2.1 or later for the
  LGPL configuration supplied by Qt. License text is in `licenses/ffmpeg`.
  Upstream source and notices: <https://ffmpeg.org/download.html>,
  <https://ffmpeg.org/legal.html>. Qt's corresponding source/build configuration:
  <https://code.qt.io/cgit/qt/qtmultimedia.git/tree/src/3rdparty?h=v6.8.3>.
- **GCC 13.1 / MinGW-w64 runtime**: libstdc++, libgcc and winpthreads.
  GCC GPLv3 with the GCC Runtime Library Exception, and the applicable
  MinGW-w64/winpthreads licenses. Copies are in `licenses/mingw`.
  <https://gcc.gnu.org/releases.html>, <https://www.mingw-w64.org/>.
- **SQLite**: the Qt SQLite plugin includes SQLite, which is in the public
  domain. <https://sqlite.org/copyright.html>.

The development toolchain is installed separately in `.tools` and is not part
of the portable application. Qt/MinGW download locations and pinned archive
checksums are recorded in `scripts/toolchain-manifest.json` in the source tree.
The Windows x64 media build uses MSVC.
It includes Microsoft Visual C++ x64 redistributable runtime DLLs from
Microsoft.VC143.CRT; those files retain Microsoft's license, not Nimbo's MIT
license. Development toolchains are never included in the client archive.

Media components:
- LiveKit C++ SDK 1.12.3 (Apache-2.0), commit 80130a2, and its bundled
  livekit_ffi 0.12.82 / Rust SDK commit a6f93a16. Sources:
  https://github.com/livekit/client-sdk-cpp/tree/v1.12.3 and
  https://github.com/livekit/rust-sdks/tree/a6f93a16.
  License copies are in licenses/livekit. Release hashes are pinned in
  scripts/bootstrap-media.ps1. The FFI uses the Windows x64 WebRTC build
  `webrtc-89d790b`. The SDK notices and the WebRTC distribution's combined
  third-party license file are in `licenses/livekit/`. The package includes
  a list of all shipped executables and DLLs with their SHA-256 hashes in
  `licenses/windows-binaries.sha256`.
- OpenH264 is the encoder reported by the tested LiveKit binary. Its BSD
  license is included in licenses/livekit/LICENSE-openh264.txt.
  https://github.com/cisco/openh264.
- LiveKit server 1.13.10 (Apache-2.0) is in the separate local-server archive,
  with its LICENSE. https://github.com/livekit/livekit/tree/v1.13.10.

No Krisp binary is included. The native desktop preview also compiles the original RNNoise and GTCRN modules to machine code using WABT 1.0.39 (Apache-2.0) and SIMDe 0.8.2. Windows camera segmentation uses the MediaPipe Tasks C runtime 0.10.32 (Apache-2.0) and the original web selfie model. Qt MSVC archive hashes are recorded
in scripts/qt-msvc-manifest.json.

- Web UI: Roboto, self-hosted Latin/Cyrillic weights 400/500/700 from @fontsource/roboto. License retained at web/src/assets/fonts/LICENSE.txt (SIL Open Font License 1.1).

## Camera background effects

- `@livekit/track-processors` 0.8.2 (Apache-2.0), with `@mediapipe/tasks-vision` 0.10.14 (Apache-2.0). Dependencies and license notices are included in the generated `assets/licenses.md`.
- MediaPipe WASM runtime files are copied from the pinned package into `web/public/effects/wasm` for same-origin hosting. Model: Google MediaPipe selfie_segmenter float16 version 1, https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter/float16/1/selfie_segmenter.tflite . No third-party CDN requests are needed at runtime.
- Upstream integration reference: https://github.com/livekit/track-processors-js/blob/main/processor-docs/video-processors.md . The effect executes on the client and is loaded only on demand.

## Experimental microphone noise suppression (client-side)

- `@sapphi-red/web-noise-suppressor` 0.4.1, MIT, https://github.com/sapphi-red/web-noise-suppressor . Pinned in package-lock.json; generated assets/licenses.md includes its license.
- Its RNNoise WASM/worklet uses `@shiguredo/rnnoise-wasm` 2022.2.0 (Apache-2.0), https://github.com/shiguredo/rnnoise-wasm, based on Xiph RNNoise (BSD-3-Clause), https://github.com/xiph/rnnoise. Corresponding notices are retained under web/public/licenses/rnnoise/.
- The GTCRN option loads `@sapphi-red/gtcrn-wasm` 0.0.3 from the same pinned wrapper. The wrapper and GTCRN weights use MIT. The bundled worklet retains the model and pffft/FFTPACK notices. [WASM implementation](https://github.com/sapphi-red/gtcrn-wasm) · [Model and benchmarks](https://github.com/Xiaobin-Rong/gtcrn)
- Same-origin assets only; microphone samples are processed locally. Build with VITE_AUDIO_LAB=0 to omit the local noise engines.

The server uses the Go Authors’ HTML tokenizer from golang.org/x/net under the BSD 3-Clause License. [License text](licenses/go-net.txt).
