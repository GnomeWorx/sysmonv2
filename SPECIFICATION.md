# SysmonV2 — Steampunk Analog-Dial System Monitor

A real-time Linux system monitor with photo-realistic steampunk brass analog gauges. Built with Qt5 C++.

## Overview

SysmonV2 replaces conventional line charts with 12 custom-painted analog dial gauges on a full-width dark wood panel background. Each gauge features a polished brass bezel with centered radial shading, a parchment dial face with concentric machined rings, a tapered crimson needle with drop shadow, and a glass dome overlay. When any gauge enters the red zone (top 1/5 of range), the gauge shakes like a game-style warning indicator.

## The 12 Gauges (3 × 4 Grid)

| Gauge | Label | Unit | Range | Red At | Source |
|---|---|---|---|---|---|
| CPU usage | CPU | % | 0–100 | ≥80% | /proc/stat |
| GPU usage | GPU | % | 0–100 | ≥80% | amdgpu sysfs (placeholder) |
| RAM usage | RAM | GB | 0–64 | ≥51 GB | /proc/meminfo |
| CPU die temp | CPU TEMP | °C | 0–100 | ≥80°C | k10temp Tctl |
| WAN internet speed | WAN | Mbps | 0–100 | ≥80 Mbps | ss -tcp (public IPs) |
| LAN local speed | LAN | Mbps | 0–100 | ≥80 Mbps | ss -tcp (private IPs) |
| NVMe SSD temp | NVME | °C | 0–100 | ≥80°C | nvme hwmon Composite |
| iGPU temp | IGPU TEMP | °C | 0–100 | ≥80°C | amdgpu hwmon edge |
| Chassis ambient | CHASSIS | °C | 0–50 | ≥40°C | acpitz hwmon |
| DIMM slot A temp | DIMM A | °C | 0–85 | ≥68°C | spd5118 (DIMM A) |
| DIMM slot B temp | DIMM B | °C | 0–85 | ≥68°C | spd5118 (DIMM B) |
| Ethernet NIC temp | ETHERNET | °C | 0–100 | ≥80°C | r8169 hwmon |

## Gauge Visual Design

Each gauge is a custom `SteamGauge` QWidget painted from scratch:

- **Wood panel background**: Painted once by the main window's central widget as a continuous tiled oak veneer image with a heavy dark stain overlay, spanning the full width behind all gauges. Gauges use `WA_TranslucentBackground` so the wood shows through seamless between columns.
- **Brass bezel**: Centered radial gradient with even shading from light brass → dark edge. Outer drop shadow cast onto wood panel.
- **Dial face**: Centered radial gradient, bright centre → darker parchment edge. Three faint concentric rings for a machined appearance. Subtle bottom-rim shadow.
- **Tick marks**: 50 minor + 10 major ticks with engraved-style value labels at each major tick. Drawn as fine dark lines.
- **Red danger zone**: Semi-transparent red arc covering the top 1/5 of the range, drawn along the outer edge of the dial face.
- **Needle**: Tapered triangular shape with 3D gradient fill (lighter tip → darker base). Crimson primary, amber secondary (for dual-value gauges). Drop shadow casts down-right at +3px +3px from a fixed 45° top-left light.
- **Centre hub**: Brass dome with concentric gradient and a dark centre screw.
- **Glass overlay**: Semi-transparent white reflection crescent at top-left, with a subtle rim highlight.
- **Title label**: Drawn on the dial face at ~28% down, gold engraved-style lettering with the unit below in smaller type.
- **Digital value label**: Drawn on the dial face at ~72% down, white monospace with a dark shadow for readability.
- **Rivets**: Four small brass rivets at the four corners of the gauge bounding box.
- **Shake effect**: When value enters red zone, the gauge jitters with a multi-frame offset pattern, fading over 600ms. Re-triggers continuously while staying in the red.

## Data Sources

All data is read directly from Linux sysfs, /proc, and process execution — no external dependencies beyond Qt5:

- **CPU usage**: `/proc/stat` delta calculation (total - idle / total)
- **CPU temp**: `/sys/class/hwmon/hwmonX/temp1_input` where name = `k10temp`
- **RAM**: `/proc/meminfo` MemTotal - MemAvailable, converted to GiB
- **Network**: `ss -i -t -n` parsed for per-connection byte counters, classified as WAN (public IP) vs LAN (private IP)
- **NVMe temp**: hwmon name=`nvme`, temp1_input (Composite)
- **iGPU temp**: hwmon name=`amdgpu`, temp1_input (edge)
- **Chassis temp**: hwmon name=`acpitz`, temp1_input
- **DIMM temps**: hwmon name=`spd5118`, first instance = DIMM A, second = DIMM B
- **Ethernet temp**: hwmon name matches `r8169`, temp1_input
- **GPU usage**: Currently reads from amdgpu voltage/power (placeholder — full GPU usage needs NVML when eGPU attached)
|- **GPU usage**: Currently reads from amdgpu voltage/power (placeholder — full GPU usage needs NVML when eGPU attached). See HANDOVER_SYSMONV2.md for details on NVIDIA iGPU VRAM and M780 placeholder.

## Building
```bash
# Dependencies
sudo apt-get install build-essential cmake qt5-default qtbase5-dev libnvidia-compute-*.dev (for NVML)

# Build
cd sysmonv2/build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# Run
./sysmonv2
```

## API Extensions — SteamGauge.h public methods for custom gauges (see HANDOVER_SYSMONV2.md, clock section):

- `setArc(degStart, degSpan)` — set dial arc range and span. Clock gauge uses 270° start to ~189° span
- `setAnimDuration(ms)` — needle animation duration; use 0ms for instant sweep like clock second hand
- `setNeedleBaseWidth(ratio)` — needle base relative to dial width (clock = thin, approx ratio=4) or specify absolute e.g. 3mm by drawing with QTransform scale/rotate instead of standard tapered shape

## API Extensions — SteamGauge.h internal/public additions for clock gauge:

- `drawClockBezel()` — custom silver brushed-metal bezel (bevel gradient centered at top, then bottom highlight)
- `setTertiaryValue(double)` — hour hand value; draws as a shorter dark brass needle rotated independently from the minute/second needles. See SYSTEMMONITORV2.cpp: m_secondGauge->setTertiaryValue(m_clockHour).

## Project Files (4 modified in this commit):

### SystemMonitorV2.h
- Added public members:  `m_nvGpuUsage      // NVIDIA GPU util%   \n    std::string              m_gpuStatus;        // "OK"/"BUSY"... etc\n`
```

## Project Structure

```
sysmonv2/
├── CMakeLists.txt
├── Version.h.in
├── SteamGauge.h          # Steampunk dial widget header
├── SteamGauge.cpp        # Full QPainter-based dial rendering
├── SystemMonitorV2.h     # Main window header
├── SystemMonitorV2.cpp   # UI layout, data reading, tick loop
├── main.cpp              # Entry point
├── .gitignore
└── build/                # Build output (gitignored)
```

## Future / Planned

- Wire real GPU usage from NVML when RTX 4060 Ti eGPU is attached
- Cost plaque with live LLM API spend (via sysmon-cost.py)
- Startup launcher and .desktop file
- Steampunk gear tray icon
- Auto-ranging WAN/LAN gauges (currently fixed 0–100 Mbps)
