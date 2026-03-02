# ⚠️ **AI ALERT**

As a software engineer with limited C++ expertise, I directed the development of Qt‑Caster by iteratively prompting AI language models (Claude and DeepSeek) to generate the majority of the code. Through systematic debugging, precise requirements, and continuous validation of the AI's output, I guided the project from concept to a fully functional streaming application. This process demonstrates my ability to leverage AI tools effectively while retaining full ownership of problem‑solving and architectural decisions.

# 🎬 Qt-Caster Stream Manager

**Qt-Caster** is a cross-platform GUI application for streaming your screen (video) and microphone/audio input (audio) over UDP using **GStreamer** and **PipeWire** (Linux) / platform‑specific capture APIs. It supports **custom stream names**, **hardware acceleration** (VAAPI, NVENC, QSV), **software fallback**, and **persistent pinned streams** that automatically start on launch.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Platform](https://img.shields.io/badge/platform-Linux%20|%20macOS%20|%20Windows-lightgrey)](https://github.com/yourusername/qt-caster)

---

## 📸 Screenshots

### Main screen

![alt text](./images/main-screen.png)

### Video stream creation

![alt text](./images/video-tream-creator.png)

### Audio stream creation

![alt text](/images/audio-stream-creator.png)

## ✨ Features

- **🎥 Video Streaming** – Capture any screen/window via PipeWire (Linux) with configurable:
  - Resolution, bitrate, framerate (via encoder)
  - Encoder: `x264` (software), `VAAPI H.264`, `NVENC H.264`, `QSV H.264`
  - Hardware acceleration with automatic fallback to software
  - x264 tuning (`zerolatency`, `film`, …), speed presets, profile
- **🎤 Audio Streaming** – Capture any PipeWire audio source (mic, loopback) with:
  - Codec: `Opus` or `AAC`
  - Bitrate, channels (1–8), sample rate
  - Opus application (`audio`, `voip`, `lowdelay`)
- **📛 Custom Stream Names** – Give your streams human‑readable names; otherwise auto‑named (`qt-caster-video-1`, …)
- **📌 Pin & Save Streams** – Check the **Pin** column to save a stream permanently. Pinned streams are saved to `~/.config/qt-caster/saved-streams.json` and restart automatically when the app launches.
- **🖥️ Live Stream Table** – View all active streams with status, destination, options, and live activity indicator (🟢/🔴)
- **🖱️ System Tray Integration** – Minimize to tray, right‑click to manage streams, click to show window
- **🧹 Clean Shutdown** – Gracefully terminates all GStreamer processes on quit (Ctrl+C, tray quit, window close)
- **📦 Portable AppImage (Linux)** – Single‑file executable with **zero runtime dependencies** (bundles Qt, GStreamer, all plugins)

---

## 🖥️ Supported Platforms

| Platform           | Status             | Notes                                           |
| ------------------ | ------------------ | ----------------------------------------------- |
| **Linux** (x86_64) | ✅ Fully supported | PipeWire required; AppImage available           |
| **macOS**          | 🚧 In progress     | Requires GStreamer, CoreAudio, ScreenCaptureKit |
| **Windows**        | 🚧 Planned         | DirectShow / WGC, DirectSound                   |

---

## 🔧 Requirements (for developers)

### Linux

- **Qt6** (`Core`, `Widgets`, `Multimedia`)
- **GStreamer** 1.0+ with plugins:
  - `gst-plugins-base`
  - `gst-plugins-good` (for `pipewiresrc`, `udpsink`)
  - `gst-plugins-bad` (for `vaapih264enc`, `nvh264enc`, `qsvh264enc` – optional)
  - `gst-plugins-ugly` (for `x264enc`)
  - `gst-libav` (for `avenc_aac`)
- **PipeWire** (≥ 0.3.40) with development headers
- **CMake** ≥ 3.16
- **C++17** compiler

**Install on Arch Linux:**

```bash
sudo pacman -S qt6-base qt6-multimedia gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav pipewire cmake base-devel
```

**Install on Ubuntu/Debian:**

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  libpipewire-0.3-dev cmake build-essential
```

**Install on Fedora:**

```bash
sudo dnf install qt6-qtbase-devel qt6-qtmultimedia-devel gstreamer1-devel gstreamer1-plugins-base-devel \
  gstreamer1-plugins-good gstreamer1-plugins-bad-free gstreamer1-plugins-ugly-free \
  gstreamer1-libav pipewire-devel cmake gcc-c++
```

### macOS

- Qt6 (via Homebrew or official installer)
- GStreamer (from gstreamer.freedesktop.org)
- CMake

### Windows

- Qt6 (MSVC 2019/2022)
- GStreamer (MinGW or MSVC runtime)
- CMake

## 🛠️ Build from Source

### 1. Clone the repository

```bash
git clone https://github.com/Khyretos/qt-screen-caster.git
cd qt-screen-caster
```

### 2. Configure with CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
```

### 3. Build

```bash
make -j$(nproc) # Linux/macOS
cmake --build . --config Release # Windows
```

After successful build, you will have two executables:

`qt-caster` – Main GUI application

`qt-caster-worker` – Worker process (spawned by the GUI)

## 📦 Packaging – Create a Portable AppImage (Linux)

Qt-Caster can be bundled into a single, dependency‑free AppImage using linuxdeploy and its plugins.

### 1- Download linuxdeploy and plugins:

```bash
wget https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
wget https://github.com/linuxdeploy/linuxdeploy-plugin-gstreamer/releases/download/continuous/linuxdeploy-plugin-gstreamer-x86_64.AppImage
chmod +x \*.AppImage
```

### 2- Run the deployment script:

```bash
./package.sh
```

The script creates `Qt-Caster-x86_64.AppImage` in the project root.

**Note:** The AppImage bundles **everything** – Qt, GStreamer, and all required plugins. It runs on any Linux distribution **without** installing GStreamer or Qt.

## 🚀 Usage

### Starting the Application

```bash
./qt-caster # from build directory
./Qt-Caster-x86_64.AppImage # portable version
```

### Creating a Stream

1. Click 🎥 Create Video Stream or 🎤 Create Audio Stream.
2. Configure the stream parameters.
3. (Optional) Enter a custom name.
4. Click ✅ Start.
5. If streaming a screen, the PipeWire portal will appear – select a screen/window.

### Managing Streams

- Double‑click a row → stops and removes that stream.
- Pin checkbox → saves the stream configuration; it will auto‑start next time.
- System Tray → right‑click for quick actions; hover for stream list.

### Stopping the Application

- Click ⏹ Quit Application – all streams are terminated gracefully.
- Press Ctrl+C in the terminal – same clean shutdown.
- Click the window close button (X) – minimizes to tray (unless quitting).

## 📁 Configuration & Saved Streams

Pinned streams are stored in:

- Linux: ~/.config/qt-caster/saved-streams.json
- macOS: ~/Library/Application Support/Qt-Caster/saved-streams.json
- Windows: %APPDATA%\Qt-Caster\saved-streams.json

The file is plain JSON. You can edit it manually, but it’s easier to use the Pin checkbox in the UI.

## 🐛 Troubleshooting

| Problem                                                           | Solution                                                                                                                                          |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Video stream fails with “stream error: no more input formats”** | Ensure `stream-properties` is not in the video pipeline (it is removed in the working version). Use the pipeline provided in `stream_worker.cpp`. |
| **Hardware encoder not available**                                | Install system drivers (VAAPI, NVIDIA, Intel QSV). The app automatically falls back to `x264`.                                                    |
| **AppImage doesn’t start**                                        | Ensure it’s executable (`chmod +x`). If on an older distribution, try building with an older GLIBC.                                               |
| **No system tray icon**                                           | Some desktop environments (e.g., GNOME) do not support system trays. The app still works; use the window directly.                                |
| **PipeWire portal does not appear**                               | Install `xdg-desktop-portal` and `xdg-desktop-portal-gtk` (or `-kde`). Restart session.                                                           |

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository.
2. Create a feature branch (git checkout -b feature/amazing-feature).
3. Commit your changes (git commit -m 'Add amazing feature').
4. Push to the branch (git push origin feature/amazing-feature).
5. Open a Pull Request.

### Coding style:

- Use 4 spaces for indentation.
- Follow Qt naming conventions (camelCase for methods, PascalCase for classes).
- Include emoji log messages (see existing code) for clarity.

## 📄 License

This project is licensed under the GNU General Public License v3.0 – see the [LICENSE](./LICENSE) file for details.

## 🙏 Acknowledgments

- [GStreamer](https://gstreamer.freedesktop.org/) – multimedia framework
- [Qt Project](https://www.qt.io/) – cross‑platform GUI toolkit
- [PipeWire](https://pipewire.org/) – low‑latency audio/video server
- [linuxdeploy](https://github.com/linuxdeploy) – AppImage packaging tools
