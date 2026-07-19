#include "SystemMonitorV2.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QRegularExpression>
#include <QApplication>
#include <QPalette>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QDateTime>
#include <QTime>
#include <QMap>
#include <QRandomGenerator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <unistd.h>
#include <cmath>
#include <algorithm>
#include "netmon_parser.h"

// ── Constructor ────────────────────────────────────────────────
SystemMonitorV2::SystemMonitorV2(QWidget *parent)
    : QMainWindow(parent)
{
    // Read RAM total BEFORE setupUI() so the gauge scale uses the real
    // installed total rather than the 64 GB fallback clamp.
    QFile fp("/proc/meminfo");
    if (fp.open(QFile::ReadOnly)) {
        QTextStream in(&fp);
        QString line;
        while (in.readLineInto(&line)) {
            if (line.startsWith("MemTotal:")) {
                // /proc/meminfo pads with spaces: "MemTotal:       64582344 kB"
                // so take the first non-empty token after the label.
                auto parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2)
                    m_ramTotalGB = parts[1].toDouble() / 1048576.0;  // KiB → GiB
                break;
            }
        }
    }
    // Fallback to 64 GB if detection fails
    if (m_ramTotalGB <= 0.0) m_ramTotalGB = 64.0;

    setupUI();
    setupStyle();

    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &SystemMonitorV2::tick);
    m_tickTimer->start(250);   // 250ms tick → 4 Hz refresh (well under 1/s)

    // Network manager for live llama.cpp TPS (no external script / dead paths)
    m_net = new QNetworkAccessManager(this);

    // Discover sensor device paths by chip name (robust across reboots)
    discoverSensors();
    // Discover active WAN (wireless) / LAN (wired) interfaces
    discoverNetworkIfaces();
    // Discover the live llama-server backend port (TPS probing)
    discoverTpsEndpoint();

    // Prime the CPU counters so first tick has a valid delta
    readCPU();

    // Prime network connections map so first tick has previous values
    readNetwork();

    // Restore persistent state (detached mode, window geometry)
    loadState();

    // Save state on aboutToQuit so pkill/SIGTERM doesn't lose it
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &SystemMonitorV2::saveState);
}

SystemMonitorV2::~SystemMonitorV2() = default;

// ── Persistent state (detached mode, window geometry) ──────────
void SystemMonitorV2::saveState() {
    QSettings s("Hermes", "sysmonv2");
    s.setValue("detached", m_detached);
    s.setValue("geometry", saveGeometry());
}

void SystemMonitorV2::loadState() {
    QSettings s("Hermes", "sysmonv2");
    bool det = s.value("detached", false).toBool();
    QByteArray geom = s.value("geometry").toByteArray();
    if (!geom.isEmpty())
        restoreGeometry(geom);
    if (det)
        setDetached(true);
}

void SystemMonitorV2::closeEvent(QCloseEvent *event) {
    saveState();
    event->accept();
}

void SystemMonitorV2::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    saveState();
}

void SystemMonitorV2::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    saveState();
}

// ── UI Setup ───────────────────────────────────────────────────
void SystemMonitorV2::setupUI() {
    // Custom central widget that paints the wood panel background
    class WoodPanelWidget : public QWidget {
    public:
        explicit WoodPanelWidget(QWidget *parent = nullptr) : QWidget(parent) {
            m_woodTexture = new QPixmap("/home/sfarrant/oak_veneer_16x9_4k.jpg");
            if (!m_woodTexture->isNull())
                m_woodTexture->setDevicePixelRatio(1.0);
        }
        ~WoodPanelWidget() override { delete m_woodTexture; }
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            if (m_woodTexture && !m_woodTexture->isNull()) {
                p.drawTiledPixmap(rect(), *m_woodTexture);
                // Heavy dark stain to mute the wood grain
                p.fillRect(rect(), QColor(15, 8, 3, 200));
            } else {
                p.fillRect(rect(), QColor(30, 14, 5));
            }
        }
    private:
        QPixmap *m_woodTexture = nullptr;
    };

    auto *central = new WoodPanelWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // ── Detach/Attach toolbar (always visible; toggles NVIDIA-only view) ──
    m_detachBar = new QWidget();
    auto *detachLay = new QHBoxLayout(m_detachBar);
    detachLay->setContentsMargins(2, 2, 2, 2);
    m_detachBtn = new QPushButton("DETACH → NVIDIA ONLY");
    m_detachBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "  stop:0 #8b6914, stop:0.5 #b8860b, stop:1 #3d2a06);"
        "  color: #e8c860; border: 1px solid #d4a843; border-radius: 4px;"
        "  padding: 4px 12px; font-weight: bold; letter-spacing: 1px; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "  stop:0 #a07d18, stop:0.5 #c8960b, stop:1 #4a3308); }"
    );
    connect(m_detachBtn, &QPushButton::clicked, this, [this]() {
        setDetached(!m_detached);
    });
    detachLay->addWidget(m_detachBtn);
    detachLay->addStretch(1);
    mainLayout->addWidget(m_detachBar);

    // ── Brass title plate (cropped to the title frame; rest of row = background) ──
    // titleBar itself is transparent so the wood panel shows through; the brass
    // plate hugs only the title text.
    auto *titleBar = new QWidget();          // transparent — wood background shows
    m_titleBar = titleBar;
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(8, 6, 8, 6);

    // Brass plate sized to the title text only (not the full row width)
    auto *plate = new QWidget();
    plate->setAutoFillBackground(true);
    QPalette brassPal;
    brassPal.setColor(QPalette::Window, QColor(60, 42, 18));
    plate->setPalette(brassPal);
    plate->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "  stop:0 #8b6914, stop:0.3 #b8860b, stop:0.6 #a07010, "
        "  stop:0.85 #6b4e0a, stop:1 #3d2a06);"
        "border: 1px solid #d4a843;"
        "border-radius: 4px;"
    );

    auto *plateShadow = new QGraphicsDropShadowEffect();
    plateShadow->setBlurRadius(8);
    plateShadow->setOffset(2, 2);
    plateShadow->setColor(QColor(0, 0, 0, 120));
    plate->setGraphicsEffect(plateShadow);

    auto *plateLay = new QHBoxLayout(plate);
    plateLay->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel("THE CHRONOMETRIC ENGINE MONITOR");
    QFont tf = titleLabel->font();
    tf.setPointSize(12);
    tf.setBold(true);
    tf.setLetterSpacing(QFont::AbsoluteSpacing, 5);
    tf.setCapitalization(QFont::SmallCaps);
    titleLabel->setFont(tf);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "color: #e8c860;"
        "background: transparent;"
        "padding: 2px 12px;"
        "font-weight: bold;"
    );

    auto *textShadow = new QGraphicsDropShadowEffect();
    textShadow->setBlurRadius(2);
    textShadow->setOffset(1, 1);
    textShadow->setColor(QColor(0, 0, 0, 100));
    titleLabel->setGraphicsEffect(textShadow);

    plateLay->addWidget(titleLabel);
    titleLay->addStretch(1);
    titleLay->addWidget(plate, 0, Qt::AlignCenter);
    titleLay->addStretch(1);
    mainLayout->addWidget(titleBar);

    // ── Rivet row across the top of the panel ──
    auto *rivetRow = new QWidget();
    m_topRivetRow = rivetRow;
    auto *rivetLay = new QHBoxLayout(rivetRow);
    rivetLay->setContentsMargins(0, 0, 0, 0);
    rivetLay->setSpacing(0);
    auto addRivet = [&]() {
        auto *rivet = new QLabel();
        rivet->setFixedSize(10, 10);
        rivet->setStyleSheet(
            "background: qradialgradient(cx:0.35, cy:0.35, radius:0.5, "
            "  fx:0.35, fy:0.35, "
            "  stop:0 #f0d080, stop:0.4 #c8a050, "
            "  stop:0.7 #8a6520, stop:1 #4a3510);"
            "border: 1px solid #3d2a06;"
            "border-radius: 5px;"
        );
        rivetLay->addWidget(rivet);
        rivetLay->addStretch(1);
    };
    for (int i = 0; i < 12; ++i) {
        if (i > 0) rivetLay->addSpacing(4);
        addRivet();
    }
    mainLayout->addWidget(rivetRow);

    // ── Clock row (centered at top, full width) ──
    auto *clockRow = new QWidget();
    m_clockRow = clockRow;
    auto *clockLay = new QHBoxLayout(clockRow);
    clockLay->setContentsMargins(0, 0, 0, 0);
    m_clockGauge = new SteamGauge("CLOCK", "HMS", 0, 60, 61);  // redThreshold=61 → never triggers on 0-60
    m_clockGauge->setArc(270.0, 360.0);  // 12 o'clock at top
    m_clockGauge->setAnimDuration(0);     // instant update for clock hands
    m_clockGauge->setNeedleBaseWidth(0.02);
    m_clockGauge->setSubtitle("");
    m_clockGauge->setMinimumHeight(140);   // match NVIDIA dial height
    // Match each NVIDIA dial's width: give the clock 1/4 of the panel width
    // (addStretch 3 + widget 2 + addStretch 3 → 2/8 = 1/4, centered) so its
    // dial diameter is identical to the NVIDIA gauges below it.
    clockLay->addStretch(3);
    clockLay->addWidget(m_clockGauge, 2);
    clockLay->addStretch(3);
    mainLayout->addWidget(clockRow, 1);  // stretch = 1

    // ── Gauge grid (3 rows × 4 cols) ──
    auto *gaugeGrid = new QGridLayout();
    gaugeGrid->setSpacing(5);

    // Row 0: CPU & core
    m_cpuGauge = new SteamGauge("CPU", "%", 0, 100, 80);
    m_cpuGauge->setSubtitle("-- %");
    m_cpuGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_cpuGauge, 0, 0);

    m_cpuTempGauge = new SteamGauge("CPU TEMP", "°C", 0, 100, 80);
    m_cpuTempGauge->setSubtitle("--°C");
    m_cpuTempGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_cpuTempGauge, 0, 1);

    // RAM gauge uses dynamically detected total RAM for max/red-zone
    m_ramGauge = new SteamGauge("SYSTEM RAM", "GB", 0, m_ramTotalGB, m_ramTotalGB * 0.8);
    m_ramGauge->setSubtitle(QString("-- / %1 GB").arg(m_ramTotalGB, 0, 'f', 0));
    m_ramGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_ramGauge, 0, 2);

    m_chassisGauge = new SteamGauge("BOARD TEMP", "°C", 0, 50, 40);
    m_chassisGauge->setSubtitle("--°C");
    m_chassisGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_chassisGauge, 0, 3);

    // Row 1: iGPU (Radeon 780M)
    m_gpuGauge = new SteamGauge("RADEON 780M", "%", 0, 100, 80);
    m_gpuGauge->setSubtitle("-- %");
    m_gpuGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_gpuGauge, 1, 0);

    m_igpuTempGauge = new SteamGauge("780M TEMP", "°C", 0, 100, 80);
    m_igpuTempGauge->setSubtitle("--°C");
    m_igpuTempGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_igpuTempGauge, 1, 1);

    m_gpuVramGauge = new SteamGauge("780M VRAM", "GB", 0, 2, 1.6);  // 2 GB shared VRAM, red at 80%
    m_gpuVramGauge->setSubtitle("-- GB");
    m_gpuVramGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_gpuVramGauge, 1, 2);

    m_nvmeTempGauge = new SteamGauge("NVMe TEMP", "°C", 0, 100, 80);
    m_nvmeTempGauge->setSubtitle("--°C");
    m_nvmeTempGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_nvmeTempGauge, 1, 3);

    // Row 2: Network + RAM sticks
    m_wanGauge = new SteamGauge("Wi-Fi", "Mbps", 0, 200, 160);
        m_wanGauge->setSubtitle("↓ --");
        m_wanGauge->setBezelColor(QColor(30, 100, 200));
        m_wanGauge->setNeedleColor(QColor(80, 160, 255));
        m_wanGauge->setAnimDuration(0);   // instant — no lag on network speed
        m_wanGauge->setMinimumHeight(140);
        gaugeGrid->addWidget(m_wanGauge, 2, 0);

    m_lanGauge = new SteamGauge("ETHERNET", "Mbps", 0, 1000, 800);
    m_lanGauge->setSubtitle("↓ --  ↑ --");
    m_lanGauge->setBezelColor(QColor(30, 100, 200));
    m_lanGauge->setNeedleColor(QColor(80, 160, 255));
    m_lanGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_lanGauge, 2, 1);

    m_dimmATempGauge = new SteamGauge("DIMM A", "°C", 0, 85, 68);
    m_dimmATempGauge->setSubtitle("--°C");
    m_dimmATempGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_dimmATempGauge, 2, 2);

    m_dimmBTempGauge = new SteamGauge("DIMM B", "°C", 0, 85, 68);
    m_dimmBTempGauge->setSubtitle("--°C");
    m_dimmBTempGauge->setMinimumHeight(140);
    gaugeGrid->addWidget(m_dimmBTempGauge, 2, 3);

    // Equal row/column stretch so gauges fill the form
    for (int r = 0; r < 3; ++r) gaugeGrid->setRowStretch(r, 1);
    for (int c = 0; c < 4; ++c) gaugeGrid->setColumnStretch(c, 1);

    // Wrap the grid in a widget so the whole 3x4 block can be hidden on detach.
    m_gaugeGridW = new QWidget();
    m_gaugeGridW->setLayout(gaugeGrid);
    mainLayout->addWidget(m_gaugeGridW, 1);

    // ── NVIDIA GPU row (full-width) ──
    auto *nvRow = new QWidget();
    m_nvRow = nvRow;
    auto *nvLay = new QHBoxLayout(nvRow);
    nvLay->setContentsMargins(0, 0, 0, 0);
    m_nvGpuGauge = new SteamGauge("4060 Ti", "%", 0, 100, 80);
    m_nvGpuGauge->setSubtitle("Usage\n--%");
    m_nvGpuGauge->setMinimumHeight(140);
    m_nvGpuGauge->setBezelColor(QColor(118, 185, 0));  // NVIDIA green
    m_nvGpuGauge->setNeedleColor(QColor(255, 255, 255));  // white needle
    nvLay->addWidget(m_nvGpuGauge);

    m_nvGpuTempGauge = new SteamGauge("4060 Ti", "°C", 0, 100, 80);
    m_nvGpuTempGauge->setSubtitle("Temp\n--°C");
    m_nvGpuTempGauge->setMinimumHeight(140);
    m_nvGpuTempGauge->setBezelColor(QColor(118, 185, 0));
    m_nvGpuTempGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvGpuTempGauge);

    // Token/TPS gauge placeholder to the right of NVIDIA
    m_nvTpsGauge = new SteamGauge("4060 Ti", "tps", 0, 200, 160);
    m_nvTpsGauge->setSubtitle("TPS\n--");
    m_nvTpsGauge->setMinimumHeight(140);
    m_nvTpsGauge->setBezelColor(QColor(118, 185, 0));
    m_nvTpsGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvTpsGauge);

    m_nvVramGauge = new SteamGauge("4060 Ti", "GB", 0, 16, 12.8);
    m_nvVramGauge->setSubtitle("VRAM\n-- GB");
    m_nvVramGauge->setMinimumHeight(140);
    m_nvVramGauge->setBezelColor(QColor(118, 185, 0));
    m_nvVramGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvVramGauge);

    mainLayout->addWidget(nvRow, 1);  // stretch = 1

    // ── Rivet row at the very bottom ──
    auto *botRivetRow = new QWidget();
    m_botRivetRow = botRivetRow;
    auto *botRivetLay = new QHBoxLayout(botRivetRow);
    botRivetLay->setContentsMargins(0, 0, 0, 0);
    botRivetLay->setSpacing(0);
    auto addBotRivet = [&]() {
        auto *rivet = new QLabel();
        rivet->setFixedSize(10, 10);
        rivet->setStyleSheet(
            "background: qradialgradient(cx:0.35, cy:0.35, radius:0.5, "
            "  fx:0.35, fy:0.35, "
            "  stop:0 #f0d080, stop:0.4 #c8a050, "
            "  stop:0.7 #8a6520, stop:1 #4a3510);"
            "border: 1px solid #3d2a06;"
            "border-radius: 5px;"
        );
        botRivetLay->addWidget(rivet);
        botRivetLay->addStretch(1);
    };
    for (int i = 0; i < 12; ++i) {
        if (i > 0) botRivetLay->addSpacing(4);
        addBotRivet();
    }
    mainLayout->addWidget(botRivetRow);

    setMinimumSize(1200, 1100);
    showMaximized();
    setWindowTitle("Chronometric Engine Monitor");
}

void SystemMonitorV2::setupStyle() {
    setStyleSheet(
        "QMainWindow { background: #2a1208; }"
        "QWidget { background: #2a1208; }"
    );
}

// ── Detach/Attach: show ONLY the NVIDIA/local-GPU row (dials stay live) ──
void SystemMonitorV2::setDetached(bool on) {
    m_detached = on;
    // Rows hidden when detached (nvRow + detachBar always stay visible).
    m_titleBar->setVisible(!on);
    m_topRivetRow->setVisible(!on);
    m_clockRow->setVisible(!on);
    m_gaugeGridW->setVisible(!on);
    m_botRivetRow->setVisible(!on);

    saveState();  // persist immediately so state survives crashes/pkill
    m_detachBtn->setText(on ? "ATTACH ← FULL MONITOR" : "DETACH → NVIDIA ONLY");
    setWindowTitle(on ? "Chronometric Engine Monitor — NVIDIA Only"
                      : "Chronometric Engine Monitor");

    if (on) {
        // Shrink window down to just the NVIDIA row.
        setMinimumSize(0, 0);
        setMaximumSize(16777215, 16777215);
        showNormal();
        adjustSize();
    } else {
        setMinimumSize(1200, 1100);
        showMaximized();
    }
    saveState();
}

// ── F11 Fullscreen ─────────────────────────────────────────────
void SystemMonitorV2::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F11) {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F10) {
        setDetached(!m_detached);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

// ── Tick ───────────────────────────────────────────────────────
void SystemMonitorV2::tick() {
    // Read all sensors (non-blocking where possible)
    readCPU();
    readRAM();
    readSensors();
    readNetwork();
    readIgpu();          // AMD Radeon 780M perf + VRAM

    // Local inference GPU (Radeon 780M) — same device as the top row
    readNvidiaLocalGpu();
    // Agent Pikey TPS (real llama.cpp throughput)
    readTpsAsync();

    // Update all gauges
    // NVIDIA GPU (bottom row, dedicated)
    m_nvGpuGauge->setValue(m_nvGpuUsage);
    m_nvGpuGauge->setSubtitle(
        QString("Usage\n%1%").arg(m_nvGpuUsage, 0, 'f', 0));

    m_nvGpuTempGauge->setValue(m_nvGpuTemp);
    m_nvGpuTempGauge->setSubtitle(QString("Temp\n%1°C").arg(m_nvGpuTemp, 0, 'f', 0));

    m_nvVramGauge->setValue(m_nvVramGB);
    m_nvVramGauge->setSubtitle(QString("VRAM\n%1 GB").arg(m_nvVramGB, 0, 'f', 1));

    // Row 0: CPU & core
    m_cpuGauge->setValue(m_cpuUsage);
    m_cpuGauge->setSubtitle(QString("%1%").arg(m_cpuUsage, 0, 'f', 0));

    m_cpuTempGauge->setValue(m_cpuTemp);
    m_cpuTempGauge->setSubtitle(QString("%1°C").arg(m_cpuTemp, 0, 'f', 0));

    m_ramGauge->setValue(m_ramGB);
    m_ramGauge->setSubtitle(
        QString("%1 / %2 GB")
            .arg(m_ramGB, 0, 'f', 1)
            .arg(m_ramTotalGB, 0, 'f', 0));

    m_chassisGauge->setValue(m_chassisTemp);
    m_chassisGauge->setSubtitle(QString("%1°C").arg(m_chassisTemp, 0, 'f', 0));

    // Row 1: iGPU (Radeon 780M)
    m_gpuGauge->setValue(m_gpuUsage);
    m_gpuGauge->setSubtitle(QString("%1%").arg(m_gpuUsage, 0, 'f', 0));

    m_igpuTempGauge->setValue(m_igpuTemp);
    m_igpuTempGauge->setSubtitle(QString("%1°C").arg(m_igpuTemp, 0, 'f', 0));

    m_gpuVramGauge->setValue(m_gpuVramGB);
    m_gpuVramGauge->setSubtitle(QString("%1 GB").arg(m_gpuVramGB, 0, 'f', 1));

    m_nvmeTempGauge->setValue(m_nvmeTemp);
    m_nvmeTempGauge->setSubtitle(QString("%1°C").arg(m_nvmeTemp, 0, 'f', 0));

    // Row 2: Network + RAM sticks
    m_wanGauge->setValue(m_wanDown);
    m_wanGauge->setSubtitle(
        QString("↓ %1")
            .arg(m_wanDown, 0, 'f', m_wanDown >= 10 ? 1 : 2));

    m_lanGauge->setValue(m_lanDown);
    m_lanGauge->setSecondaryValue(m_lanUp);
    m_lanGauge->setSubtitle(
        QString("↓ %1  ↑ %2")
            .arg(m_lanDown, 0, 'f', m_lanDown >= 10 ? 1 : 2)
            .arg(m_lanUp, 0, 'f', m_lanUp >= 10 ? 1 : 2));

    m_dimmATempGauge->setValue(m_dimmATemp);
    m_dimmATempGauge->setSubtitle(QString("%1°C").arg(m_dimmATemp, 0, 'f', 0));

    m_dimmBTempGauge->setValue(m_dimmBTemp);
    m_dimmBTempGauge->setSubtitle(QString("%1°C").arg(m_dimmBTemp, 0, 'f', 0));

    // ── Clock with sweeping second hand ──
    QTime now = QTime::currentTime();
    double sec = now.second() + now.msec() / 1000.0;          // 0-60 continuous
    double min = now.minute() + sec / 60.0;                    // 0-60 continuous
    double hour = (now.hour() % 12) * 5.0 + min / 12.0;       // 0-60 (maps 12h → 0-60 via *5)
    m_clockGauge->setValue(sec);                               // second hand = primary needle (crimson, long)
    m_clockGauge->setSecondaryValue(min);                      // minute hand = amber secondary (65% length)
    m_clockGauge->setTertiaryValue(hour);                      // hour hand = gold tertiary (50% length)

    // Agent Pikey TPS gauge
    m_nvTpsGauge->setValue(m_nvGpuTps);
    m_nvTpsGauge->setSubtitle(QString("TPS\n%1").arg(m_nvGpuTps, 0, 'f', 1));
}

// ── CPU ────────────────────────────────────────────────────────
void SystemMonitorV2::readCPU() {
    QFile f("/proc/stat");
    if (f.open(QFile::ReadOnly)) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith("cpu ")) {
            auto parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 8) {
                unsigned long long user   = parts[1].toULongLong();
                unsigned long long nice   = parts[2].toULongLong();
                unsigned long long sys    = parts[3].toULongLong();
                unsigned long long idle   = parts[4].toULongLong();
                unsigned long long iowait = parts[5].toULongLong();
                // irq = parts[6], softirq = parts[7] — not used in idle calc
                unsigned long long total = user + nice + sys + idle + iowait;
                unsigned long long diffIdle = idle - m_prevIdle;
                unsigned long long diffTotal = total - m_prevTotal;

                if (diffTotal > 0) {
                    m_cpuUsage = 100.0 * (1.0 - (double)diffIdle / diffTotal);
                }

                m_prevIdle = idle;
                m_prevTotal = total;
            }
        }
    }
}

// ── RAM ────────────────────────────────────────────────────────
void SystemMonitorV2::readRAM() {
    QFile f("/proc/meminfo");
    if (f.open(QFile::ReadOnly)) {
        unsigned long long total = 0, available = 0;
        QTextStream in(&f);
        QString line;
        while (in.readLineInto(&line)) {
            auto parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 2) continue;
            if (parts[0] == "MemTotal:")
                total = parts[1].toULongLong();
            else if (parts[0] == "MemAvailable:")
                available = parts[1].toULongLong();
        }
        if (total > 0) {
            double usedKb = static_cast<double>(total - available);
            m_ramGB = usedKb / 1048576.0;
        }
    }
}

// ── Sensor discovery (by chip name, not hardcoded index) ──────
static QStringList hwmonDirs() {
    QStringList dirs;
    QDir hw("/sys/class/hwmon");
    for (const QString &d : hw.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        dirs << hw.absoluteFilePath(d);
    }
    return dirs;
}

static QString findHwmonTemp(const QString &chipName, int which = 1) {
    // which: Nth occurrence of chipName (1-based) → its temp1_input
    int seen = 0;
    for (const QString &dir : hwmonDirs()) {
        QFile nameFile(dir + "/name");
        if (!nameFile.open(QFile::ReadOnly)) continue;
        QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
        if (name == chipName) {
            if (++seen == which) {
                QString p = QString("%1/temp1_input").arg(dir);
                return p;
            }
        }
    }
    return QString();
}

void SystemMonitorV2::discoverSensors() {
    m_cpuTempPath     = findHwmonTemp("k10temp", 1);
    m_igpuTempPath    = findHwmonTemp("amdgpu", 1);
    m_nvmeTempPath    = findHwmonTemp("nvme", 1);
    m_dimmATempPath   = findHwmonTemp("spd5118", 1);
    m_dimmBTempPath   = findHwmonTemp("spd5118", 2);
    m_chassisTempPath = findHwmonTemp("acpitz", 1);

    // AMD iGPU card — Radeon 780M exposes gpu_busy_percent + mem_info_*
    QDir drm("/sys/class/drm");
    for (const QString &c : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!c.startsWith("card") || c.contains('-')) continue;
        QString dev = drm.absoluteFilePath(c) + "/device";
        if (QFile::exists(dev + "/gpu_busy_percent")) {
            m_igpuCardPath = dev;
            break;
        }
    }
}

static double readTempC(const QString &path) {
    if (path.isEmpty()) return 0.0;
    QFile f(path);
    if (f.open(QFile::ReadOnly))
        return QString::fromUtf8(f.readAll().trimmed()).toDouble() / 1000.0;
    return 0.0;
}

// ── Sensors (k10temp, nvme, etc.) ──────────────────────────────
void SystemMonitorV2::readSensors() {
    m_cpuTemp     = readTempC(m_cpuTempPath);
    m_igpuTemp    = readTempC(m_igpuTempPath);
    m_nvmeTemp    = readTempC(m_nvmeTempPath);
    m_dimmATemp   = readTempC(m_dimmATempPath);
    m_dimmBTemp   = readTempC(m_dimmBTempPath);
    m_chassisTemp = readTempC(m_chassisTempPath);
}

// ── AMD Radeon 780M iGPU (perf % + VRAM used) ─────────────────
void SystemMonitorV2::readIgpu() {
    if (m_igpuCardPath.isEmpty()) return;

    QFile busy(m_igpuCardPath + "/gpu_busy_percent");
    if (busy.open(QFile::ReadOnly))
        m_gpuUsage = QString::fromUtf8(busy.readAll().trimmed()).toDouble();

    auto readU64 = [this](const QString &rel) -> qulonglong {
        QFile f(m_igpuCardPath + "/" + rel);
        if (f.open(QFile::ReadOnly))
            return QString::fromUtf8(f.readAll().trimmed()).toULongLong();
        return 0;
    };

    qulonglong vramUsed = readU64("mem_info_vram_used");
    qulonglong gttUsed  = readU64("mem_info_gtt_used");

    if (vramUsed > 0 || gttUsed > 0) {
        // VRAM + GTT used, converted to GB
        m_gpuVramGB = static_cast<double>(vramUsed + gttUsed) / 1073741824.0;
    }
}

// ── Network interface discovery (robust across reboots) ───────
void SystemMonitorV2::discoverNetworkIfaces() {
    // WAN = first UP wireless interface (from `iw dev`).
    // LAN = first UP wired interface that isn't the wireless one.
    // Fall back to the hardcoded defaults if discovery finds nothing.
    QString wan, lan;

    QProcess iw;
    iw.start("iw", {"dev"}, QIODevice::ReadOnly);
    iw.waitForFinished(500);
    QString iwOut = QString::fromUtf8(iw.readAllStandardOutput());
    for (const QString &line : iwOut.split('\n')) {
        int idx = line.indexOf("Interface");
        if (idx >= 0) {
            QString name = line.mid(idx + 9).trimmed();
            if (!name.isEmpty()) { wan = name; break; }
        }
    }

    // Wired: first UP interface with a MAC that isn't wireless/loopback/virtual.
    QProcess ip;
    ip.start("ip", {"-o", "link", "show", "up"}, QIODevice::ReadOnly);
    ip.waitForFinished(500);
    QString ipOut = QString::fromUtf8(ip.readAllStandardOutput());
    static const QStringList ignore = {"lo", "docker0", "virbr0", "tailscale0", "veth", "br-"};
    for (const QString &line : ipOut.split('\n')) {
        QString name = line.section(':', 1, 1).trimmed().section('@', 0, 0).trimmed();
        if (name.isEmpty() || name == wan) continue;
        bool skip = false;
        for (const QString &p : ignore)
            if (name.startsWith(p)) { skip = true; break; }
        if (skip) continue;
        // wireless interfaces report "wireless" / "wlan" flags; treat as WAN-side
        if (name.startsWith("wl")) continue;
        lan = name;
        break;
    }

    if (!wan.isEmpty()) m_wanIface = wan;
    if (!lan.isEmpty()) m_lanIface = lan;
}

// ── Network via /proc/net/dev (per-interface throughput) ──────
void SystemMonitorV2::readNetwork() {
    // Per-interface byte counters, tracked across ticks
    static QMap<QString, unsigned long long> prevRx;
    static QMap<QString, unsigned long long> prevTx;

    QMap<QString, QPair<unsigned long long, unsigned long long>> stats;
    QFile f("/proc/net/dev");
    if (f.open(QFile::ReadOnly))
        parseProcNetDev(QString::fromUtf8(f.readAll()), stats);

    auto get = [&](const QString &iface) -> QPair<unsigned long long, unsigned long long> {
        auto it = stats.find(iface);
        return (it != stats.end()) ? *it : qMakePair(0ULL, 0ULL);
    };

    auto wan = get(m_wanIface);
    auto lan = get(m_lanIface);
    unsigned long long wanRx = wan.first, wanTx = wan.second;
    unsigned long long lanRx = lan.first, lanTx = lan.second;

    double intervalSec = 0.25;  // 250ms tick

    if (prevRx.contains(m_wanIface)) {
        double dRx = static_cast<double>(wanRx - prevRx[m_wanIface]);
        double dTx = static_cast<double>(wanTx - prevTx[m_wanIface]);
        // Convert MB/s → Mbps to match the "Mbps" dial label (×8).
        m_wanDown = mbPerSecToMbps(dRx / 1'000'000.0 / intervalSec);
        m_wanUp   = mbPerSecToMbps(dTx / 1'000'000.0 / intervalSec);
        m_cumWanRx += static_cast<unsigned long long>(dRx);
        m_cumWanTx += static_cast<unsigned long long>(dTx);
    }
    if (prevRx.contains(m_lanIface)) {
        double dRx = static_cast<double>(lanRx - prevRx[m_lanIface]);
        double dTx = static_cast<double>(lanTx - prevTx[m_lanIface]);
        m_lanDown = mbPerSecToMbps(dRx / 1'000'000.0 / intervalSec);    // Mbps
        m_lanUp   = mbPerSecToMbps(dTx / 1'000'000.0 / intervalSec);
    }

    prevRx[m_wanIface] = wanRx; prevTx[m_wanIface] = wanTx;
    prevRx[m_lanIface] = lanRx; prevTx[m_lanIface] = lanTx;
}

// ── NVIDIA RTX 4060 Ti via nvidia-smi ──
void SystemMonitorV2::readNvidiaLocalGpu() {
    QProcess smi;
    smi.start("nvidia-smi", {
        "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
        "--format=csv,noheader,nounits"
    });
    if (!smi.waitForFinished(3000)) {
        m_nvGpuUsage = 0.0; m_nvGpuTemp = 0.0; m_nvVramGB = 0.0;
        return;
    }
    QString line = QString::fromUtf8(smi.readAllStandardOutput()).trimmed();
    QStringList parts = line.split(',');
    if (parts.size() < 4) { m_nvGpuUsage = 0.0; m_nvGpuTemp = 0.0; m_nvVramGB = 0.0; return; }

    m_nvGpuUsage = parts[0].trimmed().toDouble();
    m_nvGpuTemp  = parts[1].trimmed().toDouble();
    double vramUsedMiB = parts[2].trimmed().toDouble();
    // double vramTotalMiB = parts[3].trimmed().toDouble();  // 16380
    m_nvVramGB = vramUsedMiB / 1024.0;
}

// ── llama.cpp TPS — backend-agnostic, probes the live server directly ──
//
// discoverTpsEndpoint() runs once at startup: it reads the llama-server
// process command-line to find the live backend port (e.g. 39143 under LM
// Studio, or 8081 for a standalone server).  readTpsAsync() then probes
// /v1/completions directly — no /v1/models chokepoint — and tries several
// standard timing field paths so TPS works regardless of model or version.
void SystemMonitorV2::discoverTpsEndpoint() {
    // Parse /proc for the first llama-server we find and grab its --port arg
    QDir proc("/proc");
    const QStringList pids = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &pid : pids) {
        bool ok = false;
        int n = pid.toInt(&ok);
        if (!ok) continue;

        // Read cmdline (NUL-separated args, just search for --port)
        QFile cmdFile(QString("/proc/%1/cmdline").arg(pid));
        if (!cmdFile.open(QFile::ReadOnly)) continue;
        QByteArray cmdline = cmdFile.readAll();
        cmdFile.close();

        if (!cmdline.contains("llama-server")) continue;

        // Find --port <number>
        int idx = cmdline.indexOf("--port");
        if (idx < 0) idx = cmdline.indexOf("-p");       // short form
        if (idx < 0) continue;

        // Skip past "--port\0" or "-p\0"
        idx += cmdline.indexOf('\0', idx) + 1;
        if (idx >= cmdline.size()) continue;

        // Read the port number
        QByteArray portStr;
        while (idx < cmdline.size() && cmdline[idx] != '\0') {
            portStr.append(cmdline[idx]);
            ++idx;
        }
        bool portOk = false;
        int port = portStr.toInt(&portOk);
        if (!portOk || port <= 0) continue;

        m_tpsApiUrl = QString("http://127.0.0.1:%1").arg(port);

        // Also grab the API key (LM Studio generates one per backend)
        int ki = cmdline.indexOf("--api-key");
        if (ki >= 0) {
            ki += cmdline.indexOf('\0', ki) + 1;
            QByteArray keyStr;
            while (ki < cmdline.size() && cmdline[ki] != '\0') {
                keyStr.append(cmdline[ki]);
                ++ki;
            }
            m_tpsApiKey = QString::fromUtf8(keyStr);
        }
        return;  // use the first llama-server we find
    }

    // Fallback: try LM Studio's API proxy port
    m_tpsApiUrl = "http://127.0.0.1:1234";
}

// Probes the discovered llama-server backend for tokens/sec.
// No dependency on /v1/models — probes the raw /v1/completions endpoint
// which always works with whichever model is currently loaded.
void SystemMonitorV2::readTpsAsync() {
    if (m_tpsBusy || m_tpsApiUrl.isEmpty()) return;

    // Throttle: probe every 20 ticks (~5s at 250ms) to reduce backend load.
    // Count down from 20; fire when counter reaches 0.
    if (m_tpsProbeCounter > 0) {
        --m_tpsProbeCounter;
        return;
    }
    m_tpsProbeCounter = 20;  // reset for next cycle
    m_tpsBusy = true;

    // Tiny probe — use /v1/chat/completions which works through both
    // raw llama.cpp backends and the LM Studio proxy.
    QJsonObject payload;
    QJsonArray msgs;
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = "say the word hello";
    msgs.append(userMsg);
    payload["messages"] = msgs;
    payload["max_tokens"] = 12;
    payload["temperature"] = 0;
    payload["stream"] = false;
    if (!m_llmModel.isEmpty())
        payload["model"] = m_llmModel;

    QNetworkRequest preq(m_tpsApiUrl + "/v1/chat/completions");
    preq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!m_tpsApiKey.isEmpty())
        preq.setRawHeader("Authorization", ("Bearer " + m_tpsApiKey).toUtf8());

    qint64 probeStart = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply *preply = m_net->post(preq, QJsonDocument(payload).toJson());

    connect(preply, &QNetworkReply::finished, this, [this, preply, probeStart]() {
        preply->deleteLater();
        m_tpsBusy = false;

        if (preply->error() != QNetworkReply::NoError) {
            m_nvGpuTps = m_tpsLastGood;  // sticky — keep last known on error
            return;
        }

        const QJsonDocument pdoc = QJsonDocument::fromJson(preply->readAll());
        const QJsonObject obj = pdoc.object();

        // Try every TPS field location llama.cpp may use across versions
        double tps = 0.0;
        // 1. timings.predicted_per_second (raw llama.cpp backend)
        const QJsonObject timings = obj.value("timings").toObject();
        tps = timings.value("predicted_per_second").toDouble(0.0);

        // 2. Top-level tokens_per_second
        if (tps <= 0.0)
            tps = obj.value("tokens_per_second").toDouble(0.0);

        // 3. Top-level predicted_per_second
        if (tps <= 0.0)
            tps = obj.value("predicted_per_second").toDouble(0.0);

        // 4. timings.tokens_per_second
        if (tps <= 0.0)
            tps = timings.value("tokens_per_second").toDouble(0.0);

        // 5. Fallback: calculate from wall-clock time + usage.completion_tokens.
        //    Works with LM Studio proxy (no timings object) and any
        //    OpenAI-compatible server.
        if (tps <= 0.0) {
            const QJsonObject usage = obj.value("usage").toObject();
            int compToks = usage.value("completion_tokens").toInt(0);
            if (compToks > 0) {
                double elapsedSec = (QDateTime::currentMSecsSinceEpoch() - probeStart) / 1000.0;
                if (elapsedSec > 0.001)
                    tps = static_cast<double>(compToks) / elapsedSec;
            }
        }

        if (tps > 0.0) {
            m_nvGpuTps = tps;
            m_tpsLastGood = tps;
        }
        // else: keep m_tpsLastGood (sticky)
    });

    // Also kick off a lightweight /v1/models query for the label (non-blocking)
    if (m_llmModel.isEmpty()) {
        QNetworkRequest mreq(m_tpsApiUrl + "/v1/models");
        mreq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!m_tpsApiKey.isEmpty())
            mreq.setRawHeader("Authorization", ("Bearer " + m_tpsApiKey).toUtf8());
        QNetworkReply *mreply = m_net->get(mreq);
        connect(mreply, &QNetworkReply::finished, this, [this, mreply]() {
            mreply->deleteLater();
            if (mreply->error() != QNetworkReply::NoError) return;
            const QJsonDocument doc = QJsonDocument::fromJson(mreply->readAll());
            const QJsonArray data = doc.object().value("data").toArray();
            if (data.isEmpty()) return;
            const QString id = data.first().toObject().value("id").toString();
            if (!id.isEmpty()) {
                m_llmModel = id;
                // Update TPS gauge title to show model name below "4060 Ti"
                QString sn = id.section('/', -1);
                if (sn.endsWith(".gguf", Qt::CaseInsensitive))
                    sn.chop(5);
                m_nvTpsGauge->setTitle("4060\nTi\n" + sn);
            }
        });
    }
}