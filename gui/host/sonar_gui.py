#!/usr/bin/env python3
"""Desktop controls for the existing Stage 2 firmware; run the board in Vitis first."""
import os
from pathlib import Path
import queue
import time
from types import SimpleNamespace
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

from sonar_gui_control import ExperimentControl
from sonar_gui_transport import EthernetWorker, SerialWorker


class SonarApp:
    def __init__(self, root):
        self.root = root
        self.events = queue.Queue(maxsize=4096)
        self.serial_worker = self.ethernet_worker = None
        self.disconnecting = False
        self.close_after = False
        self.exit_pending = False
        self.exit_deadline = 0.0
        self.session_folder = None
        self.deliveries = self.saved_bytes = 0
        self.last_error = ""
        self.control = ExperimentControl(self.send, self.log)
        self.port = tk.StringVar()
        self.output = tk.StringVar(value=r"D:\SonarCaptures")
        self.adapter = tk.StringVar(value="Ethernet")
        self.board_ip = tk.StringVar()
        self.steps = tk.StringVar(value="20")
        self.reverse = tk.BooleanVar()
        self.connection_text = tk.StringVar(value="Disconnected")
        self.state_text = tk.StringVar(value="Board: waiting for connection")
        self.capture_text = tk.StringVar(value="0 file deliveries saved")
        self.detail_text = tk.StringVar(value="No capture received yet.")
        self.guidance = tk.StringVar(value="Run the firmware in Vitis, close its serial terminal, then select the board's COM port.")
        self.build_view()
        self.refresh_ports()
        root.protocol("WM_DELETE_WINDOW", lambda: self.request_disconnect(close=True))
        self.timer = root.after(50, self.poll)

    def build_view(self):
        root = self.root
        root.title("Airborne Sonar | Experiment control")
        root.geometry("1000x760")
        root.minsize(1000, 700)
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure("TFrame", background="#f3f5f8")
        style.configure("TLabel", background="#f3f5f8", foreground="#18283b", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI", 22, "bold"))
        style.configure("TButton", padding=(12, 7), font=("Segoe UI", 10))
        style.configure("Start.TButton", foreground="white", background="#146b68", font=("Segoe UI", 11, "bold"))
        style.configure("Stop.TButton", foreground="#8b2626", font=("Segoe UI", 11, "bold"))
        style.configure("TLabelframe", background="#f3f5f8")
        style.configure("TLabelframe.Label", background="#f3f5f8", font=("Segoe UI", 10, "bold"))
        panel = ttk.Frame(root, padding=22)
        panel.pack(fill="both", expand=True)
        panel.columnconfigure(0, weight=1)
        panel.rowconfigure(6, weight=1)
        ttk.Label(panel, text="Airborne Sonar", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(panel, text="Connect  /  Set steps  /  Start experiment").grid(row=1, column=0, sticky="w", pady=(2, 14))
        setup = ttk.LabelFrame(panel, text="Connection & recordings", padding=12)
        setup.grid(row=2, column=0, sticky="ew")
        setup.columnconfigure(1, weight=1)
        ttk.Label(setup, text="Board serial port").grid(row=0, column=0, sticky="w", padx=(0, 12))
        self.port_box = ttk.Combobox(setup, textvariable=self.port, width=23)
        self.port_box.grid(row=0, column=1, sticky="ew", pady=4)
        self.refresh_button = ttk.Button(setup, text="Refresh", command=self.refresh_ports)
        self.refresh_button.grid(row=0, column=2, padx=8)
        self.connect_button = ttk.Button(setup, text="Connect", command=self.connect)
        self.connect_button.grid(row=0, column=3)
        ttk.Label(setup, text="Save recordings to").grid(row=1, column=0, sticky="w")
        self.output_entry = ttk.Entry(setup, textvariable=self.output)
        self.output_entry.grid(row=1, column=1, sticky="ew", pady=4)
        self.browse_button = ttk.Button(setup, text="Browse", command=self.browse)
        self.browse_button.grid(row=1, column=2, padx=8)
        ttk.Button(setup, text="Open folder", command=self.open_folder).grid(row=1, column=3)
        network = ttk.Frame(setup)
        network.grid(row=2, column=0, columnspan=4, sticky="ew", pady=(6, 0))
        ttk.Label(network, text="Ethernet adapter").pack(side="left")
        self.adapter_entry = ttk.Entry(network, textvariable=self.adapter, width=18)
        self.adapter_entry.pack(side="left", padx=(8, 16))
        ttk.Label(network, text="Board IP (blank = discover)").pack(side="left")
        self.ip_entry = ttk.Entry(network, textvariable=self.board_ip, width=18)
        self.ip_entry.pack(side="left", padx=8)
        run = ttk.LabelFrame(panel, text="Experiment", padding=12)
        run.grid(row=3, column=0, sticky="ew", pady=12)
        ttk.Label(run, text="Steps per cycle").grid(row=0, column=0, sticky="w")
        self.steps_entry = ttk.Entry(run, textvariable=self.steps, width=10, font=("Segoe UI", 13))
        self.steps_entry.grid(row=0, column=1, padx=12)
        self.reverse_box = ttk.Checkbutton(run, text="Reverse direction", variable=self.reverse)
        self.reverse_box.grid(row=0, column=2, padx=8)
        self.start_button = ttk.Button(run, text="Start experiment", style="Start.TButton", command=self.start)
        self.start_button.grid(row=0, column=3, padx=8)
        self.stop_button = ttk.Button(run, text="Stop", style="Stop.TButton", command=self.stop)
        self.stop_button.grid(row=0, column=4)
        self.disconnect_button = ttk.Button(run, text="Disconnect", command=self.request_disconnect)
        self.disconnect_button.grid(row=0, column=5, padx=(8, 0))
        ttk.Label(run, text="Start sets N + steps + Enter, checks the reply, then sends R. Stop sends X.").grid(
            row=1, column=0, columnspan=6, sticky="w", pady=(10, 0))
        status = ttk.Frame(panel)
        status.grid(row=4, column=0, sticky="ew")
        for variable in (self.connection_text, self.state_text, self.capture_text, self.detail_text):
            ttk.Label(status, textvariable=variable, wraplength=920).pack(anchor="w", pady=2)
        ttk.Label(panel, textvariable=self.guidance, wraplength=920, foreground="#146b68").grid(
            row=5, column=0, sticky="ew", pady=(10, 6))
        self.activity = ScrolledText(panel, height=10, wrap="word", font=("Consolas", 9),
                                     background="#162334", foreground="#dfe8f2", relief="flat", state="disabled")
        self.activity.grid(row=6, column=0, sticky="nsew")
        ttk.Label(panel, text="Ethernet saves every 30 seconds. Stop leaves the receiver open to finish saving queued captures.",
                  wraplength=920).grid(row=7, column=0, sticky="w", pady=(8, 0))
        self.render()

    def emit(self, kind, value):
        # Backpressure bounds desktop memory; workers never touch Tk widgets.
        self.events.put((kind, value))

    def log(self, text):
        self.activity.configure(state="normal")
        self.activity.insert("end", f"{time.strftime('%H:%M:%S')}  {text}\n")
        lines = int(self.activity.index("end-1c").split(".")[0])
        if lines > 800:
            self.activity.delete("1.0", f"{lines - 800}.0")
        self.activity.see("end")
        self.activity.configure(state="disabled")

    def refresh_ports(self):
        try:
            from serial.tools import list_ports
            ports = list(list_ports.comports())
            self.port_box["values"] = [p.device for p in ports]
            if not self.port.get() and len(ports) == 1:
                self.port.set(ports[0].device)
            self.log("Available ports: " + (", ".join(f"{p.device} ({p.description})" for p in ports) or "none detected"))
        except ImportError:
            self.log('Install the serial dependency once: py -m pip install pyserial')

    def browse(self):
        folder = filedialog.askdirectory(parent=self.root, title="Choose recording folder")
        if folder:
            self.output.set(folder)

    def open_folder(self):
        folder = self.session_folder or Path(self.output.get()).expanduser()
        if not folder.is_dir():
            messagebox.showinfo("Recordings", "The folder will be created when the receiver connects.", parent=self.root)
            return
        try:
            os.startfile(str(folder))
        except OSError as error:
            messagebox.showerror("Cannot open folder", str(error), parent=self.root)

    def connect(self):
        try:
            import serial  # noqa: F401 -- verify dependency before starting either worker
            if not self.port.get().strip():
                raise ValueError("Select or enter the board's COM port first.")
            if not self.output.get().strip() or not Path(self.output.get()).is_absolute():
                raise ValueError("Choose an absolute recording folder, for example D:\\SonarCaptures.")
            if not self.adapter.get().strip():
                raise ValueError("Enter the Windows Ethernet adapter name.")
            if self.board_ip.get().strip():
                import ipaddress
                ipaddress.IPv4Address(self.board_ip.get().strip())
            self.deliveries = self.saved_bytes = 0
            self.session_folder = None
            self.last_error = ""
            self.control = ExperimentControl(self.send, self.log)
            args = SimpleNamespace(output=Path(self.output.get()), interface=self.adapter.get().strip(),
                                   board=self.board_ip.get().strip() or None, local_ip=None, port=5001)
            self.serial_worker = SerialWorker(self.port.get().strip(), self.emit)
            self.ethernet_worker = EthernetWorker(args, self.emit)
            self.serial_worker.start()
            self.ethernet_worker.start()
            self.log("Connecting. No start or movement command has been sent.")
        except ImportError:
            messagebox.showerror("Install pyserial", "Run this once in PowerShell, then reopen the GUI:\n\npy -m pip install pyserial", parent=self.root)
        except (ValueError, OSError) as error:
            messagebox.showerror("Cannot connect", str(error), parent=self.root)
        self.render()

    def send(self, command):
        if self.serial_worker is None or not self.serial_worker.is_alive():
            raise OSError("Serial worker is not connected. Reconnect before sending commands.")
        self.serial_worker.send(command)

    def start(self):
        try:
            self.control.start(self.steps.get(), self.reverse.get())
        except (ValueError, OSError) as error:
            self.log(str(error))
            messagebox.showerror("Cannot start", str(error), parent=self.root)
        self.render()

    def stop(self):
        try:
            self.control.stop()
        except (ValueError, OSError) as error:
            self.log(str(error))
            messagebox.showerror("Stop not sent", str(error), parent=self.root)
        self.render()

    def request_disconnect(self, close=False):
        if self.disconnecting or self.exit_pending:
            self.close_after |= close
            return
        self.close_after = close
        if self.serial_worker is None or self.control.drained:
            self.disconnect()
            return
        if not self.control.serial_up:
            if messagebox.askyesno("Board state unknown", "Serial is disconnected, so Stop cannot be confirmed. The board may still be running and unsaved captures may remain in RAM.\n\nClose these connections anyway?", parent=self.root):
                self.disconnect()
            return
        self.exit_pending = True
        self.exit_deadline = time.monotonic() + 90
        self.stop()
        self.log("Waiting for STOPPED and DDR empty before disconnecting. The next batch can take 30 seconds.")

    def disconnect(self):
        self.exit_pending = False
        self.disconnecting = True
        for worker in (self.serial_worker, self.ethernet_worker):
            if worker is not None:
                worker.stop()
        self.log("Closing connections...")

    def handle(self, kind, value):
        if kind == "log":
            self.log(value)
        elif kind == "serial":
            self.control.serial_connection(value)
            self.log("Serial connected at 115200 baud." if value else "Serial disconnected.")
        elif kind == "ethernet":
            self.control.ethernet_connection(value)
        elif kind == "line":
            self.control.line(value)
            if value and not value.startswith(("HEALTH RUNNING", "STAGE2 IDLE", "STAGE2 STOPPED")):
                self.log(value)
        elif kind == "folder":
            self.session_folder = Path(value)
            self.log(f"Saving recordings in {value}")
        elif kind == "record":
            path, meta = value
            self.deliveries += 1
            self.saved_bytes += meta["payload_bytes"]
            self.detail_text.set(f"Last capture: cycle {meta['cycle']} | RX {meta['rx_nominal_us']/1000:g} ms | {Path(path).name}")
            self.log(f"Saved {path} ({meta['payload_bytes']} bytes)")
        elif kind == "batch":
            self.log(f"Batch at board second {value}. Status markers do not create .bin files.")

    def poll(self):
        try:
            for _ in range(500):
                try:
                    kind, value = self.events.get_nowait()
                except queue.Empty:
                    break
                self.handle(kind, value)
            if not self.disconnecting:
                self.control.tick()
            if self.control.error and self.control.error != self.last_error:
                self.last_error = self.control.error
                self.log(self.last_error)
            if self.exit_pending:
                if self.control.drained:
                    self.disconnect()
                elif time.monotonic() >= self.exit_deadline:
                    close_now = messagebox.askyesno("Stop/save not confirmed", "The board has not confirmed both stopped operation and an empty DDR queue. Check the connections and log. Unsaved captures are lost if the board is reset or powered off.\n\nDisconnect anyway? Choose No to keep waiting.", parent=self.root)
                    if close_now:
                        self.disconnect()
                    else:
                        self.exit_deadline = time.monotonic() + 90
            if self.disconnecting and self.events.empty() and all(w is None or not w.is_alive() for w in (self.serial_worker, self.ethernet_worker)):
                self.serial_worker = self.ethernet_worker = None
                self.disconnecting = False
                self.control = ExperimentControl(self.send, self.log)
                if self.close_after:
                    self.root.destroy()
                    return
            self.render()
        except (OSError, ValueError) as error:
            self.log(f"Control error: {error}")
            self.control.serial_connection(False)
        self.timer = self.root.after(50, self.poll)

    def render(self):
        c = self.control
        attached = self.serial_worker is not None
        busy = self.disconnecting or self.exit_pending
        for widget in (self.port_box, self.output_entry, self.adapter_entry, self.ip_entry,
                       self.refresh_button, self.browse_button, self.connect_button):
            widget.configure(state="disabled" if attached or busy else "normal")
        self.start_button.configure(state="normal" if c.can_start and not busy else "disabled")
        self.stop_button.configure(state="normal" if c.serial_up and not self.disconnecting else "disabled")
        self.disconnect_button.configure(state="normal" if attached and not busy else "disabled")
        for widget in (self.steps_entry, self.reverse_box):
            widget.configure(state="normal" if not attached or (c.can_start and not busy) else "disabled")
        self.connection_text.set(f"Serial: {'connected' if c.serial_up else 'disconnected'}    |    Ethernet: {'connected' if c.ethernet_up else 'connecting / disconnected'}")
        stale = c.serial_up and time.monotonic() - c.last_status >= 4
        self.state_text.set(f"Board: {c.board_state}{' (status stale)' if stale else ''}    |    Cycle {c.cycle}    |    DDR {c.ddr}/{c.capacity}    |    Steps {c.steps}, divisor {c.divisor}")
        self.capture_text.set(f"{self.deliveries} file deliveries saved  |  {self.saved_bytes:,} bytes  |  Reconnect retries can redeliver a capture.")
        if busy:
            hint = "Stopping and saving queued captures..." if self.exit_pending else "Closing connections; discovery may take a few seconds..."
        elif c.error:
            hint = c.error
        elif c.can_start:
            hint = "Ready. Enter steps, choose direction and click Start experiment. Divisor 1 means full motor steps."
        elif c.phase != "idle":
            hint = f"Waiting for board confirmation ({c.phase})..."
        elif c.board_state == "WAIT_BUFFER":
            hint = "Board waiting for a free DDR buffer; it resumes automatically when recordings are transferred."
        elif c.board_state in {"ACQUIRE", "PRE_MOVE", "MOVING", "SETTLING"}:
            hint = "Experiment repeats automatically. Stop ends the cycles; Ethernet continues saving."
        else:
            hint = "Run in Vitis and close its serial terminal. Connect here; Start becomes available when the board and receiver are ready."
        self.guidance.set(hint)


def main():
    root = tk.Tk()
    SonarApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
