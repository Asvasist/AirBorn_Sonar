"""Exercise the desktop transports with fake ports and sockets only."""
from pathlib import Path
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from sonar_gui_transport import EthernetWorker, LineDecoder, SerialWorker
from test_ethernet import Fragmented, record


class Socket(Fragmented):
    def __init__(self, data):
        super().__init__(data)
        self.closed = False

    def shutdown(self, how):
        self.closed = True

    def close(self):
        self.closed = True


class FastEvent(threading.Event):
    def wait(self, timeout=None):
        return super().wait(0.001)


class GuiTransportTests(unittest.TestCase):
    def args(self, output):
        return SimpleNamespace(output=output, board=None, port=5001, interface="Ethernet", local_ip=None)

    def test_line_fragmentation_and_memory_bound(self):
        parser = LineDecoder()
        self.assertEqual(parser.feed(b"Enter signed microstep"), [])
        self.assertEqual(parser.feed(b" count, then Enter:\r\nOK\npartial"),
                         ["Enter signed microstep count, then Enter:", "OK"])
        self.assertEqual(parser.feed(b" end\n"), ["partial end"])
        with self.assertRaises(ValueError):
            parser.feed(b"a" * 8193)

    def test_serial_stop_discards_pending_start(self):
        writes, events = [], []
        worker = None

        class Port:
            in_waiting = 0
            def __init__(self, **kwargs):
                self.config = kwargs
                self.closed = False
            def open(self):
                self_open.assertFalse(self.dtr)
                self_open.assertFalse(self.rts)
                self_open.assertEqual(self.config["baudrate"], 115200)
            def write(self, value):
                writes.append(value)
                return len(value)
            def read(self, size):
                worker.stop()
                return b"STAGE2 STOPPED\r\n"
            def close(self):
                self.closed = True

        self_open = self
        port = Port(port=None, baudrate=115200)
        worker = SerialWorker("COM_TEST", lambda *e: events.append(e), lambda **k: port)
        worker.send(b"n")
        worker.send(b"20\r")
        worker.send(b"r")
        worker.send(b"x")
        worker.run()
        self.assertEqual(writes, [b"x"])
        self.assertTrue(port.closed)
        self.assertIn(("line", "STAGE2 STOPPED"), events)
        self.assertEqual(events[-1], ("serial", False))
        with self.assertRaises(OSError):
            worker.send(b"r")

    def test_serial_open_error_is_reported(self):
        events = []
        def fail(**kwargs):
            raise PermissionError("COM port already open in Vitis")
        SerialWorker("COM_TEST", lambda *e: events.append(e), fail).run()
        self.assertIn("already open", events[0][1])
        self.assertEqual(events[-1], ("serial", False))

    def test_receiver_reconnects_and_persists_before_event(self):
        with tempfile.TemporaryDirectory() as folder:
            sock = Socket(b"READY2\r\n" + record())
            attempts, events = [], []
            def connector(args, log):
                attempts.append(1)
                if len(attempts) == 1:
                    raise TimeoutError("connection timed out")
                return sock, "test-board", "test-laptop"
            def emit(kind, value):
                events.append((kind, value))
                if kind == "record":
                    self.assertEqual(Path(value[0]).read_bytes(), b"\x01\x80\x02\x40")
                    self.assertTrue(Path(value[0]).with_suffix(".json").is_file())
                    self.assertIn(b"ACK2", sock.sent)
                    worker.stop()
            worker = EthernetWorker(self.args(folder), emit, connector)
            worker.cancel = FastEvent()
            worker.run()
            self.assertEqual(len(attempts), 2)
            self.assertIn(("ethernet", True), events)
            self.assertEqual(len(list(Path(folder).rglob("*.bin"))), 1)
            self.assertTrue(sock.closed)

    def test_bad_payload_not_saved_or_acknowledged(self):
        with tempfile.TemporaryDirectory() as folder:
            sock = Socket(b"READY2\r\n" + record()[:-1] + b"\xff")
            def emit(kind, value):
                if kind == "log" and "checksum" in value:
                    worker.stop()
            worker = EthernetWorker(self.args(folder), emit, lambda args, log: (sock, "board", "local"))
            worker.run()
            self.assertNotIn(b"ACK2", sock.sent)
            self.assertFalse(list(Path(folder).rglob("*.bin")))

    def test_cancel_while_connecting_closes_late_socket_without_handshake(self):
        with tempfile.TemporaryDirectory() as folder:
            sock = Socket(b"READY2\r\n")
            entered, release = threading.Event(), threading.Event()
            def connect(args, log):
                entered.set()
                release.wait(2)
                return sock, "board", "local"
            events = []
            worker = EthernetWorker(self.args(folder), lambda *e: events.append(e), connect)
            worker.start()
            self.assertTrue(entered.wait(2))
            worker.stop()
            release.set()
            worker.join(2)
            self.assertFalse(worker.is_alive())
            self.assertTrue(sock.closed)
            self.assertEqual(sock.sent, b"")
            self.assertNotIn(("ethernet", True), events)

    def test_stop_releases_blocking_receive(self):
        with tempfile.TemporaryDirectory() as folder:
            blocked, release = threading.Event(), threading.Event()
            class Blocking(Socket):
                def recv(self, n):
                    if self.data:
                        return super().recv(n)
                    blocked.set()
                    release.wait(2)
                    return b""
                def shutdown(self, how):
                    release.set()
                    super().shutdown(how)
            sock = Blocking(b"READY2\r\n")
            worker = EthernetWorker(self.args(folder), lambda *e: None, lambda args, log: (sock, "board", "local"))
            worker.start()
            self.assertTrue(blocked.wait(2))
            worker.stop()
            worker.join(2)
            self.assertFalse(worker.is_alive())


if __name__ == "__main__":
    unittest.main()
