# Sonar desktop application

Tkinter application that configures the transmit signal, runs one-revolution
scans and stores every microphone capture the board sends. It talks to the
firmware exclusively over Ethernet; no serial connection is needed.

| Port | Transport | Purpose |
|---|---|---|
| 5001 | TCP | Capture records (header + PCM16 WAV payload, CRC32, per-record ACK) |
| 5002 | UDP | Board discovery (`SONARWHO` request, `SONARIP2` reply) |
| 5003 | TCP | Configuration RPC (`ASC1` framing): chirp parameters, WAV upload |
| 5004 | TCP | Console: firmware log stream and single-key commands |

## Install and run

Python 3.9+ with Tkinter (part of the standard Windows installer).

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\Start_Sonar_GUI.cmd
```

1. Program the board (see the top-level README) and connect the Ethernet cable
   directly to the laptop. The board uses a fixed link-local address.
2. Choose the recording folder and the Windows adapter name, then **Connect**.
   The board is discovered automatically; Connect sends status queries only.
3. Choose **Generate chirp** (start/stop frequency, duration, amplitude) or
   **Upload WAV**, then **Apply**. WAV files at other rates are resampled to
   96 kHz before upload.
4. Enter the number of capture positions per revolution (1–1000) and **Start**.
   The board performs one 360° scan and stops by itself. **Stop** aborts.

Transmit is limited to 49 ms; each capture is 50 ms of 16-channel audio,
decimated on the board to 96 kHz PCM16.

## Recordings

Each connection creates `session_<timestamp>/connection_<n>/` in the
recording folder. Every capture is stored as a WAV payload with a JSON
sidecar (cycle, motor position, trigger timestamp, transmit configuration)
before the board receives its acknowledgement, so an unacknowledged frame is
retransmitted rather than lost. Applied configurations and copies of
uploaded WAV files are kept in `configurations/`.

The board sends data in 30-second batches. Wait for the status line to show
`STOPPED` with an empty DDR queue before resetting the board; unsent captures
live in volatile RAM.

Command-line tools for headless use:

```powershell
py -3 host\receive_ethernet.py --output D:\SonarCaptures
py -3 host\sonar_configure.py --help
py -3 host\plot_ethernet.py <capture.bin>
```

## Code structure

| Module | Responsibility |
|---|---|
| `sonar_gui.py` | Tk widgets and event loop; the only module that touches Tk |
| `sonar_gui_control.py` | Start/stop sequencing and status parsing, no I/O |
| `sonar_gui_audio.py` | Audio validation and the cancellable configuration worker |
| `sonar_console_transport.py` | TCP 5004 console framing and worker thread |
| `sonar_gui_transport.py` | Capture receiver thread with reconnect |
| `receive_ethernet.py`, `capture_store.py` | Record protocol and atomic persistence |
| `sonar_configure.py`, `sonar_wav_format.py` | Configuration RPC client and WAV checks/resampling |
| `sonar_connection.py` | Adapter selection and board discovery |

Worker threads never call Tk; they post events to a queue drained by the
UI thread.

## Tests

```powershell
.\.venv\Scripts\python.exe -m unittest discover -s tests -v
```

The tests use fake sockets and a hidden Tk root; they never open a network
connection or move the motor.
