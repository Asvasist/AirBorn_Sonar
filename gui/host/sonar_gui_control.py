"""Board command sequencing. No GUI, serial-port or socket dependencies."""
import re
import time

STATUS = re.compile(r"STAGE2 (IDLE|WAIT_BUFFER|ACQUIRE|PRE_MOVE|MOVING|SETTLING|STOPPED|FAULT) "
                    r"cycle=(\d+) steps=(-?\d+) divisor=(\d+) receiver=(\d+) motor_pos=(-?\d+) DDR=(\d+)/(\d+)")
IDLE_STATES = {"IDLE", "STOPPED"}


def parse_steps(text, reverse=False):
    value = str(text).strip()
    if not re.fullmatch(r"[0-9]{1,6}", value) or not 1 <= int(value) <= 100000:
        raise ValueError("Enter a whole number of steps from 1 to 100000.")
    return -int(value) if reverse else int(value)


class ExperimentControl:
    def __init__(self, send, notify, now=time.monotonic):
        self.send, self.notify, self.now = send, notify, now
        self.serial_up = self.ethernet_up = False
        self.receiver_confirmed = self.fault_latched = False
        self.board_state = "UNKNOWN"
        self.phase = "idle"
        self.steps = self.divisor = self.cycle = self.ddr = self.capacity = 0
        self.requested = 0
        self.stored = False
        self.deadline = 0.0
        self.last_status = 0.0
        self.last_query = 0.0
        self.error = ""

    @property
    def can_start(self):
        return (self.serial_up and self.ethernet_up and self.receiver_confirmed and not self.fault_latched and self.phase == "idle" and
                self.board_state in IDLE_STATES and self.now() - self.last_status < 4.0)

    @property
    def drained(self):
        return (self.serial_up and self.board_state in IDLE_STATES and self.ddr == 0 and
                self.phase == "idle" and self.now() - self.last_status < 4.0)

    def serial_connection(self, up):
        self.serial_up = up
        self.board_state = "UNKNOWN"
        self.receiver_confirmed = False
        self.last_status = 0.0
        if up:
            self.error = ""
            self.send(b"s")
            self.last_query = self.now()
        else:
            self.phase = "idle"
            self.error = "Serial disconnected. Board operation is unknown; reconnect to inspect or stop it."

    def ethernet_connection(self, up):
        self.ethernet_up = up
        if not up and self.phase in {"prompt", "value", "starting"}:
            self.error = "Start cancelled because Ethernet disconnected."
            self.stop()

    def start(self, text, reverse=False):
        steps = parse_steps(text, reverse)
        if not self.can_start:
            raise ValueError("Wait for a fresh idle board status and an Ethernet connection before starting.")
        self.error = ""
        self.requested = steps
        self.stored = False
        self.phase = "prompt"
        self.deadline = self.now() + 10.0
        self.send(b"n")
        self.notify("Setting motor steps; waiting for the board's prompt.")

    def stop(self):
        if not self.serial_up:
            raise ValueError("Serial is disconnected; the GUI cannot send Stop to the board.")
        self.phase = "stopping"
        self.deadline = self.now() + 10.0
        # Transport gives X priority over queued status/settings commands.
        self.send(b"x")
        self.last_query = 0.0
        self.notify("Stop requested. Receiver stays open to save queued recordings.")

    def line(self, line):
        if "STAGE2 FAULT:" in line or "HEALTH FAULT" in line or "STAGE2 not ready:" in line:
            self.fault_latched = True
            self.board_state = "FAULT"
            self.phase = "idle"
            self.error = line
            return
        if self.phase == "prompt" and "Enter signed microstep count, then Enter:" in line:
            if not self.ethernet_up:
                self.stop()
                return
            self.send(f"{self.requested}\r".encode("ascii"))
            self.phase = "value"
            self.deadline = self.now() + 10.0
        if self.phase == "value" and "Step count stored" in line:
            self.stored = True
        if self.phase in {"prompt", "value", "starting"} and (
                "Invalid or rejected value" in line or "Cannot start:" in line):
            self.error = line
            self.stop()
            return
        match = STATUS.search(line)
        if not match:
            return
        state, cycle, steps, divisor, receiver, position, used, capacity = match.groups()
        self.board_state = state
        self.cycle, self.steps, self.divisor = int(cycle), int(steps), int(divisor)
        self.ddr, self.capacity = int(used), int(capacity)
        self.receiver_confirmed = receiver == "1"
        self.last_status = self.now()
        if state == "FAULT":
            self.fault_latched = True
            self.phase = "idle"
            self.error = "Board fault. Read the activity log; reset in Vitis only after saving queued captures."
        elif self.phase == "value" and self.stored:
            if state not in IDLE_STATES or self.steps != self.requested or not self.ethernet_up or receiver != "1":
                self.error = "Board settings/receiver confirmation did not match; start cancelled."
                self.stop()
            else:
                self.send(b"r")
                self.phase = "starting"
                self.deadline = self.now() + 10.0
                self.notify("Steps confirmed and receiver connected. Start sent.")
        elif self.phase == "starting" and state not in IDLE_STATES:
            self.phase = "idle"
            self.notify("Experiment running. The board repeats the cycle until Stop.")
        elif self.phase == "stopping" and state == "STOPPED":
            self.phase = "idle"
            self.notify("Board stopped. Waiting for queued recordings to transfer." if self.ddr else "Board stopped; DDR is empty.")

    def tick(self):
        now = self.now()
        if self.serial_up and self.phase not in {"prompt", "value"} and now - self.last_query >= 1.0:
            self.send(b"s")
            self.last_query = now
        if self.phase != "idle" and now >= self.deadline:
            if self.phase == "stopping":
                self.phase = "idle"
                self.board_state = "UNKNOWN"
                self.error = "Stop has not been confirmed. Check the serial log and board."
                self.notify(self.error)
            else:
                self.error = "Timed out waiting for board confirmation; start cancelled."
                self.stop()
