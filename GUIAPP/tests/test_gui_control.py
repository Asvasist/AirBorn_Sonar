"""Desktop command sequencing without connecting a board."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from sonar_gui_control import ExperimentControl, parse_steps


def status(state="IDLE", steps=20, receiver=1, used=0):
    return f"STAGE2 {state} cycle=3 steps={steps} divisor=1 receiver={receiver} motor_pos=60 DDR={used}/32"


class GuiControlTests(unittest.TestCase):
    def setUp(self):
        self.now = 10.0
        self.sent, self.log = [], []
        self.c = ExperimentControl(self.sent.append, self.log.append, lambda: self.now)
        self.c.serial_connection(True)
        self.c.ethernet_connection(True)
        self.c.line(status())

    def enter_value(self, steps="45", reverse=False):
        self.c.start(steps, reverse)
        self.c.line("Enter signed microstep count, then Enter:")
        self.c.line("Step count stored for every cycle. Press R once to repeat; X stops.")

    def test_start_requires_network_and_fresh_idle_and_board_receiver(self):
        self.assertTrue(self.c.can_start)
        self.c.ethernet_connection(False)
        with self.assertRaises(ValueError):
            self.c.start("20")
        self.c.ethernet_connection(True)
        self.c.line(status(receiver=0))
        self.assertFalse(self.c.can_start)
        self.c.line(status())
        self.now += 4
        self.assertFalse(self.c.can_start)
        self.assertEqual(self.sent, [b"s"])

    def test_confirmed_steps_before_start_and_only_one_start(self):
        self.enter_value()
        self.assertEqual(self.sent, [b"s", b"n", b"45\r"])
        self.c.line(status(steps=45))
        self.assertEqual(self.sent[-1], b"r")
        self.c.line(status("ACQUIRE", steps=45, used=1))
        self.c.line(status("MOVING", steps=45, used=1))
        self.now += 10
        self.c.tick()
        self.assertEqual(self.sent.count(b"r"), 1)
        self.assertFalse(self.c.can_start)

    def test_reverse_and_invalid_numbers(self):
        self.enter_value("100", True)
        self.assertEqual(self.sent[-1], b"-100\r")
        self.c.line(status(steps=-100))
        self.assertEqual(self.sent[-1], b"r")
        for value in ("0", "100001", "-20", "1.5", "NaN", "20\rr", "", "999999999"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_steps(value)
        self.assertEqual(parse_steps(" 100000 "), 100000)

    def test_mismatch_or_receiver_lost_never_starts(self):
        self.enter_value()
        self.c.line(status(steps=20))
        self.assertNotIn(b"r", self.sent)
        self.assertEqual(self.sent[-1], b"x")

    def test_stop_cancels_pending_prompt_and_late_confirmation(self):
        self.c.start("45")
        self.c.stop()
        self.c.line("Enter signed microstep count, then Enter:")
        self.c.line("Step count stored for every cycle.")
        self.c.line(status(steps=45))
        self.assertNotIn(b"r", self.sent)
        self.assertNotIn(b"45\r", self.sent)

    def test_network_loss_pending_start_stops_but_running_keeps_buffering(self):
        self.c.start("45")
        self.c.ethernet_connection(False)
        self.assertEqual(self.sent[-1], b"x")
        self.c.line(status("STOPPED"))
        self.c.ethernet_connection(True)
        self.enter_value()
        self.c.line(status(steps=45))
        self.c.line(status("WAIT_BUFFER", steps=45, used=32))
        before = list(self.sent)
        self.c.ethernet_connection(False)
        self.c.ethernet_connection(True)
        self.assertEqual(self.sent, before)

    def test_timeout_cancels_then_reports_unconfirmed_stop(self):
        self.c.start("45")
        self.now += 11
        self.c.tick()
        self.assertEqual(self.sent[-1], b"x")
        self.now += 11
        self.c.tick()
        self.assertIn("not been confirmed", self.c.error)
        self.assertFalse(self.c.can_start)

    def test_drain_requires_stopped_empty_fresh_status(self):
        self.c.line(status("MOVING", used=1))
        self.assertFalse(self.c.drained)
        self.c.stop()
        self.c.line(status("STOPPED", used=1))
        self.assertFalse(self.c.drained)
        self.c.line(status("STOPPED", used=0))
        self.assertTrue(self.c.drained)
        self.now += 4
        self.assertFalse(self.c.drained)

    def test_fault_latched_even_after_idle_report(self):
        self.c.line("HEALTH FAULT TIMEOUT")
        self.c.line(status())
        self.assertFalse(self.c.can_start)
        self.assertIn("HEALTH FAULT", self.c.error)

    def test_disconnect_invalidates_state_and_stop_cannot_be_claimed(self):
        self.c.serial_connection(False)
        self.assertFalse(self.c.can_start)
        self.assertFalse(self.c.drained)
        with self.assertRaises(ValueError):
            self.c.stop()


if __name__ == "__main__":
    unittest.main()
