#ifndef NETMON_PARSER_H
#define NETMON_PARSER_H

#include <QString>
#include <QMap>
#include <QPair>

// Parse /proc/net/dev text into per-interface {rxBytes, txBytes}.
// Mirrors the kernel format: each line "iface: rx rxErr ... tx txErr ..."
// where rx is field[0] and tx is field[8] (zero-based) of the
// space-separated tail after the colon.
//
// Exposed as a free function so it can be unit-tested without a live
// SystemMonitorV2 instance or a real /proc filesystem.
void parseProcNetDev(const QString &text,
                     QMap<QString, QPair<unsigned long long, unsigned long long>> &out);

// Convert a throughput given in MB/s to Mbps (×8).
// The WAN/LAN dials are labelled "Mbps", so the gauge value must be bits.
inline double mbPerSecToMbps(double mbPerSec) {
    return mbPerSec * 8.0;
}

#endif // NETMON_PARSER_H
