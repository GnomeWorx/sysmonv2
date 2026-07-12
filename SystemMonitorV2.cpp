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
#include <QCalendarWidget>
#include <QRandomGenerator>
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

    // Discover sensor device paths by chip name (robust across reboots)
    discoverSensors();

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
        m_wanGauge->setSubtitle("↓ --");
        m_wanGauge->setBezelColor(QColor(30, 100, 200));
        m_wanGauge->setNeedleColor(QColor(80, 160, 255));
        m_wanGauge->setAnimDuration(0);   // instant — no lag on network speed
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

    m_nvGpuTempGauge = new SteamGauge("NVIDIA TEMP", "°C", 0, 100, 80);
    m_nvGpuTempGauge->setSubtitle("--°C");
    m_nvGpuTempGauge->setFixedHeight(220);
    m_nvGpuTempGauge->setBezelColor(QColor(118, 185, 0));
    m_nvGpuTempGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvGpuTempGauge);

    // Token/TPS gauge placeholder to the right of NVIDIA
    m_nvTpsGauge = new SteamGauge("NVIDIA TPS", "tps", 0, 100, 80);
    m_nvTpsGauge->setSubtitle("-- tokens/s");
    m_nvTpsGauge->setFixedHeight(220);
    m_nvTpsGauge->setBezelColor(QColor(118, 185, 0));
    m_nvTpsGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvTpsGauge);

    m_nvVramGauge = new SteamGauge("NVIDIA VRAM", "GB", 0, 16, 12.8);
    m_nvVramGauge->setSubtitle("-- GB");
    m_nvVramGauge->setFixedHeight(220);
    m_nvVramGauge->setBezelColor(QColor(118, 185, 0));
    m_nvVramGauge->setNeedleColor(QColor(255, 255, 255));
    nvLay->addWidget(m_nvVramGauge);

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
    showMaximized();
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
    readIgpu();          // AMD Radeon 780M perf + VRAM

    // NVIDIA GPU is read asynchronously via QProcess to avoid blocking
        readNvidiaAsync();
    // Agent Pikey TPS (real llama.cpp throughput)
    readTpsAsync();

    // Update all gauges
    // NVIDIA GPU (bottom row, dedicated)
    m_nvGpuGauge->setValue(m_nvGpuUsage);
    m_nvGpuGauge->setSubtitle(
        QString("%1% / %2°C")
            .arg(m_nvGpuUsage, 0, 'f', 0)
            .arg(m_nvGpuTemp, 0, 'f', 0));

    m_nvGpuTempGauge->setValue(m_nvGpuTemp);
    m_nvGpuTempGauge->setSubtitle(QString("%1°C").arg(m_nvGpuTemp, 0, 'f', 0));

    m_nvVramGauge->setValue(m_nvVramGB);
    m_nvVramGauge->setSubtitle(QString("%1 GB").arg(m_nvVramGB, 0, 'f', 1));

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
            if (line.startsWith("MemTotal:"))
                total = line.section(' ', 1, 1).trimmed().toULongLong();
            else if (line.startsWith("MemAvailable:"))
                available = line.section(' ', 1, 1).trimmed().toULongLong();
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

// ── Network via /proc/net/dev (per-interface throughput) ──────
void SystemMonitorV2::readNetwork() {
    // Per-interface byte counters, tracked across ticks
    static QMap<QString, unsigned long long> prevRx;
    static QMap<QString, unsigned long long> prevTx;

    unsigned long long wanRx = 0, wanTx = 0, lanRx = 0, lanTx = 0;

    QFile f("/proc/net/dev");
    if (f.open(QFile::ReadOnly)) {
        while (!f.atEnd()) {
            QString line = QString::fromUtf8(f.readLine()).trimmed();
            int colon = line.indexOf(':');
            if (colon < 0) continue;
            QString iface = line.left(colon).trimmed();
            auto parts = line.mid(colon + 1).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() < 9) continue;
            unsigned long long rx = parts[0].toULongLong();
            unsigned long long tx = parts[8].toULongLong();

            if (iface == m_wanIface) { wanRx = rx; wanTx = tx; }
            else if (iface == m_lanIface) { lanRx = rx; lanTx = tx; }
        }
    }

    double intervalSec = 0.25;  // 250ms tick

    if (prevRx.contains(m_wanIface)) {
        double dRx = static_cast<double>(wanRx - prevRx[m_wanIface]);
        double dTx = static_cast<double>(wanTx - prevTx[m_wanIface]);
        m_wanDown = dRx / 1'000'000.0 / intervalSec;   // MB/s
        m_wanUp   = dTx / 1'000'000.0 / intervalSec;
        m_cumWanRx += static_cast<unsigned long long>(dRx);
        m_cumWanTx += static_cast<unsigned long long>(dTx);
    }
    if (prevRx.contains(m_lanIface)) {
        double dRx = static_cast<double>(lanRx - prevRx[m_lanIface]);
        double dTx = static_cast<double>(lanTx - prevTx[m_lanIface]);
        m_lanDown = dRx / 1'000'000.0 / intervalSec;    // MB/s
        m_lanUp   = dTx / 1'000'000.0 / intervalSec;
    }

    prevRx[m_wanIface] = wanRx; prevTx[m_wanIface] = wanTx;
    prevRx[m_lanIface] = lanRx; prevTx[m_lanIface] = lanTx;
}

// ── NVIDIA GPU (async via nvidia-smi) ──────────────────────────
void SystemMonitorV2::readNvidiaAsync() {
    if (!m_nvidiaProc) {
        m_nvidiaProc = new QProcess(this);
        connect(m_nvidiaProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(m_nvidiaProc->readAllStandardOutput().trimmed());
                    if (!out.isEmpty()) {
                        QStringList parts = out.split(',');
                        if (parts.size() >= 2) {
                            m_nvGpuUsage = parts[0].trimmed().toDouble();
                            m_nvGpuTemp  = parts[1].trimmed().toDouble();
                        }
                        if (parts.size() >= 3) {
                            m_nvVramGB = parts[2].trimmed().toDouble() / 1024.0;  // MiB → GB
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
            {"--query-gpu=utilization.gpu,temperature.gpu,memory.used",
             "--format=csv,noheader,nounits"});
    }
}

// ── llama.cpp TPS (async via measure_tps.py) ──────────────────
void SystemMonitorV2::readTpsAsync() {
    if (!m_tpsProc) {
        m_tpsProc = new QProcess(this);
        connect(m_tpsProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) {
                    QString out = QString::fromUtf8(m_tpsProc->readAllStandardOutput().trimmed());
                    bool ok = false;
                    double tps = out.toDouble(&ok);
                    if (ok) m_nvGpuTps = tps;
                    m_tpsPending = false;
                    m_tpsProc->deleteLater();
                    m_tpsProc = nullptr;
                });
    }
    if (!m_tpsPending) {
        m_tpsPending = true;
        // Script lives alongside the source tree; resolve from project dir.
        QString script = "/run/media/sfarrant/Storage/Development/cpp/sysmonv2/measure_tps.py";
        m_tpsProc->start("python3", {script});
    }
}