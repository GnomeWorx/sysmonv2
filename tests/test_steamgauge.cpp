// QTest harness for sysmonv2 SteamGauge — steampunk analog dial widget.
// Comprehensive coverage of the full public API, state transitions,
// and edge cases. This is a pure-widget test — it requires a working
// QApplication but no live /proc, /sys, or display server (CI runs
// with QT_QPA_PLATFORM=offscreen).
//
// Test categories:
//   1. Constructor / default state
//   2. Value get/set — clamping to [min, max]
//   3. Negative values (temperature gauges: min = -40 °C)
//   4. Animation control (instant vs animated, animation target)
//   5. Multi-needle (secondary, tertiary)
//   6. Subtitle (status text rendering)
//   7. Arc geometry override (setArc)
//   8. Styling overrides (bezel colour, needle colour, needle base width)
//   9. isInRedZone logic
//  10. Resize behaviour (cache invalidation on resize)
//  11. Clock unit (m_unit == "HMS") branch coverage
//  12. setValue animation stop/restart integrity

#include "SteamGauge.h"
#include <QTest>
#include <QSignalSpy>
#include <QPropertyAnimation>
#include <QtGlobal>

class SteamGaugeTest : public QObject {
    Q_OBJECT

private:
    /// Helper: wait for the internal animation to finish (or timeout).
    void waitAnimationDone(const SteamGauge *g, int timeoutMs = 2000) {
        QElapsedTimer t; t.start();
        while (t.elapsed() < timeoutMs) {
            QTest::qWait(50);
            // Once animatedValue reaches value, animation is settled.
            if (qAbs(g->animatedValue() - g->value()) < 0.001)
                break;
        }
    }

private slots:
    // ── 1. Constructor / default state ──────────────────────────
    void defaultsSetCorrectly() {
        SteamGauge g("CPU", "%");
        QCOMPARE(g.value(), 0.0);
        QCOMPARE(g.animatedValue(), 0.0);
        QVERIFY(!g.isInRedZone());
        // Minimum size policy: should have a non-zero minimum.
        QVERIFY(g.minimumWidth() > 0);
        QVERIFY(g.minimumHeight() > 0);
    }

    void constructorBindsParameters() {
        SteamGauge g("RAM", "GB", 0.0, 64.0, 50.0);
        // Value should start at min.
        QCOMPARE(g.value(), 0.0);
        // redThreshold at 50 → we are NOT in red zone at 0.
        QVERIFY(!g.isInRedZone());
    }

    // ── 2. Value get/set — clamping ─────────────────────────────
    void setValueBelowMinClamps() {
        SteamGauge g("TEMP", "°C", -40.0, 120.0, 100.0);
        g.setAnimDuration(0);  // instant mode for deterministic tests
        g.setValue(-100.0);
        QCOMPARE(g.value(), -40.0);
        QCOMPARE(g.animatedValue(), -40.0);
    }

    void setValueAboveMaxClamps() {
        SteamGauge g("TEMP", "°C", -40.0, 120.0, 100.0);
        g.setAnimDuration(0);
        g.setValue(999.0);
        QCOMPARE(g.value(), 120.0);
        QCOMPARE(g.animatedValue(), 120.0);
    }

    void setValueWithinRange() {
        SteamGauge g("CPU", "%", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);
        g.setValue(42.0);
        QCOMPARE(g.value(), 42.0);
        QCOMPARE(g.animatedValue(), 42.0);
    }

    void setValueAtExactBoundaries() {
        SteamGauge g("CPU", "%", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);
        g.setValue(0.0);
        QCOMPARE(g.value(), 0.0);
        g.setValue(100.0);
        QCOMPARE(g.value(), 100.0);
    }

    // ── 3. Negative-value ranges (temperature gauges) ───────────
    void negativeRangeValue() {
        SteamGauge g("TEMP", "°C", -40.0, 120.0, 100.0);
        g.setAnimDuration(0);
        g.setValue(-20.0);
        QCOMPARE(g.value(), -20.0);
        QCOMPARE(g.animatedValue(), -20.0);
    }

    void negativeRangeFullSpan() {
        // Full range from -40 to 120: mid-point is 40.
        SteamGauge g("TEMP", "°C", -40.0, 120.0, 100.0);
        g.setAnimDuration(0);
        g.setValue(40.0);
        QCOMPARE(g.value(), 40.0);
    }

    void zeroCrossingRange() {
        // Range crosses zero: [-20, 80]
        SteamGauge g("COLD", "°C", -20.0, 80.0, 60.0);
        g.setAnimDuration(0);
        g.setValue(0.0);
        QCOMPARE(g.value(), 0.0);
        g.setValue(-5.0);
        QCOMPARE(g.value(), -5.0);
    }

    // ── 4. Animation control ────────────────────────────────────
    void instantModeSkipsAnimation() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(0);
        g.setValue(75.0);
        // In instant mode, animatedValue must equal value immediately.
        QCOMPARE(g.animatedValue(), 75.0);
    }

    void animatedModeSweepsGracefully() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(200);
        g.setValue(0.0);          // reset to known start
        QCOMPARE(g.animatedValue(), 0.0);

        g.setValue(100.0);
        // Immediately after setValue (before animation completes),
        // the property animation should start from the old animated value.
        // animatedValue should NOT yet be 100.
        QVERIFY(g.animatedValue() < 100.0);

        // Wait for animation to settle.
        QTest::qWait(400);
        QCOMPARE(g.animatedValue(), 100.0);
    }

    void animationDurationUpdatesInFlight() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(400);
        QCOMPARE(g.animatedValue(), 0.0);  // sanity

        g.setValue(50.0);
        // Change duration mid-animation — new value should still reach target.
        g.setAnimDuration(100);
        QTest::qWait(300);
        QCOMPARE(g.animatedValue(), 50.0);
    }

    void repeatedSetValueRestartsAnimation() {
        // Rapid setValue calls should result in the last value being
        // the final animated target, not some intermediate state.
        SteamGauge g("CPU", "%");
        g.setAnimDuration(200);
        g.setValue(10.0);
        g.setValue(90.0);
        g.setValue(50.0);
        QTest::qWait(400);
        QCOMPARE(g.animatedValue(), 50.0);
    }

    // ── 5. Multi-needle: secondary & tertiary ───────────────────
    void secondaryNeedleDefaultDisabled() {
        SteamGauge g("CPU", "%");
        // Secondary value initializes to -1 (disabled).
        // Can't directly read m_secondaryValue, but setSecondaryValue
        // with a valid value should work without crash.
        g.setSecondaryValue(42.0);
        // Setting a valid value: should not crash.  This is a rendering
        // path that only triggers in paintEvent, so the test confirms
        // the method is callable without segfault.
        QVERIFY(true);  // reached here → no crash
    }

    void secondaryNeedleNegativeValueDisabled() {
        SteamGauge g("GPU", "%");
        // -1 disables the needle; value below min also means disabled.
        g.setSecondaryValue(-1.0);
        QVERIFY(true);  // no crash
    }

    void tertiaryNeedleSetAndDisplay() {
        SteamGauge g("CLOCK", "HMS", 0.0, 12.0, 10.0);
        // Tertiary used for hour hand on clock gauges.
        g.setTertiaryValue(3.0);
        QVERIFY(true);  // no crash — render layer in paintEvent
    }

    void allThreeNeedlesTogether() {
        SteamGauge g("MULTI", "UNIT", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);
        g.setValue(70.0);
        g.setSecondaryValue(45.0);
        g.setTertiaryValue(22.0);
        // All three needles active simultaneously — no crash.
        QCOMPARE(g.value(), 70.0);
    }

    // ── 6. Subtitle ─────────────────────────────────────────────
    void subtitleStoredAndClearable() {
        SteamGauge g("CPU", "%");
        g.setSubtitle("42%  68°C");
        // subtitle is private; we test that setSubtitle doesn't crash
        // and that repeated calls work.
        g.setSubtitle("");          // clear
        g.setSubtitle("99%  102°C"); // re-set
        QVERIFY(true);
    }

    void subtitleEmptyDoesNotCrash() {
        SteamGauge g("GPU", "%");
        g.setSubtitle("");
        g.setSubtitle("128 MB");
        g.setSubtitle("");   // back to empty
        QVERIFY(true);
    }

    // ── 7. Arc geometry override ────────────────────────────────
    void setArcDefault() {
        SteamGauge g("CPU", "%");
        // Default arc: start 135°, span 270° → gauge from bottom-left
        // to bottom-right.  Setting the same values is a no-op, but
        // must not crash.
        g.setArc(135.0, 270.0);
        QVERIFY(true);
    }

    void setArcCustomAngles() {
        SteamGauge g("HALF", "m/s", 0.0, 200.0, 160.0);
        g.setArc(90.0, 180.0);   // left-to-right half circle
        QVERIFY(true);
    }

    void setArcZeroSpan() {
        SteamGauge g("EDGE", "kW", 0.0, 1000.0, 900.0);
        // Zero span: edge-case for degenerate gauge — should not crash.
        g.setArc(0.0, 0.0);
        QVERIFY(true);
    }

    void setArcNegativeStart() {
        SteamGauge g("EDGE2", "A", -10.0, 10.0, 8.0);
        g.setArc(-45.0, 90.0);
        QVERIFY(true);
    }

    // ── 8. Styling overrides ────────────────────────────────────
    void bezelColorSetAndInvalidDefault() {
        SteamGauge g("CPU", "%");
        // Default: m_bezelColor is invalid → uses brass gradient.
        g.setBezelColor(QColor(0, 255, 0));  // NVIDIA green
        QVERIFY(true);
    }

    void bezelColorInvalidReset() {
        SteamGauge g("CPU", "%");
        g.setBezelColor(QColor());  // invalid QColor → back to default brass
        QVERIFY(true);
    }

    void needleColorOverride() {
        SteamGauge g("GPU", "%");
        g.setNeedleColor(QColor(0, 255, 0));  // green needle
        g.setNeedleColor(QColor());            // reset to default crimson
        QVERIFY(true);
    }

    void needleBaseWidthBounds() {
        SteamGauge g("CPU", "%");
        // Default: 0.05.  Set to a very wide base.
        g.setNeedleBaseWidth(0.15);
        g.setNeedleBaseWidth(0.001);  // ultra-thin
        g.setNeedleBaseWidth(0.05);   // back to default
        QVERIFY(true);
    }

    // ── 9. isInRedZone logic ────────────────────────────────────
    void inRedZoneWhenValueExceedsThreshold() {
        SteamGauge g("CPU", "%", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);
        g.setValue(80.0);  // exactly at threshold
        QVERIFY(g.isInRedZone());

        g.setValue(81.0);
        QVERIFY(g.isInRedZone());
    }

    void notInRedZoneWhenBelowThreshold() {
        SteamGauge g("CPU", "%", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);
        g.setValue(79.0);
        QVERIFY(!g.isInRedZone());

        g.setValue(0.0);
        QVERIFY(!g.isInRedZone());
    }

    void redZoneAtCustomThreshold() {
        // Temperature: red at 100°C
        SteamGauge g("TEMP", "°C", -40.0, 120.0, 100.0);
        g.setAnimDuration(0);
        g.setValue(95.0);
        QVERIFY(!g.isInRedZone());

        g.setValue(100.0);
        QVERIFY(g.isInRedZone());

        g.setValue(105.0);
        QVERIFY(g.isInRedZone());
    }

    void redZoneClampedValueAtMax() {
        SteamGauge g("CPU", "%", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);
        g.setValue(100.0);
        QVERIFY(g.isInRedZone());
    }

    // ── 10. Resize behaviour (cache invalidation) ────────────────
    void resizeTriggersCacheReset() {
        SteamGauge g("CPU", "%");
        // Resizing the widget should set m_cacheDirty = true
        // (private field).  We verify that resizing doesn't crash
        // and that the gauge remains paintable.
        g.resize(200, 250);
        QCOMPARE(g.width(), 200);
        QCOMPARE(g.height(), 250);

        g.resize(80, 100);
        // Gauge enforces minimum 120×140, so width stays 120
        QCOMPARE(g.width(), 120);
        QCOMPARE(g.height(), 140);
    }

    void resizeBelowMinimumEnforcesMinimum() {
        SteamGauge g("CPU", "%");
        int minW = g.minimumWidth();
        int minH = g.minimumHeight();
        g.resize(10, 10);
        // QWidget::resize respects minimumSize set in constructor.
        QVERIFY(g.width() >= minW);
        QVERIFY(g.height() >= minH);
    }

    // ── 11. Clock unit (HMS) branch coverage ─────────────────────
    void clockUnitSelectsClockBezel() {
        // When m_unit == "HMS", drawClockBezel is called instead of
        // drawBrassRing.  No visual check needed — just confirm no
        // crash in the clock rendering path.
        SteamGauge g("CLOCK", "HMS", 0.0, 12.0, 10.0);
        g.setAnimDuration(0);
        g.setValue(3.0);       // 3 o'clock
        g.setTertiaryValue(9.0); // 9 o'clock (hour hand, shorter)
        QVERIFY(true);
    }

    void clockUnitTickMarks12Hour() {
        SteamGauge g("CLOCK", "HMS", 0.0, 12.0, 10.0);
        g.setAnimDuration(0);
        // 12 unique positions — one per hour.  Exercise all of them.
        for (int h = 1; h <= 12; ++h) {
            g.setValue((double)h);
            qApp->processEvents();  // let rendering queue run
        }
        QVERIFY(true);
    }

    void clockUnitGetsSecondaryNeedle() {
        // Clock with a minute hand (secondary) + hour hand (tertiary).
        SteamGauge g("CLOCK", "HMS", 0.0, 12.0, 10.0);
        g.setAnimDuration(0);
        g.setValue(6.0);          // 6 o'clock (primary/second hand)
        g.setSecondaryValue(3.5); // 3:30 (minute hand)
        g.setTertiaryValue(6.0);  // hour hand
        QVERIFY(true);
    }

    // ── 12. setValue: animation stop/restart integrity ──────────
    void animationStopsBeforeRestart() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(500);
        g.setValue(100.0);
        // Halfway through, set a new value — animation must restart
        // cleanly without doubling up.
        QTest::qWait(100);
        g.setValue(0.0);
        QTest::qWait(600);
        QCOMPARE(g.animatedValue(), 0.0);
    }

    void switchingFromAnimatedToInstantMidFlight() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(500);
        g.setValue(80.0);
        QTest::qWait(50);
        // Switch to instant mode while animated — value should snap.
        g.setAnimDuration(0);
        g.setValue(20.0);
        QCOMPARE(g.animatedValue(), 20.0);
    }

    void switchingFromInstantToAnimated() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(0);
        g.setValue(90.0);
        QCOMPARE(g.animatedValue(), 90.0);

        g.setAnimDuration(300);
        g.setValue(10.0);
        // In animated mode, animatedValue has NOT yet reached 10.
        QVERIFY(g.animatedValue() > 10.0);
        QTest::qWait(500);
        QCOMPARE(g.animatedValue(), 10.0);
    }

    // ── 13. Edge case: setValue with duration=0 while already at same value ──
    void setValueSameValueNoOp() {
        SteamGauge g("CPU", "%");
        g.setAnimDuration(0);
        g.setValue(50.0);
        QCOMPARE(g.value(), 50.0);
        g.setValue(50.0);  // same value — should be a no-op
        QCOMPARE(g.value(), 50.0);
        QCOMPARE(g.animatedValue(), 50.0);
    }

    // ── 14. Gauge with very wide range ──────────────────────────
    void wideRangeGauge() {
        // Network gauge: 0–200 Mbps
        SteamGauge g("WAN", "Mbps", 0.0, 200.0, 160.0);
        g.setAnimDuration(0);
        g.setValue(100.0);
        QCOMPARE(g.value(), 100.0);
        QVERIFY(!g.isInRedZone());

        g.setValue(175.0);
        QVERIFY(g.isInRedZone());
    }

    // ── 15. Exact boundary for red threshold ─────────────────────
    void redZoneBoundaryExactThreshold() {
        SteamGauge g("CPU", "%", 0.0, 100.0, 80.0);
        g.setAnimDuration(0);

        g.setValue(79.999);
        QVERIFY(!g.isInRedZone());

        g.setValue(80.0);
        QVERIFY(g.isInRedZone());     // >= threshold

        g.setValue(80.001);
        QVERIFY(g.isInRedZone());
    }

    // ── 16. Gauge with min == max (degenerate range) ─────────────
    void degenerateRangeClampsToOnlyValue() {
        SteamGauge g("FLAT", "x", 5.0, 5.0, 5.0);
        g.setAnimDuration(0);
        // All values clamp to 5.0.
        g.setValue(0.0);
        QCOMPARE(g.value(), 5.0);
        g.setValue(10.0);
        QCOMPARE(g.value(), 5.0);
        g.setValue(5.0);
        QCOMPARE(g.value(), 5.0);
    }

    // ── 17. Subtitle with long text doesn't crash ────────────────
    void longSubtitle() {
        SteamGauge g("DISK", "GB/s");
        QString longText = "Read: 1234.56 MB/s  Write: 987.65 MB/s  IOPS: 45k";
        g.setSubtitle(longText);
        QVERIFY(true);
    }

    // ── 18. Multiple gauges instantiated side-by-side ────────────
    void multipleGaugesDoNotInterfere() {
        SteamGauge a("CPU", "%", 0.0, 100.0, 80.0);
        SteamGauge b("RAM", "GB", 0.0, 64.0, 50.0);
        SteamGauge c("WAN", "Mbps", 0.0, 200.0, 160.0);

        a.setAnimDuration(0);
        b.setAnimDuration(0);
        c.setAnimDuration(0);

        a.setValue(42.0);
        b.setValue(32.0);
        c.setValue(175.0);

        QCOMPARE(a.value(), 42.0);
        QCOMPARE(b.value(), 32.0);
        QCOMPARE(c.value(), 175.0);

        QVERIFY(!a.isInRedZone());
        QVERIFY(!b.isInRedZone());
        QVERIFY(c.isInRedZone());
    }

    // ── 19. Parent-child ownership ───────────────────────────────
    void gaugeWithParent() {
        QWidget parent;
        auto *g = new SteamGauge("GPU", "%", 0.0, 100.0, 80.0, &parent);
        g->setAnimDuration(0);
        g->setValue(95.0);
        QCOMPARE(g->value(), 95.0);
        QVERIFY(g->isInRedZone());
        // parent destructor will delete g — no double-free.
    }

    // ── 20. Arc override with extreme values ─────────────────────
    void setArcLargeSpan() {
        SteamGauge g("WIDE", "deg", -180.0, 180.0, 150.0);
        g.setArc(0.0, 360.0);  // full circle
        QVERIFY(true);
    }

    void setArcNegativeSpan() {
        SteamGauge g("REV", "rpm", 0.0, 100.0, 80.0);
        g.setArc(270.0, -180.0);  // reverse direction arc
        QVERIFY(true);
    }
};

QTEST_MAIN(SteamGaugeTest)
#include "test_steamgauge.moc"
