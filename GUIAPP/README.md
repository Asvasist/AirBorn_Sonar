# Airborne Sonar desktop app

Installed in `D:\GUIAPP`. This application controls the existing firmware through USB serial and saves microphone recordings over Ethernet. It does not build or program the board.

## Daily use

1. Run the firmware in Vitis. Close/disconnect its serial terminal, leaving the firmware running. Close any separate Ethernet receiver process.
2. Double-click `D:\GUIAPP\Start_Sonar_GUI.cmd`.
3. Select the board's COM port. Leave the output folder as `D:\SonarCaptures`, or choose another. Select the correct Ethernet adapter name; leave Board IP blank for discovery.
4. Click **Connect** and wait for Start to become available.
5. Enter the motor step count, choose direction and click **Start experiment**. The app performs N, number, Enter and R in order, checking the board's replies first.
6. Click **Stop** to send X. Leave the app connected until the board reports STOPPED and DDR is empty. Recordings arrive in 30-second batches.
7. Click **Open folder** to see the .bin recordings and JSON metadata. Disconnect or close the window when finished.

Start requires both a confirmed serial status and a connected Ethernet receiver. The existing firmware repeats its cycle and pauses automatically if DDR fills. Ethernet reconnects automatically; it does not issue another Start command. A lost serial connection means the app cannot confirm or stop board activity until reconnection.

The displayed divisor defines the step units: divisor 1 means full steps, divisor 8 means eighth-steps. The GUI reads this setting without changing it. Physical forward/reverse direction depends on motor wiring.

## Python setup

The installer creates a local `.venv` when a suitable Windows Python is available. The launcher prefers that environment. If setup could not complete, run in PowerShell:

```powershell
cd D:\GUIAPP
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r .\host\requirements-gui.txt
```

Then open `Start_Sonar_GUI.cmd`. Python must include Tkinter (the Tcl/Tk option in the Windows Python installer).

## Files

- `host/sonar_gui.py`: desktop window.
- `host/sonar_gui_control.py`: step-setting, Start/Stop and status handling.
- `host/sonar_gui_transport.py`: serial and Ethernet worker threads.
- Other host modules: existing capture protocol, file persistence, network discovery and command-line tools.
- `tests/`: Python tests using simulated devices; no hardware is activated.
- `docs/gui_quickstart.md`: detailed behavior and troubleshooting.

Run tests from this folder with `.\.venv\Scripts\python.exe -m unittest discover -s tests -p "test_*.py" -v`.

Firmware timing, FPGA configuration and boot from SD remain controlled outside this desktop app. The GUI displays actual nominal capture duration reported by the board; it does not change it.
