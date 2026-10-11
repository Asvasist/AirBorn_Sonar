# Sonar desktop application

Tkinter application that configures the transmit signal, runs one-revolution
scans and stores every microphone capture the board sends. It talks to the
firmware exclusively over Ethernet; no serial connection is needed.

| Port | Transport | Purpose |
|---|---|---|
| 5001 | TCP | `SONAR3` / `READY3` handshake, `ASN3` v2 records (112-byte header + PCM16 WAV), CRC32 and `ACK3` |
| 5002 | UDP | Board discovery (`SONARWHO` request, `SONARIP2` reply) |
| 5003 | TCP | Configuration RPC (`ASC2` v2, 30-word replies): chirp parameters, WAV upload, boot/test identity |
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
   The board is discovered automatically. Connect/probe reads status and assigns
   a boot UUID if the firmware has none; it sends no playback or movement command.
3. Choose **Generate chirp** (start/stop frequency, duration, amplitude) or
   **Upload WAV**, then **Apply**. WAV files at other rates are resampled to
   96 kHz before upload.
4. Enter the number of capture positions per revolution (1–1000) and **Start**.
   The board performs one 360° scan and stops by itself. **Stop** aborts.

Transmit is limited to 49 ms; each capture is 50 ms of 16-channel audio,
decimated on the board to 96 kHz PCM16.
The current receive filter preserves 0–46 kHz, transitions from 46–50 kHz
before final decimation, and reports its passband edge in the control reply.

## Recordings

Connecting or receiving an idle batch marker creates no folders in the chosen
recording directory. Applied audio settings are held in a temporary cache until
a test produces data.

Every verified **Start** asks the board to prepare a fresh test UUID for the
current boot and applied configuration, then reserves a new `test_<timestamp>_<id>/` destination.
The folder is created only when the first valid completed capture arrives:

```text
D:\SonarCaptures\
    test_<timestamp>_<id>\
        test.json
        configurations\
            config_00001.json
            config_00001_source.wav     # uploaded-WAV mode
            config_00001_playback.wav   # uploaded-WAV mode
        second_<capture_time>\
            capture_0000000001.wav
            capture_0000000001.json
```

Every capture is stored with its JSON sidecar before the board receives its
acknowledgement, so an unacknowledged frame is retransmitted rather than lost.
Each test includes a snapshot of its applied transmit configuration and uploaded
WAV assets, keeping automatic analyzer mode functional. Idle markers are still
acknowledged but no longer create `second_*` folders or `batch.json` files.

The output root contains `.sonar_recording_index.json`, written before the
console Start command. It stores the durable mapping from `(boot_id, test_id)`
to a test destination and configuration snapshot. Keep it with the recordings;
it lets delayed or retried frames return to the same destination after a GUI
restart. Recording JSON sidecars and `test.json` also contain both identities.

**Stop** keeps the receiver open to save queued captures into that same test.
**Start** stays disabled until the board is idle/stopped, its status is fresh,
and DDR reports zero queued frames. The next Start reserves another test folder,
even if the audio settings are unchanged. Delayed records and identical retries
are routed by explicit boot/test identities. Sequence numbers identify captures
within that test; they are never used to assign a frame to a different test.
The `test.json` status is finalized when the board stops and its queue is empty.

Completed audio is saved as it is delivered rather than held until a full
revolution or several revolutions complete; the board has a bounded 32-slot
queue. The existing firmware still transfers in 30-second batches, so files can
appear after movement finishes or after Stop. This GUI update does not alter
motor control, transmit/receive timing or the filter. It uses the current
firmware protocol and prepares recording identities automatically.

If retained frames arrive from an earlier connection without a known test,
they are preserved in a separate `recovered_<timestamp>_<id>/` folder. No audio
is acknowledged as saved before it is durably written. Unknown retained frames
may lack their transmit configuration; use their original saved settings when
analyzing them. A board reboot starts a new boot namespace without changing
previously saved test folders or removing their routing entries. Each recording
socket accepts non-empty captures from one boot only; a change forces reconnect.

The board sends data in 30-second batches. Wait for the status line to show
`STOPPED` with an empty DDR queue before resetting the board; unsent captures
live in volatile RAM.

Command-line tools for headless use:

```powershell
py -3 host\receive_ethernet.py --output D:\SonarCaptures
py -3 host\sonar_configure.py --help
py -3 host\sonar_configure.py prepare-test
py -3 host\plot_ethernet.py <capture.bin>
```

GUI Start prepares its test identity automatically. For headless operation,
apply settings first and run `prepare-test` before the TCP 5004 console Start
command. The previous capture/control firmware versions are rejected explicitly;
existing recordings on disk remain untouched.

## Code structure

| Module | Responsibility |
|---|---|
| `sonar_gui.py` | Tk widgets and event loop; the only module that touches Tk |
| `sonar_gui_control.py` | Start/stop sequencing and status parsing, no I/O |
| `sonar_gui_audio.py` | Audio validation and the cancellable configuration worker |
| `sonar_console_transport.py` | TCP 5004 console framing and worker thread |
| `sonar_gui_transport.py` | Capture receiver thread with reconnect |
| `sonar_test_store.py` | Lazy test folders, durable identity routing and configuration snapshots |
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

86 tests retain the 65 existing feature regressions and add 21 protocol/identity checks.
The recording tests cover idle markers, invalid payloads, two successive tests,
early Stop and DDR draining, delayed/retried records, waveform provenance,
board resets and cancelled Starts that must leave no empty test folder.
New checks cover GUI restart recovery, an old 2.5 kHz frame after reset and a
new 8 kHz Start, boot changes on an existing socket, unknown test identities,
identity persistence failures, and CRC coverage of the new packet fields.
`tests/fixtures/wire_v2.bin` and `control_v2.bin` are generated by the actual
firmware encoders using `tests/fixtures/generate_wire_fixtures.c`; the previous
wire fixture is retained to test rejection of the older protocol.
