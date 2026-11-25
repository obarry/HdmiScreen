# HdmiScreen

A Windows 11 utility that displays **the input of a USB HDMI capture device full screen**. It turns a personal laptop into a second screen for another computer, for example a work laptop that accepts neither USB connections nor unapproved software:

```
Work laptop ──HDMI──▶ HDMI→USB capture device ──USB 3──▶ Personal laptop + HdmiScreen
```

To the work laptop, the capture device is just an ordinary HDMI monitor. On the personal laptop, HdmiScreen shows the received picture full screen, borderless, with minimal latency.

- **Native C++20**: Win32, Media Foundation, Direct3D 11. No external dependency.
- **A single `.exe`**, no installation.
- **No configuration** in the usual case: the capture device and the best mode for the screen are selected automatically.
- **Teaching-oriented** code and documentation, written for a developer coming from Java.

> The user interface and the detailed documentation in `docs/` are currently in French.

## Requirements

| Item | Details |
| --- | --- |
| System | Windows 10 (1703+) or Windows 11, 64-bit |
| Capture device | UVC-class HDMI → USB capture device (recognised by the Windows Camera app), plugged into a **USB 3** port |
| To build | Visual Studio 2022 with the "Desktop development with C++" workload (includes MSVC, the Windows SDK and CMake) |

## Building

**With Visual Studio 2022**: *File > Open > Folder…* and pick the project folder. Visual Studio detects `CMakeLists.txt`. Choose the `x64-Release` configuration, then the `hdmiscreen.exe` target, and run.

**From the command line** ("Developer PowerShell for VS 2022"):

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release        # tests of the selection logic
```

The executable is written to `build\Release\hdmiscreen.exe`.

> The executable is not code-signed. On a PC where Windows **Smart App Control** is enabled, it may be blocked ("an application control policy has blocked this file"); rebuilding usually produces an executable that is allowed to run.

## Usage

1. Plug the capture device into a USB 3 port of the personal laptop, and the HDMI cable from the work laptop into the capture device.
2. On the work laptop: Win+P > **Extend**, then Settings > Display. Select the new screen and choose the resolution of the captured mode (see `--list`): **1920×1200** if offered, otherwise **1920×1080**. The display scale (100 %, 125 %…) has no effect on capture: choose it for reading comfort.
3. On the personal laptop: run `hdmiscreen.exe`.

| Key | Action |
| --- | --- |
| Esc or Alt+F4 | Quit |
| F11 | Toggle full screen / window |
| S | Scaling: fit → 1:1 → stretch |
| R | Colour range: limited (16-235) ↔ full (0-255) |
| F | Upscaling filter: GPU smoothing → bicubic → Lanczos |
| N | Sharpening (bicubic and Lanczos filters): none → light → medium → strong |
| Right-click | Full menu, also in full screen |

### Menus and settings

In windowed mode (F11), a menu bar gives access to every setting. In full screen, the same content is available with a right-click on the picture.

| Menu (French label) | Content |
| --- | --- |
| File (*Fichier*) | Reconnect the capture device, Quit |
| Display (*Affichage*) | Full screen; scaling (fit, 1:1, stretch); colour range; upscaling filter and sharpening |
| Device (*Périphérique*) | Automatic, or a specific device (a webcam included) |
| Mode | Automatic (the selected mode is shown), or one of the capture device's resolutions |
| Help (*Aide*) | Keyboard shortcuts, About (version, location of the settings file) |

Every choice is **saved to `hdmiscreen.ini`** next to the executable and restored at the next launch. Command-line options override these settings for the session. The window title shows the current device, mode and upscaling filter.

The cursor is hidden after 2 s without movement, and the screen is kept awake while the tool runs.

**Disconnects.** If the capture device disappears for a moment (loose USB contact), the last frame stays on screen and the tool reconnects to the same device as soon as it reappears. After 3 s without any frame, a dark grey screen is shown. The laptop's webcam is never selected automatically; to use it anyway, name it with `--device`.

**Picture quality.** If text looks blurred while the capture works, the cause is usually the HDMI signal sent by the source computer, not the upscaling. Switching the source's refresh rate to 30 Hz and back to 60 Hz forces the HDMI link to renegotiate and has been seen to restore a sharp picture.

### Options

| Option | Effect |
| --- | --- |
| `--list` | Lists the video devices, their modes and the automatic choices, then exits |
| `--device "text"` | Uses the device whose name contains this text (even a webcam) |
| `--mode 1920x1200[@60]` | Forces a capture mode |
| `--scaling fit\|1to1\|stretch` | Initial scaling (default: `fit`) |
| `--upscale gpu\|bicubic\|lanczos` | Upscaling filter (default: `gpu`, the GPU video processor's smoothing) |
| `--sharpness 0..3` | Sharpening level for the bicubic and Lanczos filters (default: 1) |
| `--full-range` | The signal uses the full 0-255 range (try it if the picture looks too contrasted) |
| `--windowed` / `--fullscreen` | Starts in a window / full screen |
| `--help` | Help |

Recommended first run, from a command prompt:

```powershell
.\hdmiscreen.exe --list
```

## Project structure

```
HdmiScreen/
├─ CMakeLists.txt          build description
├─ src/
│  ├─ main.cpp             entry point
│  ├─ App.*                wiring, main loop, menus and commands, --list
│  ├─ Commands.h           command identifiers (menus, shortcuts)
│  ├─ SettingsFile.*       persistent settings (hdmiscreen.ini)
│  ├─ MainWindow.*         Win32 window, menu bar, context menu
│  ├─ CaptureDevices.*     device and mode discovery (Media Foundation)
│  ├─ CaptureSession.*     asynchronous capture (COM object)
│  ├─ Renderer.*           display (Direct3D 11): swap chain, video processor
│  ├─ ShaderScaler.*       shader-based upscaling (bicubic, Lanczos, sharpening)
│  ├─ ScalingShaders.h     HLSL source of the shaders
│  ├─ ComUtils.h, Log.*    shared utilities
│  └─ VideoMode.h, Selection.*, Geometry.h, CommandLine.*   pure, tested logic
├─ tests/SelectionTests.cpp
└─ docs/                   (in French)
   ├─ ARCHITECTURE.md      threads, frame path, mode selection, error handling
   ├─ API_WINDOWS.md       Win32, COM, Media Foundation, Direct3D 11 (and what it is), shaders
   └─ CPP_POUR_DEV_JAVA.md the project's C++ explained for Java developers
```

## Reading the code

Suggested reading order:

1. [docs/CPP_POUR_DEV_JAVA.md](docs/CPP_POUR_DEV_JAVA.md), at least sections 0 to 4;
2. `VideoMode.h`, `Selection.cpp` and the tests: plain C++, no Windows;
3. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), then `main.cpp` and `App.cpp`;
4. `MainWindow.cpp`, with [docs/API_WINDOWS.md](docs/API_WINDOWS.md) section 1;
5. `CaptureDevices.cpp` and `CaptureSession.cpp`, with API_WINDOWS sections 2 and 3;
6. `Renderer.cpp`, with API_WINDOWS sections 4 and 5;
7. `ShaderScaler.cpp` and `ScalingShaders.h`, with API_WINDOWS section 4.7.

Code comments (in French) are tagged:

| Tag | Meaning |
| --- | --- |
| `// [C++]` | C++ language point |
| `// [C++ vs Java]` | Notable difference from Java |
| `// [API]` | Windows API call or concept, with the name to look up in the Microsoft documentation |

## Roadmap

| Step | Content | Status |
| --- | --- | --- |
| 1 | Hardware validation (Windows Camera app) | Done |
| 2 | Prototype: mode listing, capture, full-screen display | Done (0.1) |
| 3 | Automatic mode selection, low latency | Done (0.1) |
| 4 | Comfort: scaling, cursor hiding, sleep prevention, reconnection, USB disconnect tolerance, timestamped log | Done (0.1) |
| 5a | Menu bar, context menu, `hdmiscreen.ini`, R key | Done (0.2) |
| 5s | Optional shader-based upscaling: bicubic, Lanczos, adjustable sharpening (F and N keys) | **0.3** (occasional flicker being investigated) |
| 5b | Status bar, connection history (Log window, `hdmiscreen.log`) | To do |
| 5c | F1 information overlay in full screen (Direct2D / DirectWrite) | To do |
| 6 | Recovery after GPU loss, icon, HDMI audio (optional) | To do |
