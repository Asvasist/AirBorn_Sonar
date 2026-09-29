"""Background serial and Ethernet I/O for the desktop application."""
from collections import deque
from datetime import datetime
from pathlib import Path
import socket
import threading

from capture_store import CaptureStore, utc_now, write_json
from receive_ethernet import read_exact, receive_record
from sonar_connection import connect_board, permission_help


class LineDecoder:
    """Bound partial UART lines so a damaged stream cannot grow RAM forever."""
    def __init__(self):
        self.pending = bytearray()

    def feed(self, data):
        self.pending.extend(data)
        lines = []
        while b"\n" in self.pending:
            raw, _, rest = self.pending.partition(b"\n")
            self.pending = bytearray(rest)
            lines.append(raw.rstrip(b"\r").decode("utf-8", errors="replace"))
        if len(self.pending) > 8192:
            self.pending.clear()
            raise ValueError("UART line exceeded 8192 bytes; reconnect and check the baud rate.")
        return lines


class SerialWorker(threading.Thread):
    def __init__(self, port, emit, serial_factory=None):
        super().__init__(name="sonar-serial", daemon=True)
        self.port, self.emit, self.serial_factory = port, emit, serial_factory
        self.cancel = threading.Event()
        self.lock = threading.Lock()
        self.commands = deque()

    def send(self, data):
        with self.lock:
            if self.cancel.is_set():
                raise OSError("Serial connection is closing.")
            if data == b"x":
                self.commands.clear()
            if len(self.commands) >= 32:
                raise OSError("Serial command queue is full.")
            self.commands.append(data)

    def stop(self):
        self.cancel.set()

    def run(self):
        device = None
        try:
            factory = self.serial_factory
            if factory is None:
                import serial
                factory = serial.Serial
            device = factory(port=None, baudrate=115200, timeout=0.05, write_timeout=1)
            device.dtr = device.rts = False
            device.port = self.port
            device.open()
            if self.cancel.is_set():
                return
            self.emit("serial", True)
            decoder = LineDecoder()
            while not self.cancel.is_set():
                # Hold the lock through this bounded write: Stop can clear every
                # pending command, but cannot undo a command already transmitted.
                with self.lock:
                    command = self.commands.popleft() if self.commands else None
                    if command is not None and device.write(command) != len(command):
                        raise OSError("Incomplete UART command write.")
                for line in decoder.feed(device.read(max(1, min(device.in_waiting, 4096)))):
                    self.emit("line", line)
        except (OSError, ValueError, ImportError) as error:
            if not self.cancel.is_set():
                self.emit("log", f"Serial error: {error}")
        finally:
            self.cancel.set()
            if device is not None:
                device.close()
            self.emit("serial", False)


class EthernetWorker(threading.Thread):
    def __init__(self, args, emit, connector=connect_board):
        super().__init__(name="sonar-ethernet", daemon=True)
        self.args, self.emit, self.connector = args, emit, connector
        self.cancel = threading.Event()
        self.lock = threading.Lock()
        self.sock = None

    def stop(self):
        self.cancel.set()
        with self.lock:
            if self.sock is not None:
                try:
                    self.sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                self.sock.close()

    def log(self, message):
        self.emit("log", message)

    def run(self):
        try:
            folder = Path(self.args.output) / datetime.now().strftime("session_%Y%m%d_%H%M%S_%f")
            folder.mkdir(parents=True, exist_ok=False)
            write_json(folder / "session.json", {
                "started_utc": utc_now(), "board": self.args.board, "port": self.args.port,
                "client": "sonar_gui", "folder_time_basis": "board software capture-request timestamp, in seconds",
            })
            self.emit("folder", str(folder))
            self.receive_loop(folder)
        except (OSError, ValueError) as error:
            self.log(f"Receiver cannot create the recording session: {error}")
        finally:
            self.emit("ethernet", False)

    def receive_loop(self, folder):
        connection = 0
        while not self.cancel.is_set():
            sock = None
            try:
                sock, board, local = self.connector(self.args, log=self.log)
                with self.lock:
                    if self.cancel.is_set():
                        sock.close()
                        return
                    self.sock = sock
                sock.settimeout(10)
                sock.sendall(b"SONAR2\r\n")
                if read_exact(sock, 8) != b"READY2\r\n":
                    raise ValueError("Unexpected board handshake.")
                sock.settimeout(75)
                connection += 1
                store = CaptureStore(folder / f"connection_{connection:06d}")
                write_json(store.folder / "connection.json", {
                    "connected_utc": utc_now(), "board": board, "port": self.args.port, "local_ip": local,
                })
                if self.cancel.is_set():
                    return
                self.emit("ethernet", True)
                self.log(f"Ethernet connected to {board}. Recordings arrive in 30-second batches.")
                last = 0
                while not self.cancel.is_set():
                    path, meta = receive_record(sock, store)
                    if path:
                        seq = meta["sequence"]
                        if last and seq not in (last, last + 1):
                            self.log(f"Sequence discontinuity: previous={last}, received={seq}")
                        last = seq
                        self.emit("record", (str(path), meta))
                    else:
                        self.emit("batch", meta["trigger_request_us"] // 1_000_000)
            except (OSError, EOFError, ValueError) as error:
                if not self.cancel.is_set():
                    self.log(f"Ethernet interrupted: {error}. Retrying in 5 seconds; queued frames remain on the board.")
                    hint = permission_help(error)
                    if hint:
                        self.log(hint)
            finally:
                with self.lock:
                    self.sock = None
                    if sock is not None:
                        sock.close()
                self.emit("ethernet", False)
            self.cancel.wait(5)
