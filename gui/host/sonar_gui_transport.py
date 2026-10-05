"""Background Ethernet capture receiver for the desktop application."""
from datetime import datetime
from pathlib import Path
import socket
import threading

from capture_store import CaptureStore, utc_now, write_json
from receive_ethernet import read_exact, receive_record
from sonar_connection import connect_board, permission_help


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
                self.emit("endpoint", {"board": board, "local_ip": local, "folder": str(store.folder)})
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
