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
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QCloseEvent>
#include <QSettings>
#include <deque>
#include <map>
#include <QString>
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
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;

private:
    void saveState();
    void loadState();
    void setupUI();
    void setupStyle();
    void readCPU();
    void readNvidiaLocalGpu();    // RTX 4060 Ti via nvidia-smi
    void readIgpu();       // AMD Radeon 780M perf + VRAM
    void readRAM();
    void readSensors();
    void readNetwork();
    void readTpsAsync();       // live llama.cpp TPS: auto-detect backend, probe directly
    void discoverTpsEndpoint(); // find the local llama-server backend port once at startup
    void discoverSensors();
    void setDetached(bool on);  // hide all rows except the NVIDIA/local-GPU row
    void discoverNetworkIfaces();

    // ── Data ──
    double m_cpuUsage = 0.0;
    double m_cpuTemp = 0.0;
    double m_gpuUsage = 0.0;
    double m_gpuTemp = 0.0;
    double m_gpuVramGB = 0.0;
    double m_nvGpuUsage = 0.0;
    double m_nvGpuTemp = 0.0;
    double m_nvGpuTps = 0.0;  // measured tokens/sec of the live local model

    // ── Live local-LLM state (read from the running llama-server) ──
    QString m_llmModel;       // model id reported by the live server (/v1/models)
    bool m_tpsBusy = false;   // true while a probe request is in flight
    int m_tpsProbeCounter = 0; // tick counter — probe every 20 ticks (~5s)
    QString m_tpsApiUrl;      // auto-detected llama-server backend base URL
    QString m_tpsApiKey;      // auto-detected API key (LM Studio sets one)
    double m_tpsLastGood = 0.0; // sticky TPS — keep last good while probing
    double m_nvVramGB = 0.0;
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

    // Discovered sensor device paths (populated once at startup)
    QString m_cpuTempPath;        // k10temp temp1_input
    QString m_igpuTempPath;       // amdgpu temp1_input
    QString m_nvmeTempPath;       // nvme temp1_input
    QString m_dimmATempPath;      // first spd5118 temp1_input
    QString m_dimmBTempPath;      // second spd5118 temp1_input
    QString m_chassisTempPath;    // acpitz temp1_input
    QString m_igpuCardPath;       // /sys/class/drm/cardN/device
    QString m_wanIface = "wlp2s0";
    QString m_lanIface = "enp1s0";

    unsigned long long m_prevIdle = 0, m_prevTotal = 0;

    unsigned long long m_cumWanRx = 0, m_cumWanTx = 0;

    // ── UI elements ──
    SteamGauge *m_cpuGauge = nullptr;
    SteamGauge *m_gpuGauge = nullptr;
    SteamGauge *m_gpuVramGauge = nullptr;
    SteamGauge *m_nvGpuGauge = nullptr;
    SteamGauge *m_nvGpuTempGauge = nullptr;
    SteamGauge *m_wanGauge = nullptr;
    SteamGauge *m_lanGauge = nullptr;
    SteamGauge *m_ramGauge = nullptr;
    SteamGauge *m_cpuTempGauge = nullptr;
    SteamGauge *m_igpuTempGauge = nullptr;
    SteamGauge *m_nvmeTempGauge = nullptr;
    SteamGauge *m_dimmATempGauge = nullptr;
    SteamGauge *m_dimmBTempGauge = nullptr;
    SteamGauge *m_chassisGauge = nullptr;
    SteamGauge *m_clockGauge = nullptr;
    SteamGauge *m_nvTpsGauge = nullptr;  // tokens per second gauge
    SteamGauge *m_nvVramGauge = nullptr;

    // ── Top-level layout rows (for detach/attach toggle) ──
    QWidget *m_detachBar = nullptr;     // always-visible toolbar with the toggle button
    QWidget *m_titleBar = nullptr;      // brass title plate row
    QWidget *m_topRivetRow = nullptr;   // top rivet row
    QWidget *m_clockRow = nullptr;      // clock row
    QWidget *m_gaugeGridW = nullptr;    // wrapper holding the 3x4 gauge grid
    QWidget *m_nvRow = nullptr;         // NVIDIA/local-GPU row (kept visible when detached)
    QWidget *m_botRivetRow = nullptr;   // bottom rivet row
    QPushButton *m_detachBtn = nullptr;
    bool m_detached = false;

#ifdef HERMES_VERIFY_FRIEND
    friend class HermesVerifyDetach;
#endif

    // ── Timer ──
    QTimer *m_tickTimer = nullptr;
    QNetworkAccessManager *m_net = nullptr;
};

#endif // SYSTEMMONITORV2_H