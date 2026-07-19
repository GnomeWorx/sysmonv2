# SysmonV2 — Steampunk System Monitor

**Version:** 2.0.0  
**Author:** GnomeWorx (c) 2026  
**Stack:** C++17, Qt 5.15 (Widgets, Network, Test), CMake 3.16+  
**Display:** X11 (XCB), offscreen-capable for CI  
**Target:** Linux x86_64 with `/proc` filesystem

---

## 1. Overview

SysmonV2 is a fullscreen/desktop system monitor that displays real-time system metrics (CPU, RAM, GPU temperature, network throughput, disk I/O, process count, uptime) as **steampunk-style analog gauges** with brass rings, parchment dials, and riveted panels. It also includes a **12-hour analogue clock** with second/minute/hour hands.

The window is styled to look like a brass instrument panel on dark wood. It has two modes:

- **Attached** (default): the full panel is shown — title bar (pinned or scrollable), clock row, gauge grid, and copyright footer.
- **Detached**: each gauge pops out into its own frameless floating window — usable as a small HUD widget.

All telemetry is read from `/proc/stat`, `/proc/meminfo`, `/proc/net/dev`, `/sys/class/hwmon/*/temp*_input`, `/proc/diskstats`, `/proc/loadavg`, and `/proc/uptime`.

---

## 2. Quick-Start Build

```bash
cd ~/sysmonv2
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Build produces five binaries:

| Binary | Description |
|---|---|
| `sysmonv2` | Main application |
| `test_netmon` | Network parser unit tests (headless) |
| `test_steamgauge` | SteamGauge widget tests (offscreen) |
| `test_sysmonv2` | UI integration tests (offscreen) |

Run tests:
```bash
cd ~/sysmonv2/build
ctest --output-on-failure
```

Dependencies:
- `qtbase5-dev`, `libqt5network5` (Ubuntu/Debian)
- `g++` with C++17 support

---

## 3. Architecture

### 3.1 Source File Map

```
sysmonv2/
├── CMakeLists.txt              # Root build — app + tests
├── Version.h.in                # CMake-configured version template
├── Specification.md            ← YOU ARE HERE
├── main.cpp                    # Application entry point
├── SystemMonitorV2.h           # Main window class declaration
├── SystemMonitorV2.cpp         # Main window implementation (~500 lines)
├── SteamGauge.h                # Steampunk gauge widget declaration
├── SteamGauge.cpp              # Steampunk gauge widget implementation (~670 lines)
├── netmon_parser.h             # /proc/net/dev parser (free function)
├── netmon_parser.cpp           # Parser implementation (~23 lines)
└── tests/
    ├── CMakeLists.txt          # Test build — 3 executables
    ├── test_netmon.cpp         # 12 test cases for netmon_parser
    ├── test_steamgauge.cpp     # 23+ test cases for SteamGauge
    └── test_sysmonv2_ui.cpp    # 15 test cases for SystemMonitorV2 integration
```

### 3.2 Class Hierarchy

```
QApplication (main.cpp)
  └── SystemMonitorV2 : QWidget          # Main panel window
        ├── titleLabel : QLabel           # Steampunk brass title
        ├── subtitleLabel : QLabel        # Version/copyright
        ├── copyrightLabel : QLabel       # Footer
        ├── titleWidget : QWidget         # Container for title + version info
        ├── clockRow : QWidget            # Holds the clock gauge
        ├── gaugeGrid : QWidget           # Grid of 6+ gauges (2 cols × 3+ rows)
        ├── rowCpu, rowGpu, rowNet, rowDisk, rowSys : QWidget  # Row containers
        ├── detachBtn : QPushButton       # DETACH/ATTACH toggle
        ├── m_gauges : QVector<SteamGauge*>  # All gauges
        ├── m_cpuLoad, m_gpuTemp, ...     # Per-gauge pointers
        ├── m_cpuPrevIdle, m_cpuPrevTotal  # Delta accumulators
        ├── m_netPrev{Map}                # Network counters per iface
        ├── m_diskPrev{Map}               # Disk counters per device
        ├── m_timer : QTimer              # Main update tick (250ms)
        ├── m_detached : bool             # Mode flag
        └── m_detachedWindows : QVector<QWidget*>  # Floating gauge windows

      SteamGauge : QWidget (N instances)  # Individual gauge widget
        ├── m_title : QString             # Gauge name ("CPU LOAD")
        ├── m_unit : QString              # Unit label ("%", "°C", "Mbps")
        ├── m_subtitle : QString          # Live value text ("42%")
        ├── m_minValue, m_maxValue : double
        ├── m_redThreshold : double       # Value ≥ this = red zone
        ├── m_value : double              # Clamped current value
        ├── m_animatedValue : double      # QPropertyAnimation target
        ├── m_secondaryValue : double     # Second needle value (-1 = disabled)
        ├── m_tertiaryValue : double      # Third needle value (-1 = disabled)
        ├── m_anim : QPropertyAnimation   # Needle sweep (400ms, OutCubic)
        ├── m_shakeTimer : QTimer         # (Legacy — red-zone shake removed)
        ├── m_wasInRed : bool             # (Legacy — always false now)
        ├── m_cacheDirty : bool           # Resize invalidation flag
        ├── m_degStart, m_degSpan : double  # Arc geometry (135°, 270°)
        ├── m_needleBaseWidth : double    # Base width ratio (0.05)
        ├── m_bezelColor : QColor         # Custom bezel (invalid = brass)
        └── m_needleColor : QColor         # Custom needle (invalid = crimson)
```

### 3.3 Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│ QTimer::tick()  (every 250ms)                                   │
│   │                                                             │
│   ├─ readCPU()       → /proc/stat          → cpuLoad gauge     │
│   ├─ readRAM()       → /proc/meminfo       → ramUsed, ramAvail  │
│   ├─ readGPUTemp()   → /sys/class/hwmon    → gpuTemp gauge     │
│   ├─ readNetwork()   → /proc/net/dev + Δt  → wanMbps, lanMbps  │
│   ├─ readDiskIO()    → /proc/diskstats + Δt→ diskRead, diskWrite│
│   ├─ readUptime()    → /proc/uptime        → uptime gauge      │
│   ├─ readProcs()     → /proc/loadavg       → procs gauge        │
│   └─ updateClock()   → QTime::currentTime()→ clock gauge       │
│                                                               │
│   For each gauge: gauge->setValue(newVal)                      │
│                  gauge->setSubtitle(formattedString)           │
│                                                               │
│   Detached mode: update floating gauge windows too             │
└─────────────────────────────────────────────────────────────────┘

CPU delta calculation:
  idle = fields[4] (iowait excluded from modern Linux idle)
  total = fields[1]+[2]+[3]+[4]+[5]+... (all fields summed)
  diffIdle = idle - prevIdle
  diffTotal = total - prevTotal
  usage% = 100 * (1 - diffIdle/diffTotal)   [0% when diffTotal==0]

Network throughput:
  rxBytes(t), rxBytes(t-Δt) from /proc/net/dev per interface
  mbPerSec = (rxΔ - txΔ) / 1_000_000 / Δt
  wanMbps = min(mbPerSecToMbps(mbPerSec), 200)  # clamp to dial max
  LAN uses a separate dial for secondary interface (enp1s0)
```

---

## 4. Gauge Layout

### 4.1 Gauge Grid

```
┌─────────────────────────────────────────────────────┐
│  [CLOCK]                 Detached gauges grid?      │
├─────────────────────────────────────────────────────┤
│  CPU LOAD     │  SYSTEM RAM       │  LOCAL GPU TEMP │
│  [──–──–──]   │  [──–──–──]       │  [──–──–──]     │
│  "42%"        │  "32.0 / 64.0 GB" │  "68°C"         │
├───────────────┼───────────────────┼─────────────────┤
│  WAN THROUGHP │  LAN THROUGHPUT   │  SYSTEM UPTIME  │
│  [──–──–──]   │  [──–──–──]       │  [──–──–──]     │
│  "12.5 Mbps"  │  "0.8 Mbps"       │  "3d 14h 22m"   │
├───────────────┼───────────────────┼─────────────────┤
│  DISK READ    │  DISK WRITE       │  PROCESS COUNT  │
│  [──–──–──]   │  [──–──–──]       │  [──–──–──]     │
│  "45 MB/s"    │  "12 MB/s"        │  "342"          │
└─────────────────────────────────────────────────────┘
```

### 4.2 Gauge Specifications

| Gauge | Unit | Range | Red Threshold | Bezel | Needle |
|---|---|---|---|---|---|
| CLOCK | HMS | 0–12 | n/a | Silver | Red (sec), Amber (min), Gold (hr) |
| CPU LOAD | % | 0–100 | 80 | Brass (default) | Red |
| SYSTEM RAM | GB | 0–64 | 50 | Brass | Red |
| LOCAL GPU TEMP | °C | 0–120 | 90 | Brass (or NVIDIA green) | Red |
| WAN THROUGHPUT | Mbps | 0–200 | 160 | Brass | Red |
| LAN THROUGHPUT | Mbps | 0–200 | 160 | Brass | Red |
| SYSTEM UPTIME | days | 0–365 | n/a | Brass | Red |
| DISK READ | MB/s | 0–500 | 400 | Brass | Red |
| DISK WRITE | MB/s | 0–500 | 400 | Brass | Red |
| PROCESS COUNT | procs | 0–2000 | 1500 | Brass | Red |

Note: The clock has three needles (second hand = primary, minute hand = secondary, hour hand = tertiary). The clock uses `setArc(135, 270)` for the 12-hour arc (bottom-left to bottom-right) with 12 numerals at the rim.

---

## 5. SteamGauge Widget API

### 5.1 Public API

```cpp
// Constructor
SteamGauge(const QString &title,
           const QString &unit,
           double minValue = 0.0,
           double maxValue = 100.0,
           double redThreshold = 80.0,
           QWidget *parent = nullptr);

// Value — clamped to [min, max]
void setValue(double val);
double value() const;

// Needle animation
void setAnimDuration(int ms);  // 0 = instant snap, 400 = default
double animatedValue() const;  // Q_PROPERTY for QPropertyAnimation

// Multi-needle support
void setSecondaryValue(double val);  // amber needle, 65% length
void setTertiaryValue(double val);   // gold needle, 50% length

// Labels
void setSubtitle(const QString &text);  // live-value text on dial face

// Arc geometry
void setArc(double degStart, double degSpan);  // default: 135, 270

// Styling
void setBezelColor(const QColor &c);   // QColor() invalid = brass default
void setNeedleColor(const QColor &c);  // QColor() invalid = crimson default
void setNeedleBaseWidth(double ratio); // default 0.05 (fraction of gauge width)

// Red zone
bool isInRedZone() const;  // value >= redThreshold
```

### 5.2 Drawing Layers (bottom to top)

1. **Drop shadow** — radial gradient behind the gauge
2. **Brass/silver bezel ring** — directional radial with inner bevel chamfer
3. **Dial face** — parchment radial gradient with concentric metal rings
4. **Red danger zone** — translucent red arc in the top ~20% of the sweep
5. **Tick marks** — 10 major + 50 minor ticks with engraved-style int labels
6. **Title + subtitle** — gold engraved text on the dial face
7. **Needle(s)** — tapered triangle with gradient fill + dual drop shadows + hub/screw
8. **Glass overlay** — white crescent reflection at top + edge highlight
9. **Rivets** — 4 corner brass dots at panel edge

### 5.3 Key Implementation Details

- **Animation**: QPropertyAnimation on `animatedValue` property, duration 400ms, OutCubic easing. When `setAnimDuration(0)`, the needle snaps instantly with no animation.
- **Red zone shake**: Historically had a shake timer animation when entering red zone. This was removed as "annoying" — `m_wasInRed` is always set to false and the shake timer is stopped immediately on every `setValue()` call. The `enteredRedZone`/`exitedRedZone` signal declarations remain in the header for API compatibility but are **never emitted**.
- **Clock path**: When `m_unit == "HMS"`, the widget renders a silver bezel (instead of brass), an enamel dial face, and 12 hour numerals with tick marks. The three-needle system (second, minute, hour) is used.
- **Minimum size**: `setMinimumSize(120, 140)` with `QSizePolicy::Expanding`.

---

## 6. SystemMonitorV2 Main Window

### 6.1 Constructor Flow

1. `QWidget(nullptr)` — no parent, standalone window
2. Set window title: `"Chronometric Engine Monitor v2.0.0"`
3. Set window flags: frameless hint, stays-on-top hint
4. Set attribute `Qt::WA_TranslucentBackground` — the window background is transparent; the wood panel is painted in `paintEvent()`
5. **Read system info**: `/proc/meminfo` for total RAM (falls back to 64 GB if unavailable), network interface detection
6. **Create layout**: vertical layout with:
   - `titleWidget` (QLabel for "CHRONOMETRIC ENGINE MONITOR" + subtitle line)
   - `clockRow` (clock + spacer)
   - `gaugeGrid` (QGridLayout — 3 columns × 3 rows of gauges)
   - `copyrightLabel` ("(c) GnomeWorx 2026  Version 2.0.0")
7. **Create gauges**: 10 gauges per table in §4.2, with `setAnimDuration(250)` for smooth needle sweeps
8. **Create DETACH button**: top-right corner of title widget
9. **Connect timer**: `QTimer` at 250ms interval → `tick()`
10. **Load persistent state**: `QSettings("Hermes", "sysmonv2")` — restores `detached` flag and `windowGeometry`
11. **Set initial size**: `resize(1280, 800)` or restored geometry
12. **Apply stylesheet**:
    ```css
    background: transparent;  // WA_TranslucentBackground handles this
    QLabel#titleLabel { color: #d4a843; font-size: 28px; font-weight: bold; ... }
    QPushButton { background: #2a1f14; color: #d4a843; border: 1px solid #8b5a00; ... }
    ```

### 6.2 tick() Pipeline (called every 250ms)

```
void SystemMonitorV2::tick() {
    readCPU();        // parse /proc/stat → cpuLoad gauge
    readRAM();        // parse /proc/meminfo → ramUsed, ramAvail gauges
    readGPUTemp();    // scan /sys/class/hwmon → gpuTemp gauge
    readNetwork();    // parse /proc/net/dev → wanMbps, lanMbps gauges
    readDiskIO();     // parse /proc/diskstats → diskRead, diskWrite gauges
    readUptime();     // parse /proc/uptime → uptime gauge
    readProcs();      // parse /proc/loadavg → procs gauge
    updateClock();    // QTime::currentTime() → clock gauge

    // In detached mode: also update the floating gauge windows
    if (m_detached) {
        for (auto *w : m_detachedWindows)
            w->update();
    }
}
```

### 6.3 Data Source Parsing Details

**CPU** (`/proc/stat`):
```
cpu  user nice sys idle iowait irq softirq steal guest guest_nice
```
Parse fields [1]..[10], compute delta from previous read. If `diffTotal == 0`, usage stays at 0 (first tick guard).

**RAM** (`/proc/meminfo`):
```
MemTotal:       64582344 kB
MemFree:        3225600 kB
MemAvailable:   48215392 kB
...
```
Parse `MemTotal:` and `MemAvailable:` values. `usedGB = (total - avail) / 1048576.0`.

**GPU Temp** (`/sys/class/hwmon/hwmon*/temp*_input`):
- Scan all `hwmonN` directories for `temp*_input` files
- Read value in millidegrees Celsius → divide by 1000
- Match known GPU sensor names ("edge", "junction", or by hwmon label file)

**Network** (`/proc/net/dev`):
```
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo ...
    lo:    1000      10 ...
 wlp2s0: 12345678 ...
```
Use `parseProcNetDev()` free function. Store previous per-interface counters, compute delta over tick interval.

**Disk I/O** (`/proc/diskstats`):
```
major minor name rio rmerge rsect ruse wio wmerge wsect wuse running use aveq
```
Parse sector counts (fields [5] and [9] — sectors read/written). One sector = 512 bytes. Compute delta over interval.

**Uptime** (`/proc/uptime`):
```
123456.78  ...
```
First field = seconds since boot.

**Process count** (`/proc/loadavg`):
```
0.42 0.31 0.25 2/345 12345
```
Parse the "2/345" field — the number after `/` is total processes.

### 6.4 Detach/Attach Mode

When **detach** is toggled:

1. The main window hides all non-gauge rows (title, clock, copyright footer)
2. The gauge grid rows are hidden
3. Each gauge gets its own **frameless floating window** (`Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint`)
4. Each floating window contains a single gauge widget at small size (~180×200px)
5. The DETACH button text changes to "ATTACH"
6. When re-attaching, the floating windows close and the main panel is restored
7. The state is saved to `QSettings` and restored on next launch

### 6.5 Keyboard Shortcuts

| Key | Action |
|---|---|
| `F11` | Toggle fullscreen (one-shot, doesn't require a toggle function — just `if (isFullScreen()) showNormal(); else showFullScreen()`) |
| `F10` | Toggle detach mode (programmatically clicks the detach button) |
| `Escape` | Close window (if detached, re-attach first) |

### 6.6 State Persistence

Uses `QSettings("Hermes", "sysmonv2")` to persist across sessions:

- **`windowGeometry`**: `saveGeometry()` / `restoreGeometry()` — window position and size
- **`detached`**: boolean — whether the window was in detached mode when closed
- **Save triggered**: on `closeEvent()` and whenever detach/attach is toggled

### 6.7 Rendering

The window has a **dark wood panel background** painted in `paintEvent()`:
- Dark brown brushed wood texture: `QColor(26, 20, 16)` with subtle radial gradient
- A `QRadialGradient` creates a vignette effect (lighter in center, darker at edges)
- Small semi-transparent border: `QPen(QColor(60, 40, 15), 2)`

---

## 7. Network Parser (netmon_parser)

### 7.1 Free Functions

```cpp
// Parse /proc/net/dev text → per-interface {rxBytes, txBytes}
void parseProcNetDev(const QString &text,
                     QMap<QString, QPair<unsigned long long, unsigned long long>> &out);

// Convert MB/s to Mbps (multiply by 8)
double mbPerSecToMbps(double mbPerSec);
```

### 7.2 Parsing Rules

1. Split input on newlines, trim each line
2. Find first `:` on each line — everything before = interface name (trimmed), everything after = counter fields
3. Split counter part on whitespace (`\s+`) into parts array
4. Need at least 9 fields (or it's malformed)
5. `rx = parts[0].toULongLong()`, `tx = parts[8].toULongLong()`
6. Lines without a colon are skipped (header lines)
7. 64-bit unsigned values supported

### 7.3 Test Coverage (12 tests)

- All interfaces parsed
- Correct rx/tx columns read
- Header lines and malformed lines skipped
- MB/s → Mbps conversion
- End-to-end synthetic throughput calculation
- Tab / irregular spacing
- Zero counters
- Empty input
- Interface names with leading/trailing whitespace
- Edge case: double colon in malformed iface name
- 64-bit wrap values (near UINT64_MAX)

---

## 8. Build System

### 8.1 Root `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.16)
project(sysmonv2 VERSION 2.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

# Version header from template
configure_file(Version.h.in "${CMAKE_CURRENT_SOURCE_DIR}/Version.h")

add_executable(sysmonv2
    main.cpp
    SystemMonitorV2.cpp
    SteamGauge.cpp
    netmon_parser.cpp
)
target_link_libraries(sysmonv2 PRIVATE Qt5::Widgets Qt5::Network)

add_subdirectory(tests)
```

### 8.2 Test Build

See `tests/CMakeLists.txt` — three independent test executables, each with:
- `find_package(Qt5 REQUIRED COMPONENTS Test Widgets Network)`
- Their own source + required production sources
- `enable_testing()` + `add_test()` for CTest registration

**Running tests requires** either a display server (X11) or the offscreen platform:
```bash
QT_QPA_PLATFORM=offscreen ctest --output-on-failure
```

---

## 9. Known Limitations & Gotchas

1. **Red zone signals never fire**: The `enteredRedZone`/`exitedRedZone` signals are declared in `SteamGauge.h` but the implementation in `setValue()` always sets `m_wasInRed = false` and never emits. The shake timer code is a dead codepath. (This was intentional — the red-zone shake was removed as annoying.)
2. **GPU temp discovery is heuristic**: The code scans all `/sys/class/hwmon/hwmon*/temp*_input` files and picks the one matching "edge" or "junction" label. On multi-GPU systems or systems without AMD/nvidia kernel drivers loaded, this may return 0.
3. **Network interface detection happens once at startup**: The first `readNetwork()` call picks the "primary" (wifi/ethernet) and "secondary" (ethernet) interfaces from the parsed `/proc/net/dev` output. Changing network interfaces after startup won't be picked up.
4. **Detached mode window state is not individually positionable**: All detached windows share the same size; each is just a floating gauge widget.
5. **No Wayland support**: Uses `Qt::WA_TranslucentBackground` + custom painting which works reliably only on X11.
6. **Disk I/O uses 512-byte sectors**: The `/proc/diskstats` sector size is assumed to be 512 bytes (standard for most Linux block devices, but NVMe may report different units on some kernels).

---

## 10. Modification Guide for Another AI

### Adding a New Gauge

1. Add a new `SteamGauge*` member to `SystemMonitorV2.h` (e.g., `m_batteryPercent`)
2. Create the gauge in the constructor (choose appropriate min/max/redThreshold)
3. Add it to `gaugeGrid` at the right `QGridLayout` position
4. Create a `readBattery()` method that reads your data source
5. Call it from `tick()` in `SystemMonitorV2.cpp`
6. Add a test in `test_sysmonv2_ui.cpp`

### Changing Gauge Appearance

- **Bezel colour**: Call `gauge->setBezelColor(QColor(r, g, b))`. Invalid QColor = brass default.
- **Needle colour**: Call `gauge->setNeedleColor(QColor(r, g, b))`. Invalid = crimson default.
- **Arc sweep**: Call `gauge->setArc(startDeg, spanDeg)` for non-standard gauge arcs.
- **Animation speed**: Call `gauge->setAnimDuration(ms)`. 0 = instant, 250–400 = smooth.

### Adding a New Test

1. Add a new slot to the relevant `Test` class
2. Add the slot declaration in the `private slots:` section
3. Use `QCOMPARE`, `QVERIFY`, `QSKIP` macros
4. For UI tests: create a `SystemMonitorV2` (or `SteamGauge`), call methods, `QTest::qWait(ms)`, then assert
5. If your test touches `/proc` or `/sys`, provide synthetic data (see `fakeProcStat()`, `fakeMemInfo()` helpers)

---

## 11. Style Conventions

- **Naming**: `camelCase` for methods/vars, `m_` prefix for members, `k` prefix or `static constexpr` for constants
- **Formatting**: K&R braces, 4-space indent, ~100 column limit
- **Comments**: Doxygen-style `///` for public API, `// ── Section ──` separators
- **Includes**: Grouped: own header, Qt, stdlib, project headers. Alphabetical within groups
- **Dial colours**: All colour constants at the top of `SteamGauge.cpp` as `static const QColor`
- **Avoid**: `Q_OBJECT` in non-widget classes, raw `new`/`delete` (Qt parent ownership), `std::*` containers where Qt equivalents exist
