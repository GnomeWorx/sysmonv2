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
#include <QCalendarWidget>
#include <unistd.h>
#include <cmath>
#include <algorithm>

// ── Constructor ────────────────────────────────────────────────
SystemMonitorV2::SystemMonitorV2(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    setupStyle();

    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &SystemMonitorV2::tick);
    m_tickTimer->start(250);   // 250ms tick → 4 Hz refresh (well under 1/s)

    // Read RAM total once at startup
    QFile fp("/proc/meminfo");
    if (fp.open(QFile::ReadOnly)) {
        QTextStream in(&fp);
        QString line;
        while (in.readLineInto(&line)) {
            if (line.startsWith("MemTotal:")) {
                double kb = line.section(' ', 1, 1).trimmed().toDouble();
                m_ramTotalGB = kb / 1048576.0;  // KiB → GiB
                break;
            }
        }
    }
    // Fallback to 64 GB if detection fails
    if (m_ramTotalGB <= 0.0) m_ramTotalGB = 64.0;

    // Prime the CPU counters so first tick has a valid delta
    readCPU();

    // Prime network connections map so first tick has previous values
    readNetwork();
}

SystemMonitorV2::~SystemMonitorV2() = default;

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

    // ── Brass plate title bar ──
    auto *titleBar = new QWidget();
    auto *titleLay = new QHBoxLayout(titleBar);
    titleLay->setContentsMargins(8, 6, 8, 6);

    titleBar->setAutoFillBackground(true);
    QPalette brassPal;
    brassPal.setColor(QPalette::Window, QColor(60, 42, 18));
    titleBar->setPalette(brassPal);
    titleBar->setStyleSheet(
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
    titleBar->setGraphicsEffect(plateShadow);

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

    titleLay->addStretch(1);
    titleLay->addWidget(titleLabel, 0, Qt::AlignCenter);
    titleLay->addStretch(1);
    mainLayout->addWidget(titleBar);

    // ── Rivet row across the top of the panel ──
    auto *rivetRow = new QWidget();
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

    // ── Clock row (full width, above gauge grid) ──
    auto *clockRow = new QWidget();
    auto *clockLay = new QHBoxLayout(clockRow);
    clockLay->setContentsMargins(0, 0, 0, 0);
    m_clockGauge = new SteamGauge("CLOCK", "HMS", 0, 60, 61);  // redThreshold=61 → never triggers on 0-60
    m_clockGauge->setArc(270.0, 360.0);  // 12 o'clock at top
    m_clockGauge->setAnimDuration(0);     // instant update for clock hands
    m_clockGauge->setNeedleBaseWidth(0.02);
    m_clockGauge->setSubtitle("");
    m_clockGauge->setFixedHeight(220);
    clockLay->addWidget(m_clockGauge);

    // Calendar widget to the right of clock
    m_calendar = new QCalendarWidget();
    m_calendar->setMinimumWidth(180);
    m_calendar->setStyleSheet(
        "QCalendarWidget {"
        "  background: #2a1208;"
        "  border: 1px solid #555;"
        "  font-size: 10pt;"
        "}"
        "QCalendarWidget::weekday-header {"
        "  background: #40301a;"
        "  color: #e8c860;"
        "  font-weight: bold;"
        "  padding: 4px;"
        "}"
        "QCalendarWidget::day-number {"
        "  color: #c8a050;"
        "  font-weight: normal;"
        "  padding: 4px;"
        "}"
        "QCalendarWidget::current-date {"
        "  background: #b8860b;"
        "  color: #2a1208;"
        "  font-weight: bold;"
        "}"
        "QCalendarWidget::today {"
        "  border: 2px solid #e8c860;"
        "}"
        "QCalendarWidget::navigation-bar {"
        "  background: #b8860b;"
        "  padding: 4px;"
        "}"
        "QCalendarWidget::button {"
        "  background: #b8860b;"
        "  border: 1px solid #c8a050;"
        "  color: #2a1208;"
        "  font-weight: bold;"
        "  border-radius: 3px;"
        "  padding: 2px 6px;"
        "}"
        "QCalendarWidget::button:hover {"
        "  background: #d4a843;"
        "  border: 1px solid #e8c860;"
        "}"
    );
    clockLay->addWidget(m_calendar);

    mainLayout->addWidget(clockRow);

    // ── Gauge grid (3 rows × 4 cols) ──
    auto *gaugeGrid = new QGridLayout();
    gaugeGrid->setSpacing(5);

    // Row 0: CPU & core
    m_cpuGauge = new SteamGauge("CPU", "%", 0, 100, 80);
    m_cpuGauge->setSubtitle("-- %");
    m_cpuGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_cpuGauge, 0, 0);

    m_cpuTempGauge = new SteamGauge("CPU TEMP", "°C", 0, 100, 80);
    m_cpuTempGauge->setSubtitle("--°C");
    m_cpuTempGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_cpuTempGauge, 0, 1);

    // RAM gauge uses dynamically detected total RAM for max/red-zone
    double ramMax = qMax(m_ramTotalGB, 64.0);
    double ramRedZone = ramMax * 0.8;
    m_ramGauge = new SteamGauge("RAM", "GB", 0, ramMax, ramRedZone);
    m_ramGauge->setSubtitle("-- / 64 GB");
    m_ramGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_ramGauge, 0, 2);

    m_chassisGauge = new SteamGauge("CHASSIS", "°C", 0, 50, 40);
    m_chassisGauge->setSubtitle("--°C");
    m_chassisGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_chassisGauge, 0, 3);

    // Row 1: iGPU (Radeon 780M)
    m_gpuGauge = new SteamGauge("M780 PERF", "%", 0, 100, 80);
    m_gpuGauge->setSubtitle("-- %");
    m_gpuGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_gpuGauge, 1, 0);

    m_igpuTempGauge = new SteamGauge("M780 TEMP", "°C", 0, 100, 80);
    m_igpuTempGauge->setSubtitle("--°C");
    m_igpuTempGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_igpuTempGauge, 1, 1);

    m_gpuVramGauge = new SteamGauge("M780 VRAM", "GB", 0, 2, 1.6);  // 2 GB shared VRAM, red at 80%
    m_gpuVramGauge->setSubtitle("-- GB");
    m_gpuVramGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_gpuVramGauge, 1, 2);

    m_nvmeTempGauge = new SteamGauge("NVME TEMP", "°C", 0, 100, 80);
    m_nvmeTempGauge->setSubtitle("--°C");
    m_nvmeTempGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_nvmeTempGauge, 1, 3);

    // Row 2: Network + RAM sticks
    m_wanGauge = new SteamGauge("WAN", "Mbps", 0, 200, 160);
    m_wanGauge->setSubtitle("↓ --  ↑ --");
    m_wanGauge->setBezelColor(QColor(30, 100, 200));
    m_wanGauge->setNeedleColor(QColor(80, 160, 255));
    m_wanGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_wanGauge, 2, 0);

    m_lanGauge = new SteamGauge("LAN", "Mbps", 0, 1000, 800);
    m_lanGauge->setSubtitle("↓ --  ↑ --");
    m_lanGauge->setBezelColor(QColor(30, 100, 200));
    m_lanGauge->setNeedleColor(QColor(80, 160, 255));
    m_lanGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_lanGauge, 2, 1);

    m_dimmATempGauge = new SteamGauge("DIMM A", "°C", 0, 85, 68);
    m_dimmATempGauge->setSubtitle("--°C");
    m_dimmATempGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_dimmATempGauge, 2, 2);

    m_dimmBTempGauge = new SteamGauge("DIMM B", "°C", 0, 85, 68);
    m_dimmBTempGauge->setSubtitle("--°C");
    m_dimmBTempGauge->setFixedHeight(220);
    gaugeGrid->addWidget(m_dimmBTempGauge, 2, 3);

    // Equal row/column stretch so gauges fill the form
    for (int r = 0; r < 3; ++r) gaugeGrid->setRowStretch(r, 1);
    for (int c = 0; c < 4; ++c) gaugeGrid->setColumnStretch(c, 1);

    mainLayout->addLayout(gaugeGrid, 1);

    // ── NVIDIA GPU row (full-width) ──
    auto *nvRow = new QWidget();
    auto *nvLay = new QHBoxLayout(nvRow);
    nvLay->setContentsMargins(0, 0, 0, 0);
    m_nvGpuGauge = new SteamGauge("NVIDIA GEFORCE RTX 4060 Ti", "%", 0, 100, 80);
    m_nvGpuGauge->setSubtitle("-- % / --°C");
    m_nvGpuGauge->setFixedHeight(220);
    m_nvGpuGauge->setBezelColor(QColor(118, 185, 0));  // NVIDIA green
    m_nvGpuGauge->setNeedleColor(QColor(255, 255, 255));  // white needle
    nvLay->addWidget(m_nvGpuGauge);

    // Token/TPS gauge placeholder to the right of NVIDIA
    m_nvTpsGauge = new SteamGauge("NVIDIA TPS", "tps", 0, 1000, 800);
    m_nvTpsGauge->setSubtitle("-- tokens/s");
    m_nvTpsGauge->setFixedHeight(220);
    m_nvTpsGauge->setBezelColor(QColor(118, 185, 0));
    m_nvTpsGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvTpsGauge);

    mainLayout->addWidget(nvRow);

    // ── Rivet row at the very bottom ──
    auto *botRivetRow = new QWidget();
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
    resize(1400, 1200);
    setWindowTitle("Chronometric Engine Monitor");
}

void SystemMonitorV2::setupStyle() {
    setStyleSheet(
        "QMainWindow { background: #2a1208; }"
        "QWidget { background: #2a1208; }"
    );
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
    QMainWindow::keyPressEvent(event);
}

// ── Tick ───────────────────────────────────────────────────────
void SystemMonitorV2::tick() {
    // Read all sensors (non-blocking where possible)
    readCPU();
    readRAM();
    readSensors();
    readNetwork();

    // NVIDIA GPU is read asynchronously via QProcess to avoid blocking
    readNvidiaAsync();

    // Agent Pikey TPS (placeholder)
    readAgentPikeyStats();

    // Update all gauges
    // NVIDIA GPU (bottom row, dedicated)
    m_nvGpuGauge->setValue(m_nvGpuUsage);
    m_nvGpuGauge->setSubtitle(
        QString("%1% / %2°C")
            .arg(m_nvGpuUsage, 0, 'f', 0)
            .arg(m_nvGpuTemp, 0, 'f', 0));

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
    m_wanGauge->setSecondaryValue(m_wanUp);
    m_wanGauge->setSubtitle(
        QString("↓ %1  ↑ %2")
            .arg(m_wanDown, 0, 'f', m_wanDown >= 10 ? 1 : 2)
            .arg(m_wanUp, 0, 'f', m_wanUp >= 10 ? 1 : 2));

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

    // Highlight today's date on calendar
    m_calendar->setSelectedDate(QDate::currentDate());

    // Agent Pikey TPS gauge
    m_nvTpsGauge->setValue(m_nvGpuTps);
    m_nvTpsGauge->setSubtitle(QString("%1 tokens/s").arg(m_nvGpuTps, 0, 'f', 1));
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
                unsigned long long idleTotal = idle + iowait;

                if (m_prevTotal > 0) {
                    unsigned long long dIdle  = idleTotal - m_prevIdle;
                    unsigned long long dTotal = total - m_prevTotal;
                    m_cpuUsage = (dTotal > 0) ? (1.0 - (double)dIdle / dTotal) * 100.0 : 0.0;
                }
                m_prevIdle  = idleTotal;
                m_prevTotal = total;
            }
        }
    }
}

// ── NVIDIA GPU (async, non-blocking) ───────────────────────────
void SystemMonitorV2::readNvidiaAsync() {
    // Use member QProcess that persists across ticks; start a new read
    // only when the previous one has finished. This avoids blocking the UI.
    if (!m_nvidiaProc) {
        m_nvidiaProc = new QProcess(this);
        connect(m_nvidiaProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
                    QString out = m_nvidiaProc->readAllStandardOutput().trimmed();
                    if (!out.isEmpty()) {
                        QStringList parts = out.split(',');
                        if (parts.size() >= 2) {
                            m_nvGpuUsage = parts[0].trimmed().toDouble();
                            m_nvGpuTemp  = parts[1].trimmed().toDouble();
                        }
                    }
                    m_nvidiaPending = false;
                    m_nvidiaProc->deleteLater();
                    m_nvidiaProc = nullptr;
                });
    }

    if (!m_nvidiaPending) {
        m_nvidiaPending = true;
        m_nvidiaProc->start("nvidia-smi",
            QStringList() << "--query-gpu=utilization.gpu,temperature.gpu"
                          << "--format=csv,noheader,nounits");
    }
    // If pending, we keep the previous values (smooth update on next completion)
}

// ── RAM ────────────────────────────────────────────────────────
void SystemMonitorV2::readRAM() {
    QFile f("/proc/meminfo");
    if (f.open(QFile::ReadOnly)) {
        QTextStream in(&f);
        QString line;
        while (in.readLineInto(&line)) {
            if (line.startsWith("MemAvailable:")) {
                double availKb = line.section(' ', -2, -2).trimmed().toDouble();
                double totalKb = m_ramTotalGB * 1048576.0;
                m_ramGB = (totalKb - availKb) / 1048576.0;
                break;
            }
        }
    }
}

// ── Sensors ────────────────────────────────────────────────────
void SystemMonitorV2::readSensors() {
    // Reset temps each tick; we'll overwrite if found
    m_cpuTemp = 0.0;
    m_nvmeTemp = 0.0;
    m_igpuTemp = 0.0;
    m_chassisTemp = 0.0;
    m_ethTemp = 0.0;

    // Single pass through hwmon0..9 to find all sensors
    for (int h = 0; h < 10; ++h) {
        QFile nf(QString("/sys/class/hwmon/hwmon%1/name").arg(h));
        if (!nf.open(QFile::ReadOnly)) continue;
        QString name = QString::fromUtf8(nf.readAll()).trimmed();
        nf.close();

        QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
        if (!tf.open(QFile::ReadOnly)) continue;
        double temp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
        tf.close();

        if (name == "k10temp") {
            m_cpuTemp = temp;
        } else if (name == "nvme") {
            m_nvmeTemp = temp;
        } else if (name == "amdgpu") {
            m_igpuTemp = temp;
        } else if (name == "acpitz") {
            m_chassisTemp = temp;
        } else if (name == "r8169" || name.contains("r8169")) {
            m_ethTemp = temp;
        }
    }

    // Read DIMM temps from SPD5118 (usually hwmon4 = DIMM A, hwmon5 = DIMM B)
    int spdCount = 0;
    for (int h = 0; h < 10; ++h) {
        QFile nf(QString("/sys/class/hwmon/hwmon%1/name").arg(h));
        if (!nf.open(QFile::ReadOnly)) continue;
        QString name = QString::fromUtf8(nf.readAll()).trimmed();
        nf.close();

        if (name == "spd5118") {
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (tf.open(QFile::ReadOnly)) {
                double temp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
                tf.close();
                if (spdCount == 0) {
                    m_dimmATemp = temp;
                } else if (spdCount == 1) {
                    m_dimmBTemp = temp;
                }
                spdCount++;
            }
        }
    }
}

// ── Network ────────────────────────────────────────────────────
void SystemMonitorV2::readNetwork() {
    QProcess proc;
    proc.start("ss", QStringList() << "-i" << "-t" << "-n");
    proc.waitForFinished(2000);
    QString output = proc.readAllStandardOutput();

    std::map<QString, Conn> curConns;
    double wanDown = 0, wanUp = 0, lanDown = 0, lanUp = 0;

    QStringList lines = output.split('\n');
    for (int i = 0; i < lines.size() - 1; ++i) {
        QString l = lines[i].trimmed();
        if (l.contains("ESTAB") && l.contains(":")) {
            if (i + 1 < lines.size()) {
                QString infoLine = lines[i + 1].trimmed();
                QStringList parts = l.split(QRegularExpression("\\s+"));
                QString localAddr, peerAddr;
                if (parts.size() >= 2) {
                    localAddr = parts[0].section(':', 0, 0);
                    peerAddr  = parts[1].section(':', 0, 0);
                }

                QRegularExpression rxRe(R"(bytes_received:(\d+))");
                QRegularExpression txRe(R"(bytes_sent:(\d+))");
                auto rxM = rxRe.match(infoLine);
                auto txM = txRe.match(infoLine);

                if (rxM.hasMatch() && txM.hasMatch()) {
                    unsigned long long rx = rxM.captured(1).toULongLong();
                    unsigned long long tx = txM.captured(1).toULongLong();
                    QString key = localAddr + "-" + peerAddr;
                    curConns[key] = {rx, tx};

                    auto it = m_prevConns.find(key);
                    if (it != m_prevConns.end() && rx >= it->second.rx && tx >= it->second.tx) {
                        unsigned long long dRx = rx - it->second.rx;
                        unsigned long long dTx = tx - it->second.tx;

                        // Skip localhost
                        if (peerAddr == "127.0.0.1" || peerAddr == "::1")
                            continue;

                        // Classify as private (LAN) vs public (WAN) based on peer IP
                        bool isPrivate = false;
                        QString remoteIp = peerAddr.section(':', 0, -2);  // strip port
                        if (remoteIp.startsWith('[')) {
                            // IPv6 bracket notation: [::1]:port
                            remoteIp = remoteIp.mid(1, remoteIp.lastIndexOf(']') - 1);
                        }
                        if (remoteIp.startsWith("10.") ||
                            remoteIp.startsWith("192.168.") ||
                            (remoteIp.startsWith("172.") && remoteIp.section('.', 1, 1).toInt() >= 16 && remoteIp.section('.', 1, 1).toInt() <= 31) ||
                            remoteIp.startsWith("fc") || remoteIp.startsWith("fd") ||
                            remoteIp.startsWith("fe80"))
                            isPrivate = true;

                        // Convert bytes per 250ms tick → Mbps
                        const double intervalSec = 0.25;  // 250 ms tick
                        double speedMbpsRx = (dRx * 8.0 / 1'000'000.0) / intervalSec;
                        double speedMbpsTx = (dTx * 8.0 / 1'000'000.0) / intervalSec;

                        if (isPrivate) {
                            lanDown += speedMbpsRx;
                            lanUp   += speedMbpsTx;
                            m_cumLanRx += dRx;
                            m_cumLanTx += dTx;
                        } else {
                            wanDown += speedMbpsRx;
                            wanUp   += speedMbpsTx;
                            m_cumWanRx += dRx;
                            m_cumWanTx += dTx;
                        }
                    }
                }
            }
        }
    }

    m_wanDown = wanDown;
    m_wanUp = wanUp;
    m_lanDown = lanDown;
    m_lanUp = lanUp;
    m_prevConns = std::move(curConns);
}

// ── Agent Pikey Token Stats (async, non-blocking) ────────────────
void SystemMonitorV2::readAgentPikeyStats() {
    if (!m_tpsProc) {
        m_tpsProc = new QProcess(this);
        connect(m_tpsProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
                    QString out = m_tpsProc->readAllStandardOutput().trimmed();
                    if (!out.isEmpty()) {
                        bool ok = false;
                        double tps = out.toDouble(&ok);
                        if (ok) {
                            m_nvGpuTps = tps;
                        }
                    }
                    m_tpsPending = false;
                    m_tpsProc->deleteLater();
                    m_tpsProc = nullptr;
                });
    }

    if (!m_tpsPending) {
        m_tpsPending = true;
        m_tpsProc->start("python3", QStringList() << "/home/sfarrant/sysmonv2/measure_tps.py");
    }
    // If pending, keep previous value (smooth update on next completion)
}