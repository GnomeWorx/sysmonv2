#ifndef SYSTEMMONITORV2_H
#define SYSTEMMONITORV2_H

#include <QMainWindow>
#include <QTimer>
#include <QLabel>
#include <QProcess>
#include <QGridLayout>
#include <QScrollArea>
#include <QVector>
#include <QJsonObject>
#include <deque>
#include "SteamGauge.h"

/// SysmonV2 — Steampunk analog-dial system monitor.
class SystemMonitorV2 : public QMainWindow {
    Q_OBJECT

public:
    explicit SystemMonitorV2(QWidget *parent = nullptr);
    ~SystemMonitorV2() override;

private slots:
    void tick();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void setupUI();
    void setupStyle();
    void readCPU();
    void readRAM();
    void readSensors();
    void readNetwork();

    // ── Data ──
    double m_cpuUsage = 0.0;
    double m_cpuTemp = 0.0;
    double m_gpuUsage = 0.0;
    double m_gpuTemp = 0.0;
    double m_nvmeTemp = 0.0;
    double m_igpuTemp = 0.0;
    double m_dimmATemp = 0.0;
    double m_dimmBTemp = 0.0;
    double m_ethTemp = 0.0;
    double m_chassisTemp = 0.0;
    double m_ramGB = 0.0;
    double m_ramTotalGB = 0.0;
    double m_wanDown = 0.0;
    double m_wanUp = 0.0;
    double m_lanDown = 0.0;
    double m_lanUp = 0.0;

    unsigned long long m_prevIdle = 0, m_prevTotal = 0;

    struct Conn { unsigned long long rx, tx; };
    std::map<QString, Conn> m_prevConns;
    unsigned long long m_cumWanRx = 0, m_cumWanTx = 0;
    unsigned long long m_cumLanRx = 0, m_cumLanTx = 0;

    // ── UI elements ──
    SteamGauge *m_cpuGauge = nullptr;
    SteamGauge *m_gpuGauge = nullptr;
    SteamGauge *m_wanGauge = nullptr;
    SteamGauge *m_lanGauge = nullptr;
    SteamGauge *m_ramGauge = nullptr;
    SteamGauge *m_cpuTempGauge = nullptr;
    SteamGauge *m_igpuTempGauge = nullptr;
    SteamGauge *m_nvmeTempGauge = nullptr;
    SteamGauge *m_dimmATempGauge = nullptr;
    SteamGauge *m_dimmBTempGauge = nullptr;
    SteamGauge *m_ethTempGauge = nullptr;
    SteamGauge *m_chassisGauge = nullptr;

    // ── Timer ──
    QTimer *m_tickTimer = nullptr;
};

#endif // SYSTEMMONITORV2_H
