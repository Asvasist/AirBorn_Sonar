"""Hidden Tk smoke tests. No serial ports or network connections are opened."""
from pathlib import Path
import sys
import tkinter as tk
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from sonar_gui import SonarApp
from test_gui_control import status


class FakeWorker:
    def __init__(self):
        self.sent = []
        self.stopped = False
    def send(self, command):
        self.sent.append(command)
    def is_alive(self):
        return not self.stopped
    def stop(self):
        self.stopped = True


class GuiViewTests(unittest.TestCase):
    def setUp(self):
        try:
            self.root = tk.Tk()
        except tk.TclError as error:
            self.skipTest(f"Tk display unavailable: {error}")
        self.root.withdraw()
        with patch.object(SonarApp, "refresh_ports"):
            self.app = SonarApp(self.root)
        self.app.serial_worker = FakeWorker()
        self.app.ethernet_worker = FakeWorker()

    def tearDown(self):
        if hasattr(self, "app"):
            self.root.after_cancel(self.app.timer)
        if hasattr(self, "root"):
            self.root.destroy()

    def test_view_command_path_and_receiver_remains_after_stop(self):
        app = self.app
        app.handle("serial", True)
        app.handle("ethernet", True)
        app.handle("line", status())
        app.render()
        self.assertEqual(str(app.start_button["state"]), "normal")
        app.steps.set("45")
        app.start()
        app.handle("line", "Enter signed microstep count, then Enter:")
        app.handle("line", "Step count stored for every cycle.")
        app.handle("line", status(steps=45))
        app.handle("line", status("ACQUIRE", steps=45, used=1))
        self.assertEqual(app.serial_worker.sent, [b"s", b"n", b"45\r", b"r"])
        app.stop()
        self.assertEqual(app.serial_worker.sent[-1], b"x")
        self.assertFalse(app.ethernet_worker.stopped)
        app.handle("line", status("STOPPED", steps=45, used=1))
        app.request_disconnect()
        self.assertTrue(app.exit_pending)
        self.assertFalse(app.ethernet_worker.stopped)
        app.handle("line", status("STOPPED", steps=45, used=0))
        self.root.after_cancel(app.timer)
        app.poll()
        self.assertIsNone(app.serial_worker)
        self.assertIsNone(app.ethernet_worker)

    def test_status_and_saved_record_render(self):
        app = self.app
        app.handle("record", (r"D:\capture_1.bin", {"payload_bytes": 15000, "cycle": 1, "rx_nominal_us": 3125}))
        app.render()
        self.assertIn("15,000 bytes", app.capture_text.get())
        self.assertIn("3.125 ms", app.detail_text.get())
        self.assertEqual(str(app.start_button["state"]), "disabled")

    def test_default_layout_fits_window(self):
        self.root.update_idletasks()
        self.assertLessEqual(self.root.winfo_reqwidth(), 1000)
        self.assertLessEqual(self.root.winfo_reqheight(), 760)


if __name__ == "__main__":
    unittest.main()
