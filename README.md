# SysmonV2 — Steampunk System Monitor

Real-time hardware monitoring with brass-and-glass analog gauges. Written in Qt5/C++17.

![screenshot placeholder](screenshot.png)

## Features

- **16 analog gauges** — custom-painted radial dials with brass bezels, glass overlays, and illuminated needles
- **CPU, RAM, GPU, network, temperature** — all read from `/proc` and `/sys` in real time
- **Radeon 780M iGPU** — core %, temperature, VRAM usage (shared memory)
- **Wi-Fi & Ethernet** — per-interface throughput monitoring (Mbps)
- **LLM TPS** — live token-per-second reading from local LLM inference
- **Dynamic sensor discovery** — hwmon chips found by name, survives reboots
- **1:1 proportional resize** — all dials scale with the window

### Included Tools

#### 📟 USB Serial Monitor (`usb_monitor.py`)

PyQt5 + `pyudev` utility for detecting and monitoring USB serial devices:

- Lists all `ttyUSB*` / `ttyACM*` devices with human-readable names
- Hotplug detection via udev (with poll fallback)
- **Double-click any device** for full udev property drill-down
- Steampunk dark theme to match sysmonv2

```bash
pip install PyQt5 pyudev
python3 usb_monitor.py
```

#### 📊 netmon parser tests (`tests/test_netmon.cpp`)

Unit tests for the `/proc/net/dev` parser, built with Qt5 Test.

```bash
cmake --build build --target test_netmon && ./build/test_netmon
```

## Dependencies

| Dependency | Version | Purpose |
|---|---|---|
| Qt5 Widgets | ≥ 5.15 | UI framework |
| CMake | ≥ 3.20 | Build system |
| C++17 | `std::filesystem` | Sensor discovery |
| Vulkan | — | LLM GPU inference (optional) |

**Python tools:**
- Python ≥ 3.10
- PyQt5
- pyudev (optional, for hotplug)

## Build

```bash
cd sysmonv2
cmake -B build -G Ninja
cmake --build build
./build/sysmonv2
```

## Project Structure

```
sysmonv2/
├── CMakeLists.txt         # Build config (Qt5 AUTOMOC + Ninja)
├── main.cpp               # Entry point, auto-start TPS script
├── SystemMonitorV2.h/.cpp # Main window, sensor reads, UI layout
├── SteamGauge.h/.cpp      # Custom-painted analog dial widget
├── netmon_parser.h/.cpp   # /proc/net/dev parser
├── usb_monitor.py         # USB serial device monitor (Python)
├── tests/
│   ├── CMakeLists.txt
│   └── test_netmon.cpp    # Unit tests for network parser
└── screenshot.png
```

## Requirements

- Linux with `/proc` and `/sys` filesystems
- X11 display
- GPU: AMD Radeon 780M (iGPU) + optional NVIDIA RTX 4060 Ti (dedicated)

## License

MIT — see [LICENSE](LICENSE).
