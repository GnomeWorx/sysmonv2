# SysmonV2 — Steampunk Analog Dial System Monitor
## Complete Product Engineering Specification (PEC) for All Dials, Gauges & Board

**Version:** 2.0.0  
**Date:** 2026-07-11  
**Author:** GnomeWorx  
**Target:** Linux (Qt5/C++17, SteamGauge custom widget)

---

## Table of Contents
1. [Architecture Overview](#1-architecture-overview)
2. [SteamGauge Widget Specification (Core Engine)](#2-steamgauge-widget-specification-core-engine)
3. [Board Layout & Dashboard Specification](#3-board-layout--dashboard-specification)
4. [Gauge Specifications (All 14 Dials)](#4-gauge-specifications-all-14-dials)
   - [4.1 CPU Gauge — Usage (%)](#41-cpu-gauge--usage-)
   - [4.2 CPU Temp Gauge — Temperature (°C)](#42-cpu-temp-gauge--temperature-°c)
   - [4.3 RAM Gauge — Used GB](#43-ram-gauge--used-gb)
   - [4.4 Chassis Temp Gauge — Temperature (°C)](#44-chassis-temp-gauge--temperature-°c)
   - [4.5 M780 PERF Gauge — iGPU Usage (%)](#45-m780-perf-gauge--igpu-usage-)
   - [4.6 M780 TEMP Gauge — iGPU Temperature (°C)](#46-m780-temp-gauge--igpu-temperature-°c)
   - [4.7 M780 VRAM Gauge — VRAM Used (GB)](#47-m780-vram-gauge--vram-used-gb)
   - [4.8 NVMe Temp Gauge — Temperature (°C)](#48-nvme-temp-gauge--temperature-°c)
   - [4.9 WAN Gauge — Internet Speed (Mbps)](#49-wan-gauge--internet-speed-mbps)
   - [4.10 LAN Gauge — Local Network Speed (Mbps)](#410-lan-gauge--local-network-speed-mbps)
   - [4.11 DIMM A Temp Gauge — Temperature (°C)](#411-dimm-a-temp-gauge--temperature-°c)
   - [4.12 DIMM B Temp Gauge — Temperature (°C)](#412-dimm-b-temp-gauge--temperature-°c)
   - [4.13 Clock Gauge — 3-Hand Analog (HMS)](#413-clock-gauge--3-hand-analog-hms)
   - [4.14 NVIDIA GPU Gauge — RTX 4060 Ti Usage + Temp](#414-nvidia-gpu-gauge--rtx-4060-ti-usage--temp)
   - [4.15 NVIDIA TPS Gauge — Tokens Per Second](#415-nvidia-tps-gauge--tokens-per-second)
5. [Data Sources & Collection Logic](#5-data-sources--collection-logic)
6. [Visual Style & Colour Palette](#6-visual-style--colour-palette)
7. [Animation & Interaction Specification](#7-animation--interaction-specification)
8. [Build & Deployment](#8-build--deployment)
9. [Revision History](#9-revision-history)

---

## 1. Architecture Overview

### 1.1 High-Level Structure
```
┌─────────────────────────────────────────────────────────────────────────┐
│ SystemMonitorV2 (QMainWindow)                                           │
│ ┌─────────────────────────────────────────────────────────────────────┐ │
│ │ WoodPanelWidget (central widget) — paints oak veneer background     │ │
│ │ ┌─────────────────────────────────────────────────────────────────┐ │ │
│ │ │ VBoxLayout (6px margins, 6px spacing)                           │ │ │
│ │ │ ┌─────────────────────────────────────────────────────────────┐ │ │ │
│ │ │ │ Title Bar — "THE CHRONOMETRIC ENGINE MONITOR" (brass plate) │ │ │ │
│ │ │ └─────────────────────────────────────────────────────────────┘ │ │ │
│ │ │ ┌─────────────────────────────────────────────────────────────┐ │ │ │
│ │ │ │ Rivet Row (12 brass rivets across top)                      │ │ │ │
│ │ │ └─────────────────────────────────────────────────────────────┘ │ │ │
│ │ │ ┌─────────────────────────────────────────────────────────────┐ │ │ │
│ │ │ │ Clock Row (HBoxLayout)                                      │ │ │ │
│ │ │ │ ┌──────────────┐ ┌────────────────────────────────────────┐ │ │ │ │
│ │ │ │ │ CLOCK GAUGE  │ │ CALENDAR WIDGET (QCalendarWidget)      │ │ │ │ │
│ │ │ │ │ (220px high) │ │ (180px min width, steampunk styled)    │ │ │ │ │
│ │ │ │ └──────────────┘ └────────────────────────────────────────┘ │ │ │ │
│ │ │ └─────────────────────────────────────────────────────────────┘ │ │ │
│ │ │ ┌─────────────────────────────────────────────────────────────┐ │ │ │
│ │ │ │ Gauge Grid (QGridLayout 3×4, 5px spacing, equal stretch)   │ │ │ │
│ │ │ │ Row 0: [CPU] [CPU TEMP] [RAM] [CHASSIS]                     │ │ │ │
│ │ │ │ Row 1: [M780 PERF] [M780 TEMP] [M780 VRAM] [NVME TEMP]      │ │ │ │
│ │ │ │ Row 2: [WAN] [LAN] [DIMM A] [DIMM B]                        │ │ │ │
│ │ │ └─────────────────────────────────────────────────────────────┘ │ │ │
│ │ │ ┌─────────────────────────────────────────────────────────────┐ │ │ │
│ │ │ │ NVIDIA Row (HBoxLayout, full width)                         │ │ │ │
│ │ │ │ [NVIDIA RTX 4060 Ti GAUGE] [NVIDIA TPS GAUGE]               │ │ │ │
│ │ │ └─────────────────────────────────────────────────────────────┘ │ │ │
│ │ │ ┌─────────────────────────────────────────────────────────────┐ │ │ │
│ │ │ │ Bottom Rivet Row (12 brass rivets)                          │ │ │ │
│ │ │ └─────────────────────────────────────────────────────────────┘ │ │ │
│ │ └─────────────────────────────────────────────────────────────────┘ │ │
│ └─────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Window & Sizing
| Property | Value |
|----------|-------|
| Minimum Size | 1200 × 1100 px |
| Default Size | 1400 × 1200 px |
| Window Title | "Chronometric Engine Monitor" |
| Background | Oak veneer wood texture + dark stain overlay (#0f0803, α=200) |
| Fullscreen | F11 toggle |

### 1.3 Timer & Update Loop
| Timer | Interval | Handler | Purpose |
|-------|----------|---------|---------|
| `m_tickTimer` | 250 ms | `tick()` | All sensor reads + gauge updates + clock sweep |

---

## 2. SteamGauge Widget Specification (Core Engine)

### 2.1 Class Definition
```cpp
class SteamGauge : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double animatedValue READ animatedValue WRITE setAnimatedValue)
```

### 2.2 Constructor Parameters
```cpp
SteamGauge(
    const QString &title,       // Gauge label (e.g., "CPU", "RAM")
    const QString &unit,        // Unit suffix (e.g., "%", "°C", "GB", "HMS")
    double minValue = 0.0,      // Scale minimum
    double maxValue = 100.0,    // Scale maximum
    double redThreshold = 80.0, // Red-zone start (top 1/5 of range by default)
    QWidget *parent = nullptr
)
```

### 2.3 Public API
| Method | Purpose |
|--------|---------|
| `setValue(double)` | Primary value → animates needle |
| `setSecondaryValue(double)` | Second needle (e.g., temp on CPU gauge) |
| `setTertiaryValue(double)` | Third needle (e.g., hour hand on clock) |
| `setSubtitle(QString)` | Bottom text (e.g., "42%  68°C") |
| `setArc(double degStart, double degSpan)` | Override arc angles |
| `setAnimDuration(int ms)` | Needle animation speed (0 = instant) |
| `setNeedleBaseWidth(double ratio)` | Needle base width as fraction of gauge width |
| `setBezelColor(QColor)` | Override brass bezel colour |
| `setNeedleColor(QColor)` | Override primary needle colour |

### 2.4 Visual Layers (Paint Order — Back to Front)
1. **Outer drop shadow** (radial gradient, 60% opacity at centre)
2. **Bezel ring** — brass (default) or custom colour, centered radial gradient
3. **Inner chamfer bevel** — parchment→dark edge gradient
4. **Dial face** — parchment radial gradient + concentric machined rings
5. **Red zone arc** — translucent red band on outer track (top 1/5 of scale)
6. **Tick marks** — major (10) + minor (50), engraved colour (#2a1f14)
7. **Value labels** — at major ticks, 7pt engraved colour
8. **Title** — gold text (#d4a843), 10pt bold, letter-spacing 1.5, at 28% down face
9. **Unit label** — 7pt, muted gold, beneath title (hidden on clock)
10. **Subtitle** — 9pt monospace, bright warm white (#ffe6a0), at 72% down face, with drop shadow
11. **Primary needle** — crimson (#dc1e1e), tapered triangle, gradient 3D, drop shadow
12. **Secondary needle** — amber (#c86414), 65% length (if enabled)
13. **Tertiary needle** — gold (#b48c3c), 50% length (if enabled)
14. **Centre hub** — brass radial gradient + centre screw
15. **Glass overlay** — top-left crescent highlight + rim highlight
16. **Corner rivets** — 4 brass dome rivets at gauge corners (drawn by gauge itself)

### 2.5 Needle Animation
- **Property animation** on `animatedValue` (QPropertyAnimation)
- **Default duration:** 400 ms, `OutCubic` easing
- **Instant mode:** `setAnimDuration(0)` → snaps immediately, no shake
- **Red-zone shake:** When value enters red zone (≥ `redThreshold`):
  - Emits `enteredRedZone()`
  - 600 ms jitter: 30 ms timer, 5-pattern alternating offsets (±2px, ±1.5px)
  - Intensity fades linearly over shake duration
  - Continuous while in red zone (re-triggers after pause)
  - Emits `exitedRedZone()` on exit

### 2.6 Clock Bezel Variant (`unit == "HMS"`)
- **Bezel:** Silver brushed metal (centered radial gradient, no directional banding)
- **Dial face:** Darker enamel (warm gray radial gradient)
- **Arc:** 360° (degStart=90, degSpan=360) — 12 at top
- **Ticks:** 12 hour marks with numerals (1–12), longer inner extension
- **No red zone** on clock
- **Three needles:**
  - Primary (seconds): Crimson, full length, continuous sweep
  - Secondary (minutes): Amber, 65% length, continuous sweep
  - Tertiary (hours): Gold, 50% length, continuous sweep
- **Animation duration:** 0 ms (instant, driven by `tick()` directly)

### 2.7 Constants
| Constant | Value | Description |
|----------|-------|-------------|
| `DEG_START` | 135° | Default arc start (bottom-left) |
| `DEG_SPAN` | 270° | Default arc span (to bottom-right) |
| `RING_WIDTH_RATIO` | 0.10 | Bezel width = 10% of gauge width |
| `TICK_LONG_RATIO` | 0.035 | Major tick length |
| `TICK_SHORT_RATIO` | 0.020 | Minor tick length |
| `NEEDLE_LEN_RATIO` | 0.70 | Primary needle length |
| `NEEDLE_WIDTH` | 2.5 px | Needle base width (absolute) |
| Min gauge size | 120 × 140 px | `setMinimumSize()` |

### 2.8 Colour Constants (SteamGauge.cpp)
| Name | Hex | RGB | Use |
|------|-----|-----|-----|
| `COLOR_BRASS_LIGHT` | `#d4a843` | 212,168,67 | Brass highlight |
| `COLOR_BRASS_MID` | `#b8860b` | 184,134,11 | Brass mid-tone |
| `COLOR_BRASS_DARK` | `#8b5a00` | 139,90,0 | Brass shadow |
| `COLOR_CRIMSON` | `#8b0000` | 139,0,0 | Red zone accent |
| `COLOR_NEEDLE` | `#dc1e1e` | 220,30,30 | Primary needle |
| `COLOR_PARCHMENT` | `#f5e6c8` | 245,230,200 | Dial face base |
| `COLOR_RED_ZONE` | `#b42828` α=60 | 180,40,40,60 | Red zone arc |
| `COLOR_WOOD` | `#1a1410` | 26,20,16 | Panel background |
| `COLOR_RIVET` | `#c0a060` | 192,160,96 | Rivet domes |
| `COLOR_GOLD_TEXT` | `#d4a843` | 212,168,67 | Title text |
| `COLOR_ENGRAVED` | `#2a1f14` | 42,31,20 | Tick marks, labels |

---

## 3. Board Layout & Dashboard Specification

### 3.1 Grid Coordinates
```
Row 0 (stretch=1):  [CPU]      [CPU TEMP]  [RAM]    [CHASSIS]
Row 1 (stretch=1):  [M780 PERF][M780 TEMP] [M780 VRAM][NVME TEMP]
Row 2 (stretch=1):  [WAN]      [LAN]       [DIMM A] [DIMM B]

Row 3 (NVIDIA row): [NVIDIA GPU GAUGE] [NVIDIA TPS GAUGE]  (HBoxLayout, equal stretch)
```

### 3.2 Gauge Configuration Table
| Gauge | Title | Unit | Min | Max | Red Thresh | Bezel | Needle | Fixed Height |
|-------|-------|------|-----|-----|------------|-------|--------|--------------|
| CPU | "CPU" | "%" | 0 | 100 | 80 | Brass | Crimson | 220 |
| CPU TEMP | "CPU TEMP" | "°C" | 0 | 100 | 80 | Brass | Crimson | 220 |
| RAM | "RAM" | "GB" | 0 | 64 | 51.2 | Brass | Crimson | 220 |
| CHASSIS | "CHASSIS" | "°C" | 0 | 50 | 40 | Brass | Crimson | 220 |
| M780 PERF | "M780 PERF" | "%" | 0 | 100 | 80 | Brass | Crimson | 220 |
| M780 TEMP | "M780 TEMP" | "°C" | 0 | 100 | 80 | Brass | Crimson | 220 |
| M780 VRAM | "M780 VRAM" | "GB" | 0 | 2 | 1.6 | Brass | Crimson | 220 |
| NVME TEMP | "NVME TEMP" | "°C" | 0 | 100 | 80 | Brass | Crimson | 220 |
| WAN | "WAN" | "Mbps" | 0 | 200 | 160 | Blue (#1e64c8) | Blue (#50a0ff) | 220 |
| LAN | "LAN" | "Mbps" | 0 | 1000 | 800 | Blue (#1e64c8) | Blue (#50a0ff) | 220 |
| DIMM A | "DIMM A" | "°C" | 0 | 85 | 68 | Brass | Crimson | 220 |
| DIMM B | "DIMM B" | "°C" | 0 | 85 | 68 | Brass | Crimson | 220 |
| CLOCK | "CLOCK" | "HMS" | 0 | 60 | N/A | Silver | Crimson/Amber/Gold | 220 |
| NVIDIA GPU | "NVIDIA GEFORCE RTX 4060 Ti" | "%" | 0 | 100 | 80 | Green (#76b900) | White | 220 |
| NVIDIA TPS | "NVIDIA TPS" | "tps" | 0 | 1000 | 800 | Green (#76b900) | White | 220 |

### 3.3 Title Bar
- **Text:** "THE CHRONOMETRIC ENGINE MONITOR"
- **Font:** 12pt, bold, small caps, letter-spacing 5px
- **Colour:** `#e8c860` (warm gold)
- **Background:** Brass plate gradient + 1px `#d4a843` border + drop shadow
- **Rivets:** 12 brass rivets below title bar

### 3.4 Calendar Widget
- **Position:** Right of clock gauge (clock row)
- **Min width:** 180px
- **Style:** Dark brass (`#2a1208` bg, `#555` border, `#e8c860` headers, `#c8a050` days)
- **Today highlight:** `#b8860b` bg, `#2a1208` text, bold
- **Navigation bar:** `#b8860b` bg, `#2a1208` text

### 3.5 Bottom Rivet Row
- Identical to top rivet row (12 rivets)

---

## 4. Gauge Specifications (All 14 Dials)

### 4.1 CPU Gauge — Usage (%)
| Property | Specification |
|----------|---------------|
| **Title** | "CPU" |
| **Unit** | "%" |
| **Range** | 0 – 100 |
| **Red Zone** | ≥ 80% (top 20%) |
| **Data Source** | `/proc/stat` (first `cpu` line) |
| **Formula** | `usage% = (1 - Δidle/Δtotal) × 100`<br>`total = user + nice + system + idle + iowait + irq + softirq`<br>`idle = idle + iowait` |
| **Update Rate** | 250 ms (per tick) |
| **Needle** | Primary: crimson, animated (400 ms) |
| **Secondary** | None (CPU temp on separate gauge) |
| **Subtitle Format** | `"XX%"` (integer) |
| **Bezel** | Default brass |
| **Red Zone Shake** | Yes (600 ms jitter on entry, continuous while ≥80%) |

**Code Reference:** `SystemMonitorV2::readCPU()` (lines 450-473)

---

### 4.2 CPU Temp Gauge — Temperature (°C)
| Property | Specification |
|----------|---------------|
| **Title** | "CPU TEMP" |
| **Unit** | "°C" |
| **Range** | 0 – 100 |
| **Red Zone** | ≥ 80°C |
| **Data Source** | `/sys/class/hwmon/hwmon<N>/temp1_input` where `name == "k10temp"` |
| **Formula** | `temp°C = raw_millidegrees / 1000.0` |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX°C"` (integer) |
| **Bezel** | Default brass |

**Code Reference:** `SystemMonitorV2::readSensors()` (lines 519-524)

---

### 4.3 RAM Gauge — Used GB
| Property | Specification |
|----------|---------------|
| **Title** | "RAM" |
| **Unit** | "GB" |
| **Range** | 0 – 64 (hardcoded for 64 GB system) |
| **Red Zone** | ≥ 51.2 GB (80% of 64) |
| **Data Source** | `/proc/meminfo` → `MemTotal`, `MemAvailable` |
| **Formula** | `used_GB = (MemTotal_kB - MemAvailable_kB) / 1,048,576`<br>`MemTotal` captured once at startup |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"X.X / 64 GB"` (1 decimal) |
| **Bezel** | Default brass |

**Code Reference:** `SystemMonitorV2::readRAM()` (lines 493-507), constructor captures `m_ramTotalGB`

---

### 4.4 Chassis Temp Gauge — Temperature (°C)
| Property | Specification |
|----------|---------------|
| **Title** | "CHASSIS" |
| **Unit** | "°C" |
| **Range** | 0 – 50 |
| **Red Zone** | ≥ 40°C (80% of range) |
| **Data Source** | `/sys/class/hwmon/hwmon<N>/temp1_input` where `name == "acpitz"` |
| **Formula** | `temp°C = raw / 1000.0` |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX°C"` |
| **Bezel** | Default brass |

**Code Reference:** `SystemMonitorV2::readSensors()` (lines 540-546)

---

### 4.5 M780 PERF Gauge — iGPU Usage (%)
| Property | Specification |
|----------|---------------|
| **Title** | "M780 PERF" |
| **Unit** | "%" |
| **Range** | 0 – 100 |
| **Red Zone** | ≥ 80% |
| **Data Source** | `/sys/class/drm/card<N>/device/gpu_busy_percent` (AMD Radeon 780M) |
| **Formula** | Direct read (0–100 integer) |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX%"` |
| **Bezel** | Default brass |
| **Fallback** | If file missing → 0% |

**Code Reference:** `SystemMonitorV2::readNvidia()` — *Note: Despite function name, reads AMD iGPU busy percent*

---

### 4.6 M780 TEMP Gauge — iGPU Temperature (°C)
| Property | Specification |
|----------|---------------|
| **Title** | "M780 TEMP" |
| **Unit** | "°C" |
| **Range** | 0 – 100 |
| **Red Zone** | ≥ 80°C |
| **Data Source** | `/sys/class/hwmon/hwmon<N>/temp1_input` where `name == "amdgpu"` |
| **Formula** | `temp°C = raw / 1000.0` |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX°C"` |
| **Bezel** | Default brass |

**Code Reference:** `SystemMonitorV2::readSensors()` (lines 533-538)

---

### 4.7 M780 VRAM Gauge — VRAM Used (GB)
| Property | Specification |
|----------|---------------|
| **Title** | "M780 VRAM" |
| **Unit** | "GB" |
| **Range** | 0 – 2 (2 GB shared VRAM for 780M) |
| **Red Zone** | ≥ 1.6 GB (80% of 2 GB) |
| **Data Source** | `/sys/class/drm/card<N>/device/mem_info_vram_total` + `mem_info_vram_used`<br>Plus `mem_info_gtt_total` + `mem_info_gtt_used` |
| **Formula** | `vram_GB = (vram_used + gtt_used) / 1,073,741,824` (bytes → GB) |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"X.X GB"` (1 decimal) |
| **Bezel** | Default brass |

**Code Reference:** `SystemMonitorV2::readNvidia()` (AMD VRAM section)

---

### 4.8 NVMe Temp Gauge — Temperature (°C)
| Property | Specification |
|----------|---------------|
| **Title** | "NVME TEMP" |
| **Unit** | "°C" |
| **Range** | 0 – 100 |
| **Red Zone** | ≥ 80°C |
| **Data Source** | `/sys/class/hwmon/hwmon<N>/temp1_input` where `name == "nvme"` |
| **Formula** | `temp°C = raw / 1000.0` |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX°C"` |
| **Bezel** | Default brass |

**Code Reference:** `SystemMonitorV2::readSensors()` (lines 526-531)

---

### 4.9 WAN Gauge — Internet Speed (Mbps)
| Property | Specification |
|----------|---------------|
| **Title** | "WAN" |
| **Unit** | "Mbps" |
| **Range** | 0 – 200 (scales for typical internet) |
| **Red Zone** | ≥ 160 Mbps |
| **Data Source** | `ss -i -t -n` (TCP connection stats) |
| **Classification** | **WAN** = peer IP is **public** (not in private ranges) |
| **Private Ranges** | `10.0.0.0/8`, `127.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `::1/128`, `fe80::/10`, `fc00::/7`, `fd00::/7` |
| **Formula (per tick)** | `dRx = curr_rx - prev_rx`<br>`dTx = curr_tx - prev_tx`<br>`speed_Mbps = (bytes × 8 / 1,000,000) / 0.25` (250 ms interval) |
| **Cumulative** | `m_cumWanRx += dRx`, `m_cumWanTx += dTx` (unsigned long long) |
| **Update Rate** | 250 ms |
| **Needles** | Primary (↓ down): blue `#50a0ff`, animated<br>Secondary (↑ up): blue `#50a0ff`, animated via `setSecondaryValue()` |
| **Subtitle Format** | `"↓ X.XX  ↑ Y.YY"` (2 dp if <10, else 1 dp) |
| **Bezel** | Custom blue `#1e64c8` |
| **Needle Colour** | Custom blue `#50a0ff` |

**Code Reference:** `SystemMonitorV2::readNetwork()` (lines 582-655)

---

### 4.10 LAN Gauge — Local Network Speed (Mbps)
| Property | Specification |
|----------|---------------|
| **Title** | "LAN" |
| **Unit** | "Mbps" |
| **Range** | 0 – 1000 (1 Gbps LAN typical) |
| **Red Zone** | ≥ 800 Mbps |
| **Data Source** | `ss -i -t -n` (same as WAN) |
| **Classification** | **LAN** = peer IP is **private** (see ranges above) |
| **Formula** | Identical to WAN |
| **Cumulative** | `m_cumLanRx`, `m_cumLanTx` |
| **Update Rate** | 250 ms |
| **Needles** | Primary (↓): blue `#50a0ff`<br>Secondary (↑): blue `#50a0ff` |
| **Subtitle Format** | `"↓ X.XX  ↑ Y.YY"` |
| **Bezel** | Custom blue `#1e64c8` |
| **Needle Colour** | Custom blue `#50a0ff` |

**Code Reference:** `SystemMonitorV2::readNetwork()` (lines 582-655)

---

### 4.11 DIMM A Temp Gauge — Temperature (°C)
| Property | Specification |
|----------|---------------|
| **Title** | "DIMM A" |
| **Unit** | "°C" |
| **Range** | 0 – 85 |
| **Red Zone** | ≥ 68°C (80% of 85) |
| **Data Source** | `/sys/class/hwmon/hwmon<N>/temp1_input` where `name == "spd5118"` (first occurrence) |
| **Formula** | `temp°C = raw / 1000.0` |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX°C"` |
| **Bezel** | Default brass |
| **Identification** | First `spd5118` hwmon found = DIMM A |

**Code Reference:** `SystemMonitorV2::readSensors()` (lines 556-578)

---

### 4.12 DIMM B Temp Gauge — Temperature (°C)
| Property | Specification |
|----------|---------------|
| **Title** | "DIMM B" |
| **Unit** | "°C" |
| **Range** | 0 – 85 |
| **Red Zone** | ≥ 68°C |
| **Data Source** | `/sys/class/hwmon/hwmon<N>/temp1_input` where `name == "spd5118"` (second occurrence) |
| **Formula** | `temp°C = raw / 1000.0` |
| **Update Rate** | 250 ms |
| **Needle** | Primary: crimson, animated |
| **Subtitle Format** | `"XX°C"` |
| **Bezel** | Default brass |
| **Identification** | Second `spd5118` hwmon found = DIMM B |

**Code Reference:** `SystemMonitorV2::readSensors()` (lines 556-578)

---

### 4.13 Clock Gauge — 3-Hand Analog (HMS)
| Property | Specification |
|----------|---------------|
| **Title** | "CLOCK" |
| **Unit** | "HMS" (special marker) |
| **Range** | 0 – 60 (maps 0–59 sec, 0–59 min, 0–11 hr×5) |
| **Red Zone** | None (clock) |
| **Data Source** | `QTime::currentTime()` |
| **Formulas** | `sec = second + msec/1000.0` (0–60 continuous)<br>`min = minute + sec/60.0` (0–60 continuous)<br>`hour = (hour % 12) * 5.0 + min/12.0` (0–60, maps 12h→60) |
| **Needles** | **Primary (sec):** Crimson, 100% length, continuous sweep, instant (anim=0)<br>**Secondary (min):** Amber `#c86414`, 65% length<br>**Tertiary (hr):** Gold `#b48c3c`, 50% length |
| **Bezel** | Silver (custom `drawClockBezel()`) |
| **Ticks** | 12 hour numerals (1–12) at 30° intervals, 12 at top (90°) |
| **Arc** | `setArc(90, 360)` — full circle, 12 at top |
| **Subtitle** | Empty (time shown by hands) |
| **Update Rate** | 250 ms (driven by `tick()`) |

**Code Reference:** `SystemMonitorV2::tick()` (lines 434-440), `SteamGauge::drawClockBezel()`, `drawTickMarks()` (HMS branch)

---

### 4.14 NVIDIA GPU Gauge — RTX 4060 Ti Usage + Temp
| Property | Specification |
|----------|---------------|
| **Title** | "NVIDIA GEFORCE RTX 4060 Ti" |
| **Unit** | "%" |
| **Range** | 0 – 100 |
| **Red Zone** | ≥ 80% |
| **Data Source** | `nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader,nounits` |
| **Formula** | Direct parse: `usage%`, `temp°C` |
| **Update Rate** | 250 ms (blocking `QProcess`, 3 s timeout) |
| **Needle** | Primary: **White** (`#ffffff`), animated (400 ms) |
| **Subtitle Format** | `"XX% / YY°C"` (usage + temp combined) |
| **Bezel** | Custom green `#76b900` (NVIDIA brand) |
| **Needle Colour** | Custom white `#ffffff` |

**Code Reference:** `SystemMonitorV2::readNvidia()` (lines 476-490), `tick()` updates (lines 377-382)

---

### 4.15 NVIDIA TPS Gauge — Tokens Per Second
| Property | Specification |
|----------|---------------|
| **Title** | "NVIDIA TPS" |
| **Unit** | "tps" |
| **Range** | 0 – 1000 |
| **Red Zone** | ≥ 800 |
| **Data Source** | `/tmp/agent_pikey_stats.txt` (placeholder) |
| **Formula** | File contains single number: tokens/second |
| **Update Rate** | 250 ms |
| **Needle** | Primary: **White** (`#ffffff`), animated |
| **Subtitle Format** | `"X.X tokens/s"` (1 decimal) |
| **Bezel** | Custom green `#76b900` |
| **Needle Colour** | Custom white `#ffffff` |
| **Status** | Placeholder — reads 0 if file missing |

**Code Reference:** `SystemMonitorV2::readAgentPikeyStats()` (lines 658-676), `tick()` updates (lines 446-447)

---

## 5. Data Sources & Collection Logic

### 5.1 `/proc/stat` — CPU Usage
```cpp
// Read first line starting with "cpu "
// Fields: user nice system idle iowait irq softirq ...
total = user + nice + system + idle + iowait + irq + softirq
idleTotal = idle + iowait
usage% = (1.0 - (idle - prevIdle) / (total - prevTotal)) * 100.0
```

### 5.2 `/proc/meminfo` — RAM
```cpp
// Startup: read MemTotal once → m_ramTotalGB
// Per tick: read MemAvailable
used_GB = (MemTotal_kB - MemAvailable_kB) / 1048576.0
```

### 5.3 `/sys/class/hwmon/hwmon<N>/` — Temperatures
| Sensor | hwmon `name` | File | Scale |
|--------|--------------|------|-------|
| CPU (k10temp) | `k10temp` | `temp1_input` | /1000 |
| NVMe | `nvme` | `temp1_input` | /1000 |
| iGPU (amdgpu) | `amdgpu` | `temp1_input` | /1000 |
| Chassis (acpitz) | `acpitz` | `temp1_input` | /1000 |
| Ethernet (r8169) | `r8169*` | `temp1_input` | /1000 |
| DIMM A/B | `spd5118` (×2) | `temp1_input` | /1000 |

Scan `hwmon0`–`hwmon9` each tick.

### 5.4 `/sys/class/drm/card<N>/device/` — AMD iGPU (Radeon 780M)
| Metric | File |
|--------|------|
| Usage % | `gpu_busy_percent` |
| VRAM Total | `mem_info_vram_total` |
| VRAM Used | `mem_info_vram_used` |
| GTT Total | `mem_info_gtt_total` |
| GTT Used | `mem_info_gtt_used` |

VRAM GB = `(vram_used + gtt_used) / 1024^3`

### 5.5 `ss -i -t -n` — Network (TCP connections)
- Parse `ESTAB` lines + following info line
- Extract `bytes_received:N` and `bytes_sent:N`
- Key = `localAddr-peerAddr`
- Delta per tick → classify by peer IP (private vs public)
- Speed Mbps = `delta_bytes * 8 / 1e6 / 0.25`

### 5.6 `nvidia-smi` — NVIDIA GPU
```bash
nvidia-smi --query-gpu=utilization.gpu,temperature.gpu --format=csv,noheader,nounits
```
Output: `XX, YY` → usage%, temp°C

### 5.7 `/tmp/agent_pikey_stats.txt` — TPS (Placeholder)
Single float: tokens per second. Currently returns 0.

---

## 6. Visual Style & Colour Palette

### 6.1 Wood Panel Background
- **Texture:** `/home/sfarrant/oak_veneer_16x9_4k.jpg` (tiled)
- **Overlay:** `rgba(15, 8, 3, 200)` — heavy dark stain
- **Fallback:** Solid `#1e0e05` if texture missing

### 6.2 Title Bar Brass Plate
```css
background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
  stop:0 #8b6914, stop:0.3 #b8860b, stop:0.6 #a07010,
  stop:0.85 #6b4e0a, stop:1 #3d2a06);
border: 1px solid #d4a843;
border-radius: 4px;
```
- **Drop shadow:** 8px blur, offset (2,2), `rgba(0,0,0,120)`
- **Text shadow:** 2px blur, offset (1,1), `rgba(0,0,0,100)`

### 6.3 Rivets (Top & Bottom Rows)
```css
background: qradialgradient(cx:0.35,cy:0.35,radius:0.5,
  fx:0.35,fy:0.35,
  stop:0 #f0d080, stop:0.4 #c8a050,
  stop:0.7 #8a6520, stop:1 #4a3510);
border: 1px solid #3d2a06;
border-radius: 5px;
```
- Size: 10×10 px, spaced 4 px apart, 12 per row

### 6.4 Gauge Colour Assignments
| Gauge Group | Bezel | Primary Needle | Secondary Needle |
|-------------|-------|----------------|------------------|
| CPU, RAM, Temps (CPU, NVMe, Chassis, DIMMs) | Brass | Crimson `#dc1e1e` | Amber `#c86414` (if used) |
| M780 (iGPU) | Brass | Crimson | Amber |
| WAN, LAN | Blue `#1e64c8` | Blue `#50a0ff` | Blue `#50a0ff` |
| Clock | Silver (custom) | Crimson (sec) | Amber (min) / Gold (hr) |
| NVIDIA GPU, TPS | Green `#76b900` | White `#ffffff` | — |

### 6.5 Calendar Styling
```css
QCalendarWidget { background: #2a1208; border: 1px solid #555; }
QCalendarWidget::weekday-header { background: #40301a; color: #e8c860; }
QCalendarWidget::day-number { color: #c8a050; }
QCalendarWidget::current-date { background: #b8860b; color: #2a1208; font-weight: bold; }
QCalendarWidget::navigation-bar { background: #b8860b; }
QCalendarWidget::button { background: #b8860b; color: #2a1208; }
```

---

## 7. Animation & Interaction Specification

### 7.1 Needle Animation
- **All gauges except clock:** 400 ms `OutCubic` easing
- **Clock:** 0 ms (instant) — driven directly by `tick()` math
- **Red zone shake:** 600 ms, 30 ms frames, 5-pattern jitter, fades out

### 7.2 Clock Sweep (Continuous, Not Ticking)
```cpp
double sec = now.second() + now.msec() / 1000.0;      // 0–60 smooth
double min = now.minute() + sec / 60.0;               // 0–60 smooth
double hour = (now.hour() % 12) * 5.0 + min / 12.0;  // 0–60 (maps 12h→60)
m_clockGauge->setValue(sec);           // Primary (seconds)
m_clockGauge->setSecondaryValue(min);  // Secondary (minutes)
m_clockGauge->setTertiaryValue(hour);  // Tertiary (hours)
```

### 7.3 Fullscreen Toggle
- **Key:** F11
- **Action:** Toggle `showFullScreen()` / `showNormal()`

### 7.4 Window Persistence
- No `QSettings` persistence currently implemented
- Fixed default size 1400×1200, minimum 1200×1100

---

## 8. Build & Deployment

### 8.1 CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.12)
project(sysmonv2 VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

set(SOURCES
    main.cpp
    SystemMonitorV2.cpp
    SystemMonitorV2.h
    SteamGauge.cpp
    SteamGauge.h
)

add_executable(sysmonv2 ${SOURCES})
target_link_libraries(sysmonv2 Qt5::Widgets Qt5::Network)
```

### 8.2 Dependencies
| Dependency | Purpose |
|------------|---------|
| Qt5 Widgets | GUI framework |
| Qt5 Network | (Unused currently, but linked) |
| `nvidia-smi` | NVIDIA GPU stats (runtime) |
| `ss` (iproute2) | Network connection stats (runtime) |
| `/sys/class/hwmon/`, `/sys/class/drm/`, `/proc/` | Kernel interfaces (runtime) |

### 8.3 Build Commands
```bash
cd /home/sfarrant/sysmonv2
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./sysmonv2
```

### 8.4 Required Assets
- `/home/sfarrant/oak_veneer_16x9_4k.jpg` — wood texture (must exist for full effect)

---

## 9. Revision History

| Date | Version | Author | Changes |
|------|---------|--------|---------|
| 2026-07-11 | 2.0.0 | GnomeWorx | Initial comprehensive PEC for sysmonv2 — all 14 gauges, SteamGauge engine, board layout, data sources, visual spec |

---

## Appendix A: Gauge Quick Reference Card

| # | Gauge | Title | Unit | Range | Red @ | Source |
|---|-------|-------|------|-------|-------|--------|
| 1 | CPU | CPU | % | 0-100 | 80 | /proc/stat |
| 2 | CPU Temp | CPU TEMP | °C | 0-100 | 80 | hwmon:k10temp |
| 3 | RAM | RAM | GB | 0-64 | 51.2 | /proc/meminfo |
| 4 | Chassis | CHASSIS | °C | 0-50 | 40 | hwmon:acpitz |
| 5 | M780 Perf | M780 PERF | % | 0-100 | 80 | drm:gpu_busy_percent |
| 6 | M780 Temp | M780 TEMP | °C | 0-100 | 80 | hwmon:amdgpu |
| 7 | M780 VRAM | M780 VRAM | GB | 0-2 | 1.6 | drm:mem_info_vram+gtt |
| 8 | NVMe | NVME TEMP | °C | 0-100 | 80 | hwmon:nvme |
| 9 | WAN | WAN | Mbps | 0-200 | 160 | ss -i -t -n (public IP) |
| 10 | LAN | LAN | Mbps | 0-1000 | 800 | ss -i -t -n (private IP) |
| 11 | DIMM A | DIMM A | °C | 0-85 | 68 | hwmon:spd5118 (1st) |
| 12 | DIMM B | DIMM B | °C | 0-85 | 68 | hwmon:spd5118 (2nd) |
| 13 | Clock | CLOCK | HMS | 0-60 | — | QTime::currentTime() |
| 14 | NVIDIA GPU | NVIDIA GEFORCE RTX 4060 Ti | % | 0-100 | 80 | nvidia-smi |
| 15 | NVIDIA TPS | NVIDIA TPS | tps | 0-1000 | 800 | /tmp/agent_pikey_stats.txt |

---

**End of Specification**