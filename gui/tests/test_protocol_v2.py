"""Protocol compatibility and recording identity regressions, without hardware."""
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from capture_store import CaptureStore, write_json
from receive_ethernet import HEADER, decode_header, receive_record
from sonar_configure import ControlClient, crc32, packet
from sonar_gui_audio import ConfigurationWorker, matching_configuration
from sonar_gui_transport import BootBoundStore
from sonar_test_store import TestCaptureStore
from test_ethernet import BOOT, TEST, BOOT2, TEST2, Fragmented, record
from test_gui_audio import reply, Sock
import test_gui_view as view


class Connected(Fragmented):
    def close(self):
        pass


class ProtocolV2Tests(unittest.TestCase):
    def test_c_control_fixture_and_identity_word_order(self):
        fixture = Path(__file__).parent / "fixtures/control_v2.bin"
        sock = Connected(fixture.read_bytes())
        with ControlClient("fake", connected_socket=sock) as client:
            value = client.status()
        self.assertEqual(value["boot_id"], BOOT)
        self.assertEqual(value["test_id"], TEST)
        self.assertEqual(value["sample_hz"], 96000)
        self.assertEqual(value["rx_filter_cutoff_hz"], 46000)
        self.assertEqual(sock.sent[:8], b"ASC2\x02\x00\x1c\x00")

    def test_legacy_capture_fixture_is_rejected(self):
        fixture = Path(__file__).parent / "fixtures/wire_v1.bin"
        with self.assertRaises(ValueError):
            decode_header(fixture.read_bytes())

    def test_legacy_and_corrupted_control_responses_are_rejected(self):
        fixture = (Path(__file__).parent / "fixtures/control_v2.bin").read_bytes()
        legacy = bytearray(fixture)
        legacy[:4] = b"ASC1"
        legacy[4] = 1
        struct.pack_into("<I", legacy, 24, crc32(legacy[:24]))
        damaged_header = bytearray(fixture)
        damaged_header[12] ^= 1
        damaged_payload = bytearray(fixture)
        damaged_payload[-1] ^= 1
        for data in (legacy, damaged_header, damaged_payload):
            with self.subTest(data=bytes(data[:8])):
                with ControlClient("fake", connected_socket=Connected(bytes(data))) as client:
                    with self.assertRaises(ValueError):
                        client.status()

    def test_missing_or_corrupted_capture_identity_has_no_ack(self):
        with tempfile.TemporaryDirectory() as folder:
            store = CaptureStore(folder)
            missing = record(boot_id="0" * 32)
            damaged = bytearray(record())
            damaged[92] ^= 1
            for data in (missing, damaged):
                sock = Fragmented(bytes(data))
                with self.assertRaises(ValueError):
                    receive_record(sock, store)
                self.assertEqual(sock.sent, b"")
            self.assertFalse(list(Path(folder).iterdir()))

    def test_boot_assignment_and_existing_boot_do_not_issue_motion_commands(self):
        client = ControlClient.__new__(ControlClient)
        calls = []
        def request(op, payload=b""):
            calls.append((op, payload))
            return reply(boot_id="0" * 32) if op == 1 else reply(boot_id=payload.hex())
        client.request = request
        with patch("sonar_configure.uuid.uuid4", return_value=uuid.UUID(hex=BOOT)):
            self.assertEqual(client.ensure_boot_identity()["boot_id"], BOOT)
        self.assertEqual(calls, [(1, b""), (10, bytes.fromhex(BOOT))])
        calls.clear()
        client.request = lambda op, payload=b"": (calls.append((op, payload)) or reply())
        self.assertEqual(client.ensure_boot_identity()["boot_id"], BOOT)
        self.assertEqual(calls, [(1, b"")])

    def test_boot_assignment_requires_exact_board_confirmation(self):
        client = ControlClient.__new__(ControlClient)
        client.request = lambda op, payload=b"": reply(boot_id="0" * 32 if op == 1 else BOOT2)
        with patch("sonar_configure.uuid.uuid4", return_value=uuid.UUID(hex=BOOT)):
            with self.assertRaises(ValueError):
                client.ensure_boot_identity()

    def test_every_start_prepares_a_fresh_id_with_expected_boot_and_configuration(self):
        client = ControlClient.__new__(ControlClient)
        calls = []
        def request(op, payload):
            calls.append((op, payload))
            return reply(test_id=payload[16:32].hex())
        client.request = request
        with patch("sonar_configure.uuid.uuid4", side_effect=[uuid.UUID(hex=TEST), uuid.UUID(hex=TEST2)]):
            first = client.prepare_test(BOOT, 7)
            second = client.prepare_test(BOOT, 7)
        self.assertNotEqual(first["test_id"], second["test_id"])
        self.assertEqual([op for op, _ in calls], [11, 11])
        for _, payload in calls:
            self.assertEqual(len(payload), 36)
            self.assertEqual(payload[:16], bytes.fromhex(BOOT))
            self.assertEqual(struct.unpack("<I", payload[32:])[0], 7)

    def test_prepare_rejects_invalid_ids_and_mismatched_confirmation(self):
        client = ControlClient.__new__(ControlClient)
        calls = []
        client.request = lambda op, payload: (calls.append(op) or reply(test_id=TEST2))
        for boot, config_id in (("0" * 32, 7), ("bad", 7), (BOOT, 0), (BOOT, 65536)):
            with self.assertRaises(ValueError):
                client.prepare_test(boot, config_id)
        self.assertFalse(calls)
        with patch("sonar_configure.uuid.uuid4", return_value=uuid.UUID(hex=TEST)):
            with self.assertRaises(ValueError):
                client.prepare_test(BOOT, 7)

    def test_verify_worker_prepares_identity_before_success_event(self):
        events, calls = [], []
        expected = reply()
        class Client:
            def __init__(self, *args, **kwargs):
                pass
            def __enter__(self):
                return self
            def __exit__(self, *args):
                pass
            def status(self):
                calls.append("status")
                return expected
            def prepare_test(self, boot, config_id):
                calls.append((boot, config_id))
                return reply(test_id=TEST2)
        worker = ConfigurationWorker(1, "verify", SimpleNamespace(port=5001), None,
                                     lambda _, event: events.append(event), expected=expected,
                                     connector=lambda args, log: (Sock(), "fake", "fake"),
                                     client_factory=Client)
        worker.run()
        self.assertEqual(calls, ["status", (BOOT, 7)])
        self.assertEqual(next(e[2]["test_id"] for e in events if e[1] == "verify"), TEST2)

    def test_same_configuration_after_a_different_boot_never_matches(self):
        self.assertFalse(matching_configuration(reply(), reply(boot_id=BOOT2)))
        self.assertFalse(matching_configuration(reply(boot_id="0" * 32), reply(boot_id="0" * 32)))

    def test_recording_socket_rejects_a_second_boot_without_ack(self):
        with tempfile.TemporaryDirectory() as folder:
            target = TestCaptureStore(folder)
            bound = BootBoundStore(target)
            first = Fragmented(record())
            receive_record(first, bound)
            second = Fragmented(record(seq=2, boot_id=BOOT2))
            with self.assertRaisesRegex(ValueError, "Boot identity changed"):
                receive_record(second, bound)
            self.assertEqual(second.sent, b"")
            self.assertEqual(len(list(Path(folder).rglob("capture_*.bin"))), 1)


def configuration(cache, boot=BOOT, start=2500):
    configs = cache / "configurations"
    configs.mkdir(parents=True, exist_ok=True)
    value = reply(config_id=1, boot_id=boot, start_hz=start, sample_hz=96000)
    write_json(configs / "config_00001.json", dict(configuration=value, protocol="ASC2-v2"))


def capture_meta(boot=BOOT, test=TEST):
    return dict(sequence=1, cycle=1, trigger_request_us=100, channels=16,
                config_id=1, tx_mode="GENERATE", tx_duration_us=35000,
                boot_id=boot, test_id=test)


class DurableIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cache = self.root / "cache"
        self.output = self.root / "recordings"
        configuration(self.cache)
        self.store = TestCaptureStore(self.output)

    def begin(self, boot=BOOT, test=TEST):
        return self.store.begin_test(1, 3, self.cache, 1, boot_id=boot, test_id=test)

    def test_late_pre_reset_capture_keeps_its_old_folder_and_2500_hz_settings(self):
        old_folder = self.begin()
        self.store.reset_board()
        configuration(self.cache, boot=BOOT2, start=8000)
        new_folder = self.begin(BOOT2, TEST2)
        old = self.store.save(capture_meta(), bytes(4))
        new = self.store.save(capture_meta(BOOT2, TEST2), b"\x01" * 4)
        old_json = json.loads(old.with_suffix(".json").read_text())
        new_json = json.loads(new.with_suffix(".json").read_text())
        self.assertEqual(old.parents[1], old_folder)
        self.assertEqual(new.parents[1], new_folder)
        self.assertEqual(old_json["transmit_configuration"]["configuration"]["start_hz"], 2500)
        self.assertEqual(new_json["transmit_configuration"]["configuration"]["start_hz"], 8000)
        self.assertEqual(old_json["configuration_status"], "matched")

    def test_gui_restart_preserves_the_reserved_route_and_configuration(self):
        folder = self.begin()
        configuration(self.cache, start=8000)
        restarted = TestCaptureStore(self.output)
        path = restarted.save(capture_meta(), bytes(4))
        value = json.loads(path.with_suffix(".json").read_text())
        self.assertEqual(path.parents[1], folder)
        self.assertEqual(value["transmit_configuration"]["configuration"]["start_hz"], 2500)
        self.assertEqual(restarted.save(capture_meta(), bytes(4)), path)
        self.assertEqual(len(list(folder.rglob("capture_*.bin"))), 1)

    def test_unknown_identity_is_recovered_without_active_settings(self):
        active = self.begin()
        path = self.store.save(capture_meta(BOOT2, TEST2), bytes(4))
        value = json.loads(path.with_suffix(".json").read_text())
        self.assertNotEqual(path.parents[1], active)
        self.assertTrue(path.parents[1].name.startswith("recovered_"))
        self.assertEqual(value["configuration_status"], "unavailable")
        self.assertNotIn("transmit_configuration", value)

    def test_wrong_boot_configuration_cache_cannot_reserve_a_test(self):
        with self.assertRaises(ValueError):
            self.begin(BOOT2, TEST2)
        self.assertFalse(self.output.exists())

    def test_failed_durable_route_write_prevents_start_destination(self):
        with patch("sonar_test_store.write_json", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                self.begin()
        self.assertFalse(self.store.runs)
        self.assertFalse(list(self.output.glob("test_*")))

    def test_wrong_test_configuration_is_marked_mismatch(self):
        folder = self.begin()
        path = self.store.save(capture_meta(), bytes(4))
        manifest = folder / "configurations/config_00001.json"
        value = json.loads(manifest.read_text())
        value["recording_identity"]["test_id"] = TEST2
        write_json(manifest, value)
        second = self.store.save(dict(capture_meta(), sequence=2, cycle=2), bytes(4))
        self.assertEqual(json.loads(second.with_suffix(".json").read_text())["configuration_status"], "mismatch")

    def test_invalid_existing_index_is_reported_without_overwriting_it(self):
        self.output.mkdir()
        index = self.output / ".sonar_recording_index.json"
        invalid = b'{"version":1,"tests":[{"boot_id":"invalid"}]}\n'
        index.write_bytes(invalid)
        with self.assertRaisesRegex(ValueError, "Cannot load recording index"):
            TestCaptureStore(self.output)
        self.assertEqual(index.read_bytes(), invalid)


class IdentityViewTests(unittest.TestCase):
    def setUp(self):
        view.GuiViewTests.setUp(self)

    def tearDown(self):
        view.GuiViewTests.tearDown(self)

    def test_unconfirmed_identity_never_sends_positions_or_start(self):
        app = self.app
        app.handle("console", True)
        app.handle("ethernet", True)
        app.control.audio_configuration(view.APPLIED)
        app.handle("line", view.status())
        app.pending_start = ("3", False)
        app.handle_audio(app.audio_token, "verify", dict(view.APPLIED, boot_id="0" * 32))
        self.assertEqual(app.console_worker.sent, [b"s"])
        self.assertFalse(app.ethernet_worker.tests)

    def test_delayed_record_and_folder_cannot_replace_current_test_display(self):
        app = self.app
        app.pending_identity = (BOOT2, TEST2)
        app.session_folder = Path("D:/new_test")
        app.handle("folder", "D:/old_test")
        app.handle("record", ("D:/old_test/capture_1.bin", dict(capture_meta(), payload_bytes=4)))
        self.assertEqual(app.session_folder, Path("D:/new_test"))
        self.assertEqual(app.deliveries, 0)

    def test_current_filter_passband_is_shown(self):
        self.app.render()
        self.assertIn("0–46 kHz", str(self.app.audio_note["text"]))


if __name__ == "__main__":
    unittest.main()
