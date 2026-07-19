// QTest harness for sysmonv2 network parsing + unit conversion.
// Verifies:
//   1. parseProcNetDev() extracts rx/tx for every interface.
//   2. Columns are read correctly (rx = field[0], tx = field[8]).
//   3. Malformed / header lines are skipped without crashing.
//   4. mbPerSecToMbps() converts MB/s -> Mbps (x8).
//   5. A known-good synthetic sample produces the expected gauge value.
//   6. Wide formatting (tabs, irregular spacing) is handled.
//   7. Zero counters parse correctly.
//   8. Empty input produces empty map.
//   9. Interface names with embedded colons split on first colon only.
//  10. 64-bit counter values (including wrap-around sizes) parse.
//  11. mbPerSecToMbps edge cases (negative, zero, fractional).
//  12. Header lines without a colon are explicitly skipped.
//
// This is a pure-logic test — it does NOT touch the real /proc/net/dev
// or require a display server.

#include "netmon_parser.h"
#include <QTest>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

class NetmonTest : public QObject {
    Q_OBJECT
private slots:
    void parsesAllInterfaces();
    void readsRxTxColumns();
    void skipsHeaderAndMalformed();
    void convertsMbps();
    void syntheticSampleE2E();
    void parsesWideFormatting();
    void parsesZeroCounters();
    void handlesEmptyInput();
    void handlesInterfaceNameWithSpaces();
    void handlesRxTxWrapping();
    void mbPerSecToMbpsEdgeCases();
    void parseProcNetDevSkipsLinesWithoutColon();
};

static QString sampleProcNetDev() {
    return
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:    1000      10    0    0    0     0          0         0     1000      10    0    0    0     0       0          0\n"
        " wlp2s0: 21465169   35233    0    2    0     0          0         0 10214415   26548    0    0    0     0       0          0\n"
        " enp1s0:  5000000    4000    0    0    0     0          0         0  2500000    2000    0    0    0     0       0          0\n";
}

void NetmonTest::parsesAllInterfaces() {
    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(sampleProcNetDev(), out);
    QCOMPARE(out.size(), 3);
    QVERIFY(out.contains("lo"));
    QVERIFY(out.contains("wlp2s0"));
    QVERIFY(out.contains("enp1s0"));
}

void NetmonTest::readsRxTxColumns() {
    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(sampleProcNetDev(), out);
    // wlp2s0: rx=21465169 (field 0), tx=10214415 (field 8)
    QCOMPARE(out["wlp2s0"].first, 21465169ULL);
    QCOMPARE(out["wlp2s0"].second, 10214415ULL);
    // enp1s0: rx=5000000, tx=2500000
    QCOMPARE(out["enp1s0"].first, 5000000ULL);
    QCOMPARE(out["enp1s0"].second, 2500000ULL);
}

void NetmonTest::skipsHeaderAndMalformed() {
    QString garbage =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes packets ...\n"          // header line, no colon -> skipped
        "halfline no colon here\n"             // no colon -> skipped
        "  badiface: 1 2 3\n"                  // <9 fields -> skipped
        " good0: 111 0 0 0 0 0 0 0 222 0 0 0 0 0 0 0\n";
    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(garbage, out);
    QCOMPARE(out.size(), 1);
    QVERIFY(out.contains("good0"));
    QCOMPARE(out["good0"].first, 111ULL);
    QCOMPARE(out["good0"].second, 222ULL);
}

void NetmonTest::convertsMbps() {
    QCOMPARE(mbPerSecToMbps(0.0), 0.0);
    QCOMPARE(mbPerSecToMbps(1.0), 8.0);
    QCOMPARE(mbPerSecToMbps(12.5), 100.0);   // 12.5 MB/s == 100 Mbps
    QCOMPARE(mbPerSecToMbps(25.0), 200.0);   // pin of WAN dial
}

void NetmonTest::syntheticSampleE2E() {
    // Simulate two ticks 0.25s apart on wlp2s0 with a 25 MB transfer.
    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(sampleProcNetDev(), out);
    unsigned long long rx0 = out["wlp2s0"].first;

    // Second tick: +25 MB on the wire.
    QString t1 = sampleProcNetDev();
    t1.replace("21465169", "46465169");   // +25,000,000 bytes
    parseProcNetDev(t1, out);
    unsigned long long rx1 = out["wlp2s0"].first;

    double dRx = static_cast<double>(rx1 - rx0);
    double intervalSec = 0.25;
    double mbPerSec = dRx / 1'000'000.0 / intervalSec;     // 100 MB/s
    double mbps = mbPerSecToMbps(mbPerSec);                // 800 Mbps

    QCOMPARE(dRx, 25000000.0);
    QCOMPARE(mbPerSec, 100.0);
    QCOMPARE(mbps, 800.0);
    // 800 Mbps would peg the 200-Mbps WAN dial — confirms the unit was the bug.
    QVERIFY(mbps > 200.0);
}

// --- New comprehensive tests ---

void NetmonTest::parsesWideFormatting() {
    // /proc/net/dev lines can use tabs, multiple spaces, and irregular spacing.
    // The parser uses QRegularExpression("\\s+") to split, so it should handle all.
    QString input =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "wlp2s0:\t21465169\t35233\t0\t2\t0\t0\t0\t0\t10214415\t26548\t0\t0\t0\t0\t0\t0\n"
        "enp1s0:   5000000    4000  0  0  0  0  0  0   2500000   2000  0  0  0  0  0  0\n";

    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(input, out);
    QCOMPARE(out.size(), 2);
    QVERIFY(out.contains("wlp2s0"));
    QVERIFY(out.contains("enp1s0"));
    QCOMPARE(out["wlp2s0"].first, 21465169ULL);
    QCOMPARE(out["wlp2s0"].second, 10214415ULL);
    QCOMPARE(out["enp1s0"].first, 5000000ULL);
    QCOMPARE(out["enp1s0"].second, 2500000ULL);
}

void NetmonTest::parsesZeroCounters() {
    // Interface with all zero counters — should parse just fine.
    QString input =
        " test0: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";

    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(input, out);
    QCOMPARE(out.size(), 1);
    QVERIFY(out.contains("test0"));
    QCOMPARE(out["test0"].first, 0ULL);
    QCOMPARE(out["test0"].second, 0ULL);
}

void NetmonTest::handlesEmptyInput() {
    // Empty string should produce an empty map without crashing.
    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev("", out);
    QCOMPARE(out.size(), 0);
}

void NetmonTest::handlesInterfaceNameWithSpaces() {
    // Though unlikely in practice, test that the parser splits on the FIRST colon
    // in the line. If a line has multiple colons (e.g. a malformed interface name
    // with an extra colon), the parser treats everything before the first colon as
    // the interface name and everything after as the counter fields.
    // In this case the counters start with a non-numeric "if" so no valid parse
    // occurs — the parser correctly drops the line rather than misidentifying it.
    QString input =
        "weird:if: 12345 0 0 0 0 0 0 0 67890 0 0 0 0 0 0 0\n";

    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(input, out);
    // The parser splits on the first colon: iface="weird", counters start with "if:".
    // Since "if" is not a valid unsigned long long, the line is skipped entirely.
    QCOMPARE(out.size(), 0);

    // However, a normal interface name with leading/trailing whitespace IS handled.
    QString normalInput =
        "   eth0  :  1000 0 0 0 0 0 0 0  500 0 0 0 0 0 0 0\n";
    out.clear();
    parseProcNetDev(normalInput, out);
    QCOMPARE(out.size(), 1);
    QVERIFY(out.contains("eth0"));
    QCOMPARE(out["eth0"].first, 1000ULL);
    QCOMPARE(out["eth0"].second, 500ULL);
}

void NetmonTest::handlesRxTxWrapping() {
    // 64-bit counters on busy interfaces can exceed 32-bit range.
    // Test with values near UINT64_MAX and large values.
    // "bigif" rx=18446744073709551615 (UINT64_MAX), tx=10000000000000000000
    QString input =
        " bigif: 18446744073709551615 0 0 0 0 0 0 0 10000000000000000000 0 0 0 0 0 0 0\n";

    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(input, out);
    QCOMPARE(out.size(), 1);
    QVERIFY(out.contains("bigif"));
    QCOMPARE(out["bigif"].first, 18446744073709551615ULL);
    QCOMPARE(out["bigif"].second, 10000000000000000000ULL);
}

void NetmonTest::mbPerSecToMbpsEdgeCases() {
    // Negative value: the function doesn't guard against it, but it should
    // still multiply correctly (negative * 8 = negative).
    QCOMPARE(mbPerSecToMbps(-1.0), -8.0);
    QCOMPARE(mbPerSecToMbps(-0.5), -4.0);

    // Zero: already tested above, but reaffirm.
    QCOMPARE(mbPerSecToMbps(0.0), 0.0);

    // Fractional: 0.125 MB/s = 1 Mbps
    QCOMPARE(mbPerSecToMbps(0.125), 1.0);

    // Very small fraction
    QCOMPARE(mbPerSecToMbps(0.001), 0.008);

    // Large value
    QCOMPARE(mbPerSecToMbps(125.0), 1000.0);
}

void NetmonTest::parseProcNetDevSkipsLinesWithoutColon() {
    // Dedicated test for header lines that have no colon — these must be skipped.
    // The /proc/net/dev header has two lines without a colon before the data lines.
    QString input =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
        "    lo:  1000  10  0  0  0  0  0  0  1000  10  0  0  0  0  0  0\n";

    QMap<QString, QPair<unsigned long long, unsigned long long>> out;
    parseProcNetDev(input, out);
    // Only the "lo" line with a colon should be parsed; the two header lines skipped.
    QCOMPARE(out.size(), 1);
    QVERIFY(out.contains("lo"));
    QCOMPARE(out["lo"].first, 1000ULL);
    QCOMPARE(out["lo"].second, 1000ULL);

    // Also verify that entirely whitespace-only lines (after trimming) are harmless.
    QString withBlank =
        "    lo:  1000  10  0  0  0  0  0  0  1000  10  0  0  0  0  0  0\n"
        "\n"
        "  \n"
        "eth0: 500 1 0 0 0 0 0 0 250 1 0 0 0 0 0 0\n";
    out.clear();
    parseProcNetDev(withBlank, out);
    QCOMPARE(out.size(), 2);
    QVERIFY(out.contains("lo"));
    QVERIFY(out.contains("eth0"));
}

QTEST_MAIN(NetmonTest)
#include "test_netmon.moc"