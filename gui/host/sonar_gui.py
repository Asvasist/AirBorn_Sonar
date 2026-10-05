#!/usr/bin/env python3
"""Desktop controls for the GENERATE/WAV BRAM Vitis application."""
import os
from pathlib import Path
import queue
from sonar_configure import playback_description
import time
from types import SimpleNamespace
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

from sonar_gui_control import ExperimentControl, parse_steps
from sonar_gui_transport import EthernetWorker
from sonar_console_transport import ConsoleWorker
from sonar_gui_audio import ConfigurationWorker, audio_request


class SonarApp:
    def __init__(self, root):
        self.root = root
        self.events = queue.Queue(maxsize=4096)
        self.console_worker = self.ethernet_worker = None
        self.audio_worker = None
        self.console_token = 0
        self.retired_consoles = []
        self.audio_token = 0
        self.endpoint = self.network_args = None
        self.probe_pending = False
        self.pending_start = None
        self.audio_capabilities = None
        self.disconnecting = False
        self.close_after = False
        self.exit_pending = False
        self.exit_deadline = 0.0
        self.session_folder = None
        self.deliveries = self.saved_bytes = 0
        self.last_error = ""
        self.control = ExperimentControl(self.send, self.log)
        self.output = tk.StringVar(value=r"D:\SonarCaptures")
        self.adapter = tk.StringVar(value="Ethernet")
        self.board_ip = tk.StringVar()
        self.steps = tk.StringVar(value="30")
        self.reverse = tk.BooleanVar()
        self.tx_mode = tk.StringVar(value="GENERATE")
        self.tx_start_hz = tk.StringVar(value="2500")
        self.tx_stop_hz = tk.StringVar(value="5600")
        self.tx_duration_ms = tk.StringVar(value="35")
        self.tx_amplitude = tk.StringVar(value="40")
        self.wav_path = tk.StringVar()
        self.wav_info = tk.StringVar(value="Select a WAV. Other rates are resampled to 96 kHz; duration is limited by BRAM and 49 ms.")
        self.audio_text = tk.StringVar(value="Audio settings have not been applied.")
        self.connection_text = tk.StringVar(value="Disconnected")
        self.state_text = tk.StringVar(value="Board: waiting for connection")
        self.capture_text = tk.StringVar(value="0 file deliveries saved")
        self.detail_text = tk.StringVar(value="No capture received yet.")
        self.guidance = tk.StringVar(value="Run the firmware in Vitis, connect Ethernet, then click Connect.")
        self.build_view()
        for variable in (self.tx_mode, self.tx_start_hz, self.tx_stop_hz, self.tx_duration_ms,
                         self.tx_amplitude, self.wav_path):
            variable.trace_add("write", self.audio_edited)
        root.protocol("WM_DELETE_WINDOW", lambda: self.request_disconnect(close=True))
        self.timer = root.after(50, self.poll)

    def build_view(self):
        root = self.root
        root.title("Airborne Sonar | Experiment control")
        root.geometry("1100x900")
        root.minsize(1040, 860)
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
        panel.rowconfigure(7, weight=1)
        ttk.Label(panel, text="Airborne Sonar", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(panel, text="Connect  /  Choose audio  /  Apply settings  /  Start scan").grid(row=1, column=0, sticky="w", pady=(2, 10))
        setup = ttk.LabelFrame(panel, text="Connection & recordings", padding=12)
        setup.grid(row=2, column=0, sticky="ew")
        setup.columnconfigure(1, weight=1)
        ttk.Label(setup, text="Ethernet console + recordings").grid(row=0, column=0, columnspan=3, sticky="w", pady=4)
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
        audio = ttk.LabelFrame(panel, text="Transmit signal  |  Microphones: 50 ms, 96 kHz", padding=10)
        audio.grid(row=3, column=0, sticky="ew", pady=(10, 0))
        audio.columnconfigure(0, weight=1)
        modes = ttk.Frame(audio)
        modes.grid(row=0, column=0, sticky="ew")
        self.generate_radio = ttk.Radiobutton(modes, text="Generate chirp", value="GENERATE", variable=self.tx_mode)
        self.generate_radio.pack(side="left", padx=(0, 18))
        self.wav_radio = ttk.Radiobutton(modes, text="Upload WAV", value="WAV", variable=self.tx_mode)
        self.wav_radio.pack(side="left", padx=(0, 28))
        ttk.Label(modes, text="Amplitude (%)").pack(side="left")
        self.amplitude_entry = ttk.Entry(modes, textvariable=self.tx_amplitude, width=6)
        self.amplitude_entry.pack(side="left", padx=(8, 18))
        ttk.Label(modes, text="0-100% digital level  |  TX / RX: 96 kHz").pack(side="left")
        self.generated_frame = ttk.Frame(audio)
        self.generated_frame.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        self.generated_entries = []
        for i, (label, variable) in enumerate((("Start (Hz)", self.tx_start_hz),
                                               ("Stop (Hz)", self.tx_stop_hz),
                                               ("Duration (ms, max 49)", self.tx_duration_ms))):
            ttk.Label(self.generated_frame, text=label).grid(row=0, column=2*i, padx=(0 if i == 0 else 20, 8))
            entry = ttk.Entry(self.generated_frame, textvariable=variable, width=12)
            entry.grid(row=0, column=2*i+1)
            self.generated_entries.append(entry)
        self.wav_frame = ttk.Frame(audio)
        self.wav_frame.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        self.wav_frame.columnconfigure(0, weight=1)
        self.wav_entry = ttk.Entry(self.wav_frame, textvariable=self.wav_path)
        self.wav_entry.grid(row=0, column=0, sticky="ew")
        self.wav_browse = ttk.Button(self.wav_frame, text="Choose WAV...", command=self.choose_wav)
        self.wav_browse.grid(row=0, column=1, padx=8)
        self.wav_inspect = ttk.Button(self.wav_frame, text="Inspect file", command=self.preview_wav)
        self.wav_inspect.grid(row=0, column=2)
        self.audio_note = ttk.Label(audio, textvariable=self.wav_info, wraplength=990)
        self.audio_note.grid(row=2, column=0, sticky="w", pady=5)
        actions = ttk.Frame(audio)
        actions.grid(row=3, column=0, sticky="ew")
        actions.columnconfigure(2, weight=1)
        self.apply_button = ttk.Button(actions, text="Apply settings", command=self.apply_audio)
        self.apply_button.grid(row=0, column=0)
        self.audio_check = ttk.Button(actions, text="Check board audio", command=self.check_audio)
        self.audio_check.grid(row=0, column=1, padx=8)
        ttk.Label(actions, textvariable=self.audio_text, wraplength=650).grid(row=0, column=2, sticky="w")
        run = ttk.LabelFrame(panel, text="Experiment", padding=12)
        run.grid(row=4, column=0, sticky="ew", pady=10)
        ttk.Label(run, text="Positions per 360°").grid(row=0, column=0, sticky="w")
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
        ttk.Label(run, text="Choose 30, 60, 90 ... up to 800 positions. One run always totals exactly 800 Tic units = 360°.").grid(
            row=1, column=0, columnspan=6, sticky="w", pady=(10, 0))
        status = ttk.Frame(panel)
        status.grid(row=5, column=0, sticky="ew")
        for variable in (self.connection_text, self.state_text, self.capture_text, self.detail_text):
            ttk.Label(status, textvariable=variable, wraplength=920).pack(anchor="w", pady=2)
        ttk.Label(panel, textvariable=self.guidance, wraplength=920, foreground="#146b68").grid(
            row=6, column=0, sticky="ew", pady=(8, 6))
        self.activity = ScrolledText(panel, height=6, wrap="word", font=("Consolas", 9),
                                     background="#162334", foreground="#dfe8f2", relief="flat", state="disabled")
        self.activity.grid(row=7, column=0, sticky="nsew")
        ttk.Label(panel, text="Ethernet saves every 30 seconds. Stop leaves the receiver open to finish saving queued captures.",
                  wraplength=990).grid(row=8, column=0, sticky="w", pady=(8, 0))
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

    def audio_edited(self, *_):
        if self.audio_busy():
            self.cancel_audio()
        if _ and _[0] == str(self.wav_path):
            self.wav_info.set("Select a WAV and click Inspect file. Other rates are resampled to 96 kHz before upload.")
        self.invalidate_audio("Settings changed. Apply them before starting.")
        self.render()

    def invalidate_audio(self, message):
        self.control.audio_configuration(None)
        self.pending_start = None
        self.audio_text.set(message)

    def audio_busy(self):
        return self.audio_worker is not None

    def cancel_audio(self):
        self.audio_token += 1
        if self.audio_worker is not None:
            self.audio_worker.stop()
        self.pending_start = None

    def audio_job(self, action, request=None, expected=None):
        if self.audio_busy():
            raise ValueError("Wait for the current audio operation to finish.")
        if action != "preview" and (not self.endpoint or not self.control.ethernet_up):
            raise ValueError("Wait for the recording connection before configuring audio.")
        args = SimpleNamespace(**vars(self.network_args)) if self.network_args else None
        if args is not None and self.endpoint:
            args.board = self.endpoint["board"]
        self.audio_token += 1
        self.audio_worker = ConfigurationWorker(self.audio_token, action, args,
            self.endpoint["folder"] if self.endpoint else None, self.emit, request, expected)
        self.audio_worker.start()
        self.render()

    def choose_wav(self):
        path = filedialog.askopenfilename(parent=self.root, title="Select chirp WAV",
                                          filetypes=[("WAV audio", "*.wav"), ("All files", "*.*")])
        if path:
            self.wav_path.set(path)
            self.preview_wav()

    def preview_wav(self):
        try:
            if not self.wav_path.get().strip():
                raise ValueError("Select a WAV file first.")
            self.audio_job("preview", {"mode": "WAV", "path": self.wav_path.get()})
        except (ValueError, OSError) as error:
            messagebox.showerror("WAV file", str(error), parent=self.root)

    def check_audio(self):
        try:
            self.audio_text.set("Checking the board audio interface...")
            self.audio_job("probe")
        except (ValueError, OSError) as error:
            self.audio_text.set(str(error))

    def apply_audio(self):
        try:
            if not self.control.can_configure:
                raise ValueError("Wait for an idle board and the recording connection. Stop the scan before changing audio.")
            request = audio_request(self.tx_mode.get(), self.tx_start_hz.get(), self.tx_stop_hz.get(),
                                    self.tx_duration_ms.get(), self.tx_amplitude.get(), self.wav_path.get())
            self.invalidate_audio("Applying audio settings..." if request["mode"] == "GENERATE" else "Validating and uploading WAV...")
            self.audio_job("apply", request)
        except (ValueError, OSError) as error:
            self.log(str(error))
            messagebox.showerror("Cannot apply audio", str(error), parent=self.root)

    def handle_audio(self, token, kind, value):
        if token != self.audio_token:
            return
        if kind in ("log", "progress"):
            if kind == "log":
                self.log(value)
            else:
                self.audio_text.set(value)
        elif kind == "preview":
            self.wav_info.set(f"{value['name']}  |  {value['sample_hz']:,} Hz  |  {value['channels']} channel(s), "
                              f"{value['bits']}-bit  |  {value['duration_us']/1000:g} ms"
                              f"  → TX 96 kHz, {value['transmission']['samples']:,} samples")
            if value['transmission'].get('clipped_samples', 0):
                self.log(f"Resampling clipped {value['transmission']['clipped_samples']} samples; reduce the source WAV level if needed.")
        elif kind == "probe":
            self.audio_capabilities = value["capabilities"] if value.get("sample_hz") == 96000 else 0
            if not self.audio_capabilities:
                self.invalidate_audio("96 kHz audio unavailable. Load the matching Vitis firmware, platform and bitstream.")
            else:
                modes = "/".join(name for mask, name in ((1, "GENERATE"), (2, "WAV")) if self.audio_capabilities & mask)
                self.audio_text.set(f"Board supports {modes}. Apply the selected settings before Start."
                                    if self.control.applied_audio is None else f"Board audio available; applied configuration {self.control.applied_audio['config_id']}.")
                self.wav_info.set(playback_description(value))
                self.log(playback_description(value))
        elif kind == "applied":
            reply, manifest = value
            self.control.audio_configuration(reply)
            mode = "WAV" if reply["mode"] else "GENERATE"
            self.audio_text.set(f"Applied #{reply['config_id']}: {mode}, {reply['duration_us']/1000:g} ms, {reply['amplitude_pct']}%, 96 kHz.")
            self.log(f"Audio applied; configuration saved to {manifest}")
            self.send(b"s")
        elif kind == "verify":
            pending = self.pending_start
            self.pending_start = None
            if pending is not None and not (self.disconnecting or self.exit_pending):
                try:
                    self.control.start(*pending)
                    self.audio_text.set(f"Configuration #{value['config_id']} verified. Starting scan...")
                except (ValueError, OSError) as error:
                    self.audio_text.set(str(error))
                    self.log(str(error))
        elif kind == "error":
            self.invalidate_audio(value)
            self.log(f"Audio: {value}")
        # Worker lifetime is collected in poll(), after its final event.

    def connect(self):
        try:
            if not self.output.get().strip() or not Path(self.output.get()).is_absolute():
                raise ValueError("Choose an absolute recording folder, for example D:\\SonarCaptures.")
            if not self.adapter.get().strip():
                raise ValueError("Enter the Windows Ethernet adapter name.")
            if self.board_ip.get().strip():
                import ipaddress
                ipaddress.IPv4Address(self.board_ip.get().strip())
            self.deliveries = self.saved_bytes = 0
            self.cancel_audio()
            self.session_folder = None
            self.last_error = ""
            self.control = ExperimentControl(self.send, self.log)
            self.endpoint = None
            self.audio_capabilities = None
            self.invalidate_audio("Connect, choose audio settings, then Apply.")
            args = SimpleNamespace(output=Path(self.output.get()), interface=self.adapter.get().strip(),
                                   board=self.board_ip.get().strip() or None, local_ip=None, port=5001)
            self.network_args = args
            self.ethernet_worker = EthernetWorker(args, self.emit)
            self.ethernet_worker.start()
            self.log("Connecting. No start or movement command has been sent.")
        except (ValueError, OSError) as error:
            messagebox.showerror("Cannot connect", str(error), parent=self.root)
        self.render()

    def open_console(self, endpoint):
        # Bind controls to the exact board/local IP used by the recording socket.
        # Tokens discard late lines from a superseded connection.
        self.console_token += 1
        if self.console_worker is not None:
            self.console_worker.stop()
            self.retired_consoles.append(self.console_worker)
        self.control.console_connection(False)
        token = self.console_token
        self.console_worker = ConsoleWorker(
            endpoint["board"], endpoint["local_ip"],
            lambda kind, value: self.emit("console_event", (token, kind, value)))
        self.console_worker.start()

    def send(self, command):
        if self.console_worker is None or not self.console_worker.is_alive():
            raise OSError("Ethernet console is not connected. Wait for reconnection before sending commands.")
        self.console_worker.send(command)

    def start(self):
        try:
            parse_steps(self.steps.get(), self.reverse.get())
            if not self.control.can_start:
                raise ValueError("Apply audio settings and wait for the board's confirmation before Start.")
            self.pending_start = (self.steps.get(), self.reverse.get())
            self.audio_text.set("Verifying the applied configuration before starting...")
            self.audio_job("verify", expected=dict(self.control.applied_audio))
        except (ValueError, OSError) as error:
            self.log(str(error))
            messagebox.showerror("Cannot start", str(error), parent=self.root)
        self.render()

    def stop(self):
        try:
            if self.audio_busy():
                self.cancel_audio()
                self.invalidate_audio("Audio operation cancelled. Check/apply settings again before Start.")
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
        if self.console_worker is None or self.control.drained:
            self.disconnect()
            return
        if not self.control.console_up:
            if messagebox.askyesno("Board state unknown", "The Ethernet console is disconnected. The firmware requests Stop on connection loss, but it has not been confirmed here; unsaved captures may remain in RAM.\n\nClose these connections anyway?", parent=self.root):
                self.disconnect()
            return
        self.exit_pending = True
        self.exit_deadline = time.monotonic() + 90
        self.stop()
        self.log("Waiting for STOPPED and DDR empty before disconnecting. The next batch can take 30 seconds.")

    def disconnect(self):
        self.exit_pending = False
        self.disconnecting = True
        self.console_token += 1  # Ignore queued events from the closing console.
        self.control.console_connection(False)
        self.cancel_audio()
        self.invalidate_audio("Disconnected. Apply settings after reconnecting.")
        self.endpoint = None
        self.probe_pending = False
        for worker in (self.console_worker, self.ethernet_worker):
            if worker is not None:
                worker.stop()
        self.log("Closing connections...")

    def handle(self, kind, value):
        if kind == "log":
            self.log(value)
        elif kind == "console_event":
            token, event, data = value
            if token == self.console_token:
                self.handle(event, data)
        elif kind == "history":
            if value:
                self.log("[history] " + value)
        elif kind == "console":
            self.control.console_connection(value)
            if value:
                self.probe_pending = self.endpoint is not None
            if not value:
                self.cancel_audio()
                self.invalidate_audio("Ethernet console disconnected; audio confirmation cleared.")
            self.log("Ethernet console connected (TCP 5004)." if value else "Ethernet console disconnected.")
        elif kind == "ethernet":
            self.control.ethernet_connection(value)
            if not value:
                self.cancel_audio()
                self.endpoint = None
                self.probe_pending = False
                self.invalidate_audio("Ethernet disconnected. Apply settings after reconnecting.")
        elif kind == "endpoint":
            if self.disconnecting:
                return  # A queued discovery result must not reopen a closing session.
            self.cancel_audio()
            self.endpoint = value
            self.open_console(value)
            self.audio_capabilities = None
            self.probe_pending = True
            self.invalidate_audio("Checking the audio interface on the connected board...")
        elif kind == "audio":
            self.handle_audio(*value)
        elif kind == "line":
            if value.startswith("INIT:"):
                self.cancel_audio()
                self.invalidate_audio("Board restarted. Reconnect and apply settings again.")
            self.control.line(value)
            if value and not value.startswith(("HEALTH RUNNING", "SONAR IDLE", "SONAR STOPPED")):
                self.log(value)
        elif kind == "folder":
            self.session_folder = Path(value)
            self.log(f"Saving recordings in {value}")
        elif kind == "record":
            path, meta = value
            self.deliveries += 1
            self.saved_bytes += meta["payload_bytes"]
            self.detail_text.set(f"Last capture: cycle {meta['cycle']} | {meta.get('tx_mode', 'UNKNOWN')} "
                                 f"config #{meta.get('config_id', 0)} | TX {meta.get('tx_nominal_us', 0)/1000:g} ms "
                                 f"| RX {meta['rx_nominal_us']/1000:g} ms | {Path(path).name}")
            self.log(f"Saved {path} ({meta['payload_bytes']} bytes)")
        elif kind == "batch":
            self.log(f"Batch at board second {value}. Status markers do not create audio files.")

    def poll(self):
        try:
            for _ in range(500):
                try:
                    kind, value = self.events.get_nowait()
                except queue.Empty:
                    break
                self.handle(kind, value)
            self.retired_consoles = [w for w in self.retired_consoles if w.is_alive()]
            if self.audio_worker is not None and not self.audio_worker.is_alive():
                self.audio_worker = None
            if (self.probe_pending and not self.audio_busy() and self.control.ethernet_up
                    and not self.disconnecting and not self.exit_pending):
                self.probe_pending = False
                self.check_audio()
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
            if self.disconnecting and self.events.empty() and not self.retired_consoles and all(w is None or not w.is_alive() for w in (self.console_worker, self.ethernet_worker, self.audio_worker)):
                self.console_worker = self.ethernet_worker = None
                self.disconnecting = False
                self.control = ExperimentControl(self.send, self.log)
                if self.close_after:
                    self.root.destroy()
                    return
            self.render()
        except (OSError, ValueError) as error:
            self.log(f"Control error: {error}")
            self.control.console_connection(False)
        self.timer = self.root.after(50, self.poll)

    def render(self):
        c = self.control
        attached = self.ethernet_worker is not None
        busy = self.disconnecting or self.exit_pending
        audio_busy = self.audio_busy()
        for widget in (self.output_entry, self.adapter_entry, self.ip_entry,
                       self.browse_button, self.connect_button):
            widget.configure(state="disabled" if attached or busy else "normal")
        self.start_button.configure(state="normal" if c.can_start and not busy and not audio_busy else "disabled")
        self.stop_button.configure(state="normal" if c.console_up and not self.disconnecting else "disabled")
        self.disconnect_button.configure(state="normal" if attached and not busy else "disabled")
        for widget in (self.steps_entry, self.reverse_box):
            widget.configure(state="normal" if (not attached or c.can_configure) and not busy and not audio_busy else "disabled")
        editable = (not attached or c.can_configure) and not busy and not audio_busy
        for widget in (self.generate_radio, self.wav_radio, self.amplitude_entry, self.wav_entry,
                       self.wav_browse, self.wav_inspect, *self.generated_entries):
            widget.configure(state="normal" if editable else "disabled")
        wav = self.tx_mode.get() == "WAV"
        (self.generated_frame if wav else self.wav_frame).grid_remove()
        (self.wav_frame if wav else self.generated_frame).grid()
        self.audio_note.configure(textvariable=self.wav_info if wav else "",
                                  text="TX: 1-47,999 Hz; up to 49 ms. RX filter cutoff remains 12 kHz; use the current sonar band." if not wav else "")
        supported = self.audio_capabilities is None or bool(self.audio_capabilities & (2 if wav else 1))
        self.apply_button.configure(text="Upload & apply" if wav else "Apply settings",
                                    state="normal" if c.can_configure and self.endpoint and supported and not busy and not audio_busy else "disabled")
        self.audio_check.configure(state="normal" if self.endpoint and c.ethernet_up and not busy and not audio_busy else "disabled")
        self.connection_text.set(f"Ethernet console: {'connected' if c.console_up else 'connecting / disconnected'}    |    Recordings: {'connected' if c.ethernet_up else 'connecting / disconnected'}")
        stale = c.console_up and time.monotonic() - c.last_status >= 4
        self.state_text.set(f"Board: {c.board_state}{' (status stale)' if stale else ''}    |    Cycle {c.cycle}    |    DDR {c.ddr}/{c.capacity}    |    Positions/rev {abs(c.steps)}, direction {'reverse' if c.steps < 0 else 'forward'}, divisor {c.divisor}")
        self.capture_text.set(f"{self.deliveries} file deliveries saved  |  {self.saved_bytes:,} bytes  |  Reconnect retries can redeliver a capture.")
        if busy:
            hint = "Stopping and saving queued captures..." if self.exit_pending else "Closing connections; discovery may take a few seconds..."
        elif audio_busy:
            hint = "Audio operation in progress. Start is disabled until the board confirms the settings. Stop remains available."
        elif c.error:
            hint = c.error
        elif c.can_start:
            hint = "Ready. Enter positions per 360°, choose direction and click Start experiment. Keep divisor 1 for the current 800-unit calibration."
        elif c.can_configure:
            hint = "Choose Generate chirp or Upload WAV, then apply the audio settings before starting the scan."
        elif c.phase != "idle":
            hint = f"Waiting for board confirmation ({c.phase})..."
        elif c.board_state == "WAIT_BUFFER":
            hint = "Board waiting for a free DDR buffer; it resumes automatically when recordings are transferred."
        elif c.board_state in {"ACQUIRE", "PRE_MOVE", "MOVING", "SETTLING"}:
            hint = "One 360° scan is running and will stop automatically after the selected number of positions. Stop can end it early."
        else:
            hint = "Run in Vitis and connect Ethernet. Start becomes available after both connections and audio settings are confirmed."
        self.guidance.set(hint)


def main():
    root = tk.Tk()
    SonarApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
