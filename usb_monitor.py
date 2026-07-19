#!/usr/bin/env python3
"""
USB Serial Device Monitor — PyQt5 + pyudev
Detects all USB serial devices (ttyUSB*, ttyACM*) and monitors
hot-plug / hot-unplug in real time.

Requirements:  pip install PyQt5 pyudev
"""

import sys, os, glob

from PyQt5.QtCore   import Qt, QTimer
from PyQt5.QtGui    import QFont, QPalette, QColor
from PyQt5.QtWidgets import (QApplication, QWidget, QVBoxLayout,
                             QHBoxLayout, QLabel, QListWidget,
                             QListWidgetItem, QPushButton, QDialog,
                             QTextEdit)

try:
    import pyudev
    HAVE_PYUDEV = True
except ImportError:
    HAVE_PYUDEV = False


# ── Scan for serial devices via sysfs ───────────────────────────
def scan_serial_devices():
    """Return list of (device_path, vendor_model) tuples for USB serial devices."""
    devices = []
    for base in ('/dev/ttyUSB*', '/dev/ttyACM*', '/dev/ttyS0'):
        for path in glob.glob(base):
            real = os.path.realpath(path) if os.path.islink(path) else path
            # Try to extract human-readable info via udevadm or sysfs
            info = get_device_info(real)
            devices.append((real, info))
    # Deduplicate by major:minor (same device can appear under multiple names)
    seen = set()
    unique = []
    for dev_path, info in devices:
        try:
            st = os.stat(dev_path)
            key = (st.st_dev, st.st_rdev)
        except OSError:
            key = dev_path
        if key not in seen:
            seen.add(key)
            unique.append((dev_path, info))
    return unique


def get_device_info(dev_path):
    """Return a nice string like 'Arduino Mega 2560 (ttyACM0)' or fall back."""
    try:
        import subprocess
        out = subprocess.check_output(
            ['udevadm', 'info', '--query=property', '--name=' + dev_path],
            stderr=subprocess.DEVNULL, timeout=2, text=True
        )
        props = {}
        for line in out.splitlines():
            if '=' in line:
                k, v = line.split('=', 1)
                props[k.strip()] = v.strip()
        name = props.get('ID_MODEL_FROM_DATABASE') or props.get('ID_MODEL') or ''
        serial = props.get('ID_SERIAL_SHORT', '')
        if name:
            label = name
            if serial:
                label += f' [{serial[:8]}]'
            return label
    except Exception:
        pass
    # Fallback: just show the base name
    return os.path.basename(dev_path)


def fetch_all_udev_props(dev_path):
    """Return a dict of ALL udev properties for a device path."""
    import subprocess
    props = {}
    try:
        # Device properties (ID_*, SUBSYSTEM, etc.)
        out = subprocess.check_output(
            ['udevadm', 'info', '--query=property', '--name=' + dev_path],
            stderr=subprocess.DEVNULL, timeout=2, text=True
        )
        for line in out.splitlines():
            if '=' in line:
                k, v = line.split('=', 1)
                props[k.strip()] = v.strip()

        # Parent USB device info (vendor, product, usb hierarchy)
        out2 = subprocess.check_output(
            ['udevadm', 'info', '--query=all', '--name=' + dev_path],
            stderr=subprocess.DEVNULL, timeout=2, text=True
        )
        for line in out2.splitlines():
            if line.startswith('P: '):
                props['UDEV_PATH'] = line[3:].strip()
            elif line.startswith('E: '):
                kv = line[3:].strip()
                if '=' in kv:
                    k, v = kv.split('=', 1)
                    # Don't overwrite device-level props with duplicate keys
                    if k not in props:
                        props[k] = v
            elif line.startswith('S: '):
                symlinks = props.get('UDEV_SYMLINKS', '')
                symlinks += line[3:].strip() + '  '
                props['UDEV_SYMLINKS'] = symlinks

        # Try to get USB topology info
        try:
            # Walk up the device tree to find USB vendor/product
            import glob as gg
            # Reconstruct typical path to USB device info
            real = os.path.realpath('/sys/class/tty/' + os.path.basename(dev_path))
            parts = real.split('/')
            for i, p in enumerate(parts):
                if p.startswith('usb'):
                    usb_path = '/'.join(parts[:i+1])
                    for udev_f in ['idVendor', 'idProduct', 'manufacturer',
                                    'product', 'serial', 'bcdDevice', 'speed']:
                        f_path = os.path.join(usb_path, udev_f)
                        if os.path.isfile(f_path):
                            try:
                                with open(f_path) as fh:
                                    val = fh.read().strip()
                                    props['USB_' + udev_f.upper()] = val
                            except OSError:
                                pass
                    break
        except Exception:
            pass

    except subprocess.TimeoutExpired:
        props['ERROR'] = 'udevadm timed out'
    except subprocess.CalledProcessError as e:
        props['ERROR'] = f'udevadm failed: {e}'
    except FileNotFoundError:
        props['ERROR'] = 'udevadm not found'

    return props


# ── Drill-down Detail Dialog ────────────────────────────────────
class DeviceDetailDialog(QDialog):
    """Modal dialog showing all udev properties for a serial device."""
    def __init__(self, dev_path, props, parent=None):
        super().__init__(parent)
        self.setWindowTitle(f'🔍 {os.path.basename(dev_path)} Details')
        self.setMinimumSize(580, 500)

        # Steampunk dark theme
        palette = self.palette()
        palette.setColor(QPalette.Window, QColor(25, 12, 4))
        palette.setColor(QPalette.WindowText, QColor(212, 168, 67))
        palette.setColor(QPalette.Base, QColor(15, 7, 2))
        palette.setColor(QPalette.Text, QColor(212, 168, 67))
        self.setPalette(palette)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 12, 12, 12)
        layout.setSpacing(8)

        # Title with device path
        title = QLabel(f'📟 {dev_path}')
        title.setStyleSheet('color: #f5e6b8; font-size: 13px; font-weight: bold;')
        title.setWordWrap(True)
        layout.addWidget(title)

        # Properties text area
        text = QTextEdit()
        text.setReadOnly(True)
        text.setFont(QFont('Consolas', 10))
        text.setStyleSheet("""
            QTextEdit {
                background: #150a03;
                color: #d4a843;
                border: 1px solid #6b4e0a;
                border-radius: 4px;
                padding: 6px;
                selection-background-color: #6b4e0a;
            }
        """)

        # Build rich text
        html = '<pre style="color:#d4a843; font-family:Consolas,monospace;">\n'
        # Group properties by category
        key_order = [
            ('Device', ['DEVNAME', 'UDEV_PATH', 'MAJOR', 'MINOR', 'SUBSYSTEM',
                        'DEVTYPE', 'UDEV_SYMLINKS', 'ERROR']),
            ('Identity', ['ID_VENDOR', 'ID_VENDOR_ID', 'ID_MODEL', 'ID_MODEL_ID',
                          'ID_SERIAL', 'ID_SERIAL_SHORT', 'ID_REVISION',
                          'USB_IDVENDOR', 'USB_IDPRODUCT', 'USB_MANUFACTURER',
                          'USB_PRODUCT', 'USB_SERIAL', 'USB_BCDDEVICE', 'USB_SPEED']),
            ('USB Bus', ['ID_BUS', 'ID_USB_INTERFACES', 'ID_USB_DRIVER',
                         'ID_PATH', 'ID_PATH_TAG', 'DEVPATH']),
            ('Driver', ['DRIVER', 'ID_MM_CANDIDATE', 'ID_VF_SERIAL',
                        'SYSTEMD_READY', 'TAGS']),
            ('Other', []),
        ]

        seen = set()
        for group_name, keys in key_order:
            group_entries = []
            for k in keys:
                if k in props:
                    group_entries.append(f'<b style="color:#b8860b;">{k}</b>  {escape_html(props[k])}')
                    seen.add(k)
            # Add any remaining unseen props to 'Other'
            if group_name == 'Other':
                for k, v in sorted(props.items()):
                    if k not in seen and k not in ('ERROR',):
                        group_entries.append(f'{k}  {escape_html(v)}')
            if group_entries:
                html += f'\n⚙  <span style="color:#f5e6b8;">{group_name}</span>\n'
                html += '─' * 48 + '\n'
                for entry in group_entries:
                    html += '  ' + entry + '\n'

        html += '\n</pre>'
        text.setHtml(html)
        layout.addWidget(text, 1)

        # Close button
        close_btn = QPushButton('Close')
        close_btn.setStyleSheet("""
            QPushButton {
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 #8b6914, stop:0.5 #b8860b, stop:1 #6b4e0a);
                color: #f5e6b8;
                border: 1px solid #d4a843;
                border-radius: 4px;
                padding: 6px 24px;
                font-weight: bold;
                min-width: 100px;
            }
            QPushButton:pressed {
                background: #3d2a06;
            }
        """)
        close_btn.clicked.connect(self.accept)
        layout.addWidget(close_btn, 0, Qt.AlignCenter)


def escape_html(text):
    """Minimal HTML escaping for text display."""
    text = str(text)
    text = text.replace('&', '&amp;')
    text = text.replace('<', '&lt;')
    text = text.replace('>', '&gt;')
    text = text.replace('"', '&quot;')
    return text


# ── Main Monitor Widget ─────────────────────────────────────────
class USBSerialMonitor(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('USB Serial Monitor')
        self.setMinimumSize(520, 380)

        # Dark steampunk-ish palette to match sysmonv2 vibe
        palette = self.palette()
        palette.setColor(QPalette.Window, QColor(30, 14, 5))
        palette.setColor(QPalette.WindowText, QColor(212, 168, 67))
        palette.setColor(QPalette.Base, QColor(20, 10, 3))
        palette.setColor(QPalette.Text, QColor(212, 168, 67))
        palette.setColor(QPalette.Button, QColor(60, 42, 18))
        palette.setColor(QPalette.ButtonText, QColor(212, 168, 67))
        palette.setColor(QPalette.Highlight, QColor(139, 105, 20))
        self.setPalette(palette)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(8)

        # Header
        header = QLabel('🔌 USB Serial Devices')
        header.setAlignment(Qt.AlignCenter)
        f = header.font()
        f.setPointSize(14)
        f.setBold(True)
        header.setFont(f)
        layout.addWidget(header)

        # Device list
        self.device_list = QListWidget()
        self.device_list.setAlternatingRowColors(False)
        self.device_list.setFont(QFont('Consolas', 11))
        self.device_list.setStyleSheet("""
            QListWidget {
                background: #1a0d04;
                color: #d4a843;
                border: 1px solid #8b6914;
                border-radius: 4px;
                padding: 4px;
            }
            QListWidget::item {
                padding: 6px 8px;
                border-bottom: 1px solid #3d2a06;
            }
            QListWidget::item:selected {
                background: #6b4e0a;
            }
        """)
        layout.addWidget(self.device_list, 1)

        # Status bar
        status_row = QHBoxLayout()
        self.status_label = QLabel('Scanning…')
        self.status_label.setFont(QFont('Segoe UI', 9))
        self.status_label.setStyleSheet('color: #a07010;')
        status_row.addWidget(self.status_label, 1)

        refresh_btn = QPushButton('🔄 Refresh')
        refresh_btn.setStyleSheet("""
            QPushButton {
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 #8b6914, stop:0.5 #b8860b, stop:1 #6b4e0a);
                color: #f5e6b8;
                border: 1px solid #d4a843;
                border-radius: 4px;
                padding: 4px 16px;
                font-weight: bold;
            }
            QPushButton:pressed {
                background: #3d2a06;
            }
        """)
        refresh_btn.clicked.connect(self.refresh_devices)
        status_row.addWidget(refresh_btn)

        layout.addLayout(status_row)

        # Double-click drill-down
        self.device_list.itemDoubleClicked.connect(self.show_device_detail)

        # ── Hotplug monitoring ──
        self._poll_timer = QTimer(self)
        self._poll_timer.timeout.connect(self.refresh_devices)

        if HAVE_PYUDEV:
            self._setup_udev_monitor()
            self.status_label.setText('Monitoring via udev (hotplug active)')
        else:
            # Fallback: poll every 2 seconds
            self._poll_timer.start(2000)
            self.status_label.setText('Monitoring via poll (2 s interval)')

        # Initial scan
        self.refresh_devices()

    # ── pyudev Monitor ──────────────────────────────────────────
    def _setup_udev_monitor(self):
        try:
            ctx = pyudev.Context()
            monitor = pyudev.Monitor.from_netlink(ctx)
            monitor.filter_by(subsystem='tty')
            self._udev_observer = pyudev.MonitorObserver(
                monitor, self._on_udev_event, name='usb-serial-mon'
            )
            self._udev_observer.start()
        except Exception as e:
            self.status_label.setText(f'udev monitor failed: {e} — falling back to poll')
            self._poll_timer.start(2000)

    def _on_udev_event(self, device):
        # Debounce: udev fires multiple events per plug; batch with a short timer
        if hasattr(self, '_udev_debounce') and self._udev_debounce.isActive():
            return
        self._udev_debounce = QTimer()
        self._udev_debounce.setSingleShot(True)
        self._udev_debounce.timeout.connect(self.refresh_devices)
        self._udev_debounce.start(300)

    # ── Refresh ─────────────────────────────────────────────────
    def refresh_devices(self):
        devices = scan_serial_devices()
        count = len(devices)

        self.device_list.clear()
        for path, info in devices:
            text = f'  {path:24s}  {info}'
            item = QListWidgetItem(text)
            item.setData(Qt.UserRole, path)  # store full path for drill-down
            item.setToolTip('Double-click for details')
            self.device_list.addItem(item)

        self.status_label.setText(
            f'{count} device{"s" if count != 1 else ""} found'
        )

    # ── Double-click drill-down ────────────────────────────────
    def show_device_detail(self, item):
        path = item.data(Qt.UserRole)
        if not path:
            return
        props = fetch_all_udev_props(path)
        dlg = DeviceDetailDialog(path, props, self)
        dlg.exec()


# ── Entry Point ─────────────────────────────────────────────────
def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')
    w = USBSerialMonitor()
    w.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
