# Python desktop controls

The desktop application replaces manual serial commands and the separate Ethernet receiver command. The existing board firmware still performs the timing, DMA capture, motor movement and DDR buffering. Continue launching that firmware in Vitis; SD-card boot is a separate future task.

## Install once

In PowerShell:

```powershell
cd D:\GUIAPP\host
py -3 -m pip install -r .\requirements-gui.txt
```

The app uses Python 3 with Tkinter and pyserial. Tkinter is normally included in a Windows Python installation. If Python reports that `tkinter` is missing, modify the Python installation to include Tcl/Tk. The existing command-line Ethernet receiver still needs only the standard library.

## Run an experiment

1. Connect USB/UART and the direct Ethernet cable. Run the existing application in Vitis and allow initialization to finish.
2. Close or disconnect the Vitis serial terminal so the GUI can own the COM port. Leave the firmware running. Close any separate `receive_ethernet.py` process; use one receiver at a time.
3. Double-click `host\Start_Sonar_GUI.cmd`, or run `py -3 .\sonar_gui.py` from the host folder.
4. Select the board's COM port. Choose the recording folder (default `D:\SonarCaptures`). Leave the adapter as `Ethernet` unless Windows uses a different name. Leave Board IP blank for discovery.
5. Click **Connect**. This opens serial at 115200 baud and starts Ethernet reception. It sends only status queries; it does not start acquisition or move the motor.
6. Once Start is enabled, enter the **steps per cycle**, choose the direction and click **Start experiment**. The GUI sends `N`, waits for the prompt, sends the number and Enter, confirms the returned settings and receiver status, then sends `R` once. The firmware repeats its cycle until stopped.
7. Click **Stop** to send `X`. The receiver stays connected and continues saving the remaining DDR queue. Wait for the board to report `STOPPED` and DDR `0/32` before resetting or powering off.
8. Click **Open folder** to inspect the current recording session. Click **Disconnect** or close the window when finished. During a run, closing first requests Stop and waits for queued recordings to save. If stopping or saving cannot be confirmed after 90 seconds, the GUI asks whether to continue waiting or disconnect anyway.

Step counts are in the Tic's current configured step units, shown by the divisor in the status line. Divisor 1 means full motor steps; divisor 8 means eighth-steps. This GUI reads the divisor but does not change it. Forward sends a positive count and Reverse a negative count; physical shaft direction depends on the wiring. Valid counts are 1–100000. Change steps between runs.

## Recording and reconnect behavior

- Ethernet transfer remains in 30-second batches. The first .bin can therefore take approximately 30 seconds after capture to appear. Batch/status markers create JSON metadata, not empty microphone files.
- Files use the existing `session_...\connection_...\second_...\capture_....bin` layout, with matching JSON metadata. Second folders refer to the board software capture-request timestamp, not laptop arrival time or a new FPGA timestamp.
- Each frame is checked and its raw bytes and metadata are saved before acknowledgement. The GUI uses the same receiver and storage code as the working command-line tool.
- Network failure triggers automatic reconnect every five seconds. A running experiment continues under the firmware's buffer policy: it pauses before a new capture when DDR is full and resumes when space becomes available. The GUI does not send another R on reconnect.
- Stop during a pending start cancels the command sequence. Stop is a software command through UART, not a physical emergency stop. If UART disconnects, the GUI cannot confirm or stop board activity until it reconnects.
- Closing waits for a stopped, empty DDR queue when possible. Choosing to disconnect anyway does not save queued data. Unacknowledged data remains in volatile board RAM and is lost on reset or power loss.
- The file-delivery counter counts received deliveries; retry after a lost acknowledgement can save a duplicate in another connection folder. Preserve metadata when processing recordings.
- The GUI displays the nominal RX duration from each frame. It does not change the XSA's chirp, capture length, channel packing or experiment timing.

## If connection fails

Check the activity log. A busy COM port usually means the Vitis terminal or another program still owns it. Use Disconnect, close the competing terminal and reconnect. Start also requires a recent compatible `STAGE2` status line with idle/stopped state and a connected receiver; firmware faults block Start.

For Ethernet, select the actual Windows adapter name. The helper reads the adapter address and discovers the board without changing Windows settings. Board and laptop still need compatible IPv4 subnets. The optional Board IP field selects an existing address; it does not reconfigure hardware. Socket error 10013 is a Windows/VPN/firewall permission issue, not a missing microphone file.

## Maintenance and tests

`sonar_gui.py` owns Tk widgets, `sonar_gui_control.py` owns command sequencing, and `sonar_gui_transport.py` owns worker-thread I/O. Workers never call Tk. `receive_ethernet.py`, `capture_store.py` and `sonar_connection.py` retain the shared wire protocol, persistence and network selection.

From D:\GUIAPP (the test command can use .\.venv\Scripts\python.exe instead of py -3):

```powershell
py -3 -m unittest discover -s tests -p "test_*.py" -v
```

Tests use fake ports and sockets; they do not actuate the board. Hidden Tk tests skip when a display is unavailable. The application source and FPGA are unchanged by this desktop addition, and no firmware rebuild is needed solely for the GUI.

