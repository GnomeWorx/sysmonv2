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
    m_tickTimer->start(1000);

    // Read RAM total once
    QFile fp("/proc/meminfo");
    if (fp.open(QFile::ReadOnly)) {
        QTextStream in(&fp);
        QString line;
        while (in.readLineInto(&line)) {
            if (line.startsWith("MemTotal:")) {
                double kb = line.section(' ', 1, 1).trimmed().toDouble();
                m_ramTotalGB = kb / 1048576.0;
                break;
            }
        }
    }
}

SystemMonitorV2::~SystemMonitorV2() = default;

// ── UI Setup ───────────────────────────────────────────────────
void SystemMonitorV2::setupUI() {
    auto *central = new QWidget(this);
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

    // ── Gauge grid (3 rows × 4 cols) ──
    auto *gaugeGrid = new QGridLayout();
    gaugeGrid->setSpacing(5);

    // Row 0: Performance gauges (the original 4)
    // CPU usage %
    m_cpuGauge = new SteamGauge("CPU", "%", 0, 100, 80);
    m_cpuGauge->setSubtitle("-- %");
    gaugeGrid->addWidget(m_cpuGauge, 0, 0);

    // GPU usage % (Radeon 780M iGPU)
    m_gpuGauge = new SteamGauge("GPU", "%", 0, 100, 80);
    m_gpuGauge->setSubtitle("-- %");
    gaugeGrid->addWidget(m_gpuGauge, 0, 1);

    // RAM usage (GB)
    m_ramGauge = new SteamGauge("RAM", "GB", 0, 64, 51);
    m_ramGauge->setSubtitle("-- / -- GB");
    gaugeGrid->addWidget(m_ramGauge, 0, 2);

    // CPU die temp (k10temp Tctl)
    m_cpuTempGauge = new SteamGauge("CPU TEMP", "°C", 0, 100, 80);
    m_cpuTempGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_cpuTempGauge, 0, 3);

    // Row 1: Network + storage temp
    // WAN internet speed
    m_wanGauge = new SteamGauge("WAN", "Mbps", 0, 100, 80);
    m_wanGauge->setSubtitle("↓ --  ↑ --");
    gaugeGrid->addWidget(m_wanGauge, 1, 0);

    // LAN local speed
    m_lanGauge = new SteamGauge("LAN", "Mbps", 0, 100, 80);
    m_lanGauge->setSubtitle("↓ --  ↑ --");
    gaugeGrid->addWidget(m_lanGauge, 1, 1);

    // NVMe boot SSD temp
    m_nvmeTempGauge = new SteamGauge("NVME", "°C", 0, 100, 80);
    m_nvmeTempGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_nvmeTempGauge, 1, 2);

    // iGPU (Radeon 780M) edge temp
    m_igpuTempGauge = new SteamGauge("IGPU TEMP", "°C", 0, 100, 80);
    m_igpuTempGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_igpuTempGauge, 1, 3);

    // Row 2: Enclosure / peripheral temps
    // ACPI chassis temp (acpitz)
    m_chassisGauge = new SteamGauge("CHASSIS", "°C", 0, 50, 40);
    m_chassisGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_chassisGauge, 2, 0);

    // RAM stick A temp (SPD5118 DIMM A)
    m_dimmATempGauge = new SteamGauge("DIMM A", "°C", 0, 85, 68);
    m_dimmATempGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_dimmATempGauge, 2, 1);

    // RAM stick B temp (SPD5118 DIMM B)
    m_dimmBTempGauge = new SteamGauge("DIMM B", "°C", 0, 85, 68);
    m_dimmBTempGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_dimmBTempGauge, 2, 2);

    // Realtek NIC temp (r8169)
    m_ethTempGauge = new SteamGauge("ETHERNET", "°C", 0, 100, 80);
    m_ethTempGauge->setSubtitle("--°C");
    gaugeGrid->addWidget(m_ethTempGauge, 2, 3);

    // Equal row/column stretch so gauges fill the form
    for (int r = 0; r < 3; ++r) gaugeGrid->setRowStretch(r, 1);
    for (int c = 0; c < 4; ++c) gaugeGrid->setColumnStretch(c, 1);

    mainLayout->addLayout(gaugeGrid, 1);

    setMinimumSize(900, 650);
    resize(1000, 760);
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
    readCPU();
    readRAM();
    readSensors();
    readNetwork();

    // Row 0: Performance
    m_cpuGauge->setValue(m_cpuUsage);
    m_cpuGauge->setSubtitle(QString("%1%").arg(m_cpuUsage, 0, 'f', 0));

    m_gpuGauge->setValue(m_gpuUsage);
    m_gpuGauge->setSubtitle(QString("%1%").arg(m_gpuUsage, 0, 'f', 0));

    m_ramGauge->setValue(m_ramGB);
    m_ramGauge->setSubtitle(
        QString("%1 / %2 GB")
            .arg(m_ramGB, 0, 'f', 1)
            .arg(m_ramTotalGB, 0, 'f', 0));

    m_cpuTempGauge->setValue(m_cpuTemp);
    m_cpuTempGauge->setSubtitle(QString("%1°C").arg(m_cpuTemp, 0, 'f', 0));

    // Row 1: Network + storage/graphics temps
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

    m_nvmeTempGauge->setValue(m_nvmeTemp);
    m_nvmeTempGauge->setSubtitle(QString("%1°C").arg(m_nvmeTemp, 0, 'f', 0));

    m_igpuTempGauge->setValue(m_igpuTemp);
    m_igpuTempGauge->setSubtitle(QString("%1°C").arg(m_igpuTemp, 0, 'f', 0));

    // Row 2: Enclosure / peripheral temps
    m_chassisGauge->setValue(m_chassisTemp);
    m_chassisGauge->setSubtitle(QString("%1°C").arg(m_chassisTemp, 0, 'f', 0));

    m_dimmATempGauge->setValue(m_dimmATemp);
    m_dimmATempGauge->setSubtitle(QString("%1°C").arg(m_dimmATemp, 0, 'f', 0));

    m_dimmBTempGauge->setValue(m_dimmBTemp);
    m_dimmBTempGauge->setSubtitle(QString("%1°C").arg(m_dimmBTemp, 0, 'f', 0));

    m_ethTempGauge->setValue(m_ethTemp);
    m_ethTempGauge->setSubtitle(QString("%1°C").arg(m_ethTemp, 0, 'f', 0));
}

// ── CPU ────────────────────────────────────────────────────────
void SystemMonitorV2::readCPU() {
    QFile f("/proc/stat");
    if (f.open(QFile::ReadOnly)) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith("cpu ")) {
            auto parts = line.split(' ', Qt::SkipEmptyParts);
            if (parts.size() >= 5) {
                unsigned long long user = parts[1].toULongLong();
                unsigned long long nice = parts[2].toULongLong();
                unsigned long long sys = parts[3].toULongLong();
                unsigned long long idle = parts[4].toULongLong();
                unsigned long long total = user + nice + sys + idle;

                if (m_prevTotal > 0) {
                    unsigned long long dIdle = idle - m_prevIdle;
                    unsigned long long dTotal = total - m_prevTotal;
                    m_cpuUsage = (dTotal > 0) ? (1.0 - (double)dIdle / dTotal) * 100.0 : 0.0;
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
        QTextStream in(&f);
        QString line;
        while (in.readLineInto(&line)) {
            if (line.startsWith("MemAvailable:")) {
                double availKb = line.section(' ', 1, 1).trimmed().toDouble();
                double totalKb = m_ramTotalGB * 1048576.0;
                m_ramGB = (totalKb - availKb) / 1048576.0;
                break;
            }
        }
    }
}

// ── Sensors ────────────────────────────────────────────────────
void SystemMonitorV2::readSensors() {
    // Read CPU temp from k10temp hwmon
    m_cpuTemp = 0.0;
    for (int h = 0; h < 10; ++h) {
        QFile nf(QString("/sys/class/hwmon/hwmon%1/name").arg(h));
        if (!nf.open(QFile::ReadOnly)) continue;
        QString name = QString::fromUtf8(nf.readAll()).trimmed();
        nf.close();

        if (name == "k10temp") {
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (tf.open(QFile::ReadOnly)) {
                m_cpuTemp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
                tf.close();
            }
        }
        else if (name == "nvme") {
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (tf.open(QFile::ReadOnly)) {
                m_nvmeTemp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
                tf.close();
            }
        }
        else if (name == "amdgpu") {
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (tf.open(QFile::ReadOnly)) {
                m_igpuTemp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
                tf.close();
            }
        }
        else if (name == "acpitz") {
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (tf.open(QFile::ReadOnly)) {
                m_chassisTemp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
                tf.close();
            }
        }
        else if (name == "r8169" || name.contains("r8169")) {
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (tf.open(QFile::ReadOnly)) {
                m_ethTemp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
                tf.close();
            }
        }
    }

    // Read DIMM temps from SPD5118 (hwmon4 = DIMM A, hwmon5 = DIMM B)
    for (int h = 0; h < 10; ++h) {
        QFile nf(QString("/sys/class/hwmon/hwmon%1/name").arg(h));
        if (!nf.open(QFile::ReadOnly)) continue;
        QString name = QString::fromUtf8(nf.readAll()).trimmed();
        nf.close();

        if (name == "spd5118") {
            // Read the i2c address to distinguish DIMM A vs B
            QString devPath = QFileInfo(QString("/sys/class/hwmon/hwmon%1").arg(h)).canonicalPath();
            QFile tf(QString("/sys/class/hwmon/hwmon%1/temp1_input").arg(h));
            if (!tf.open(QFile::ReadOnly)) continue;
            double temp = QString::fromUtf8(tf.readAll()).trimmed().toDouble() / 1000.0;
            tf.close();

            // The hwmon numbering is stable: first spd5118 = DIMM A, second = DIMM B
            if (m_dimmATemp == 0.0) {
                m_dimmATemp = temp;
            } else if (m_dimmBTemp == 0.0) {
                m_dimmBTemp = temp;
            }
        }
    }

    // GPU usage placeholder — read from amdgpu power/volt if available
    m_gpuUsage = 0.0;
    m_gpuTemp = m_igpuTemp;
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

                        bool isPrivate = false;
                        if (peerAddr == "127.0.0.1" || peerAddr == "::1")
                            continue;
                        if (peerAddr.startsWith("10.") ||
                            peerAddr.startsWith("192.168.") ||
                            (peerAddr.startsWith("172.") && peerAddr.section('.', 1, 1).toInt() >= 16 && peerAddr.section('.', 1, 1).toInt() <= 31) ||
                            peerAddr.startsWith("fc") || peerAddr.startsWith("fd") ||
                            peerAddr.startsWith("fe80"))
                            isPrivate = true;

                        double speedMbpsRx = dRx * 8.0 / 1000000.0;
                        double speedMbpsTx = dTx * 8.0 / 1000000.0;

                        if (isPrivate) {
                            lanDown += speedMbpsRx;
                            lanUp += speedMbpsTx;
                            m_cumLanRx += dRx;
                            m_cumLanTx += dTx;
                        } else {
                            wanDown += speedMbpsRx;
                            wanUp += speedMbpsTx;
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
