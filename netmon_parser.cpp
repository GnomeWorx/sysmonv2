#include "netmon_parser.h"
#include <QRegularExpression>

void parseProcNetDev(const QString &text,
                     QMap<QString, QPair<unsigned long long, unsigned long long>> &out)
{
    out.clear();
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        int colon = line.indexOf(':');
        if (colon < 0) continue;                 // header / blank
        QString iface = line.left(colon).trimmed();
        auto parts = line.mid(colon + 1)
                         .split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() < 9) continue;          // malformed line
        bool okRx = false, okTx = false;
        unsigned long long rx = parts[0].toULongLong(&okRx);
        unsigned long long tx = parts[8].toULongLong(&okTx);
        if (okRx && okTx)
            out.insert(iface, qMakePair(rx, tx));
    }
}
