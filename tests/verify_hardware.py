"""Validate Stage 3 input evidence and its selected software contract."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--archive", type=Path, help="Optional original SynchronizationTesting.zip")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
hardware = root / "hardware"
manifest = json.loads((hardware / "manifest.json").read_text())
exports = json.loads((hardware / "export_inventory.json").read_text())

for name, entry in manifest["files"].items():
    actual = hashlib.sha256((hardware / "reference" / name).read_bytes()).hexdigest()
    assert actual == entry["sha256"], name

bd = json.loads((hardware / "reference" / "pdm_dma_bd.bd").read_text())["design"]
components = bd["components"]
def parameter(component, key):
    return int(components[component]["parameters"][key]["value"], 0)
def define(file, key):
    text = (root / "src" / file).read_text()
    match = re.search(r"^#define\s+" + key + r"\s+(0x[0-9a-fA-F]+|[0-9]+)U?\s*(?:/\*.*)?$", text, re.M)
    assert match, key
    return int(match[1], 0)

assert parameter("top_pdm_axis_capture_0", "WORDS_TO_CAPTURE") == define("sonar_mic_config.h", "SONAR_MIC_CAPTURE_WORDS") == 3750
assert parameter("axi_gpio_0", "C_GPIO_WIDTH") == define("sonar_profile.h", "SONAR_EXPECTED_GPIO_WIDTH") == 2
assert "axi_iic_0" in components
assert define("sonar_mic_config.h", "SONAR_MIC_ENABLE_HARDWARE") == 0
assert len({entry["sha256"] for entry in exports}) == 8
assert all(entry["capture_words"] == 750000 for entry in exports)
segments = bd["addressing"]["/processing_system7_0"]["address_spaces"]["Data"]["segments"]
for component, address in (("axi_dma_0", 0x40400000), ("axi_gpio_0", 0x41200000), ("axi_iic_0", 0x41600000)):
    assert int(segments["SEG_" + component + "_Reg"]["offset"], 0) == address
assert "top_pdm_axis_capture_0/start_capture" in bd["nets"]["axi_gpio_0_gpio_io_o"]["ports"]
assert "ssm2603_i2s_chirp_tx_0/start_async" in bd["nets"]["xlslice_0_Dout"]["ports"]
assert "ssm2603_i2s_chirp_tx_0/codec_ready" in bd["nets"]["xlslice_1_Dout"]["ports"]
for name in ("ssm2603_i2s_chirp_tx_0_tx_busy", "ssm2603_i2s_chirp_tx_0_tx_done"):
    assert len(bd["nets"][name]["ports"]) == 2
    assert any(port.startswith("ila_1/") for port in bd["nets"][name]["ports"])

motor = (hardware / "reference" / "motor_test_reference.txt").read_text(encoding="utf-8-sig")
assert "Driver input clock = 111111115 Hz" in motor
assert "PS I2C1" in motor and "MIO12" in motor and "MIO13" in motor
assert define("sonar_motor_config.h", "SONAR_MOTOR_ADDRESS") == 0x0e
assert define("sonar_motor_config.h", "SONAR_MOTOR_I2C_INPUT_HZ") == 111111115
assert define("sonar_motor_config.h", "SONAR_MOTOR_I2C_HZ") == 20000
assert define("sonar_motor_config.h", "SONAR_MOTOR_SPEED") == 30000000
assert define("sonar_motor_config.h", "SONAR_MOTOR_KEEPALIVE_MS") == 250

if args.archive:
    with zipfile.ZipFile(args.archive) as archive:
        for name, entry in manifest["files"].items():
            if "archive_entry" in entry:
                assert archive.read(entry["archive_entry"]) == (hardware / "reference" / name).read_bytes()
        for entry in exports:
            assert hashlib.sha256(archive.read(entry["archive_entry"])).hexdigest() == entry["sha256"]
print("PASS: unchanged input evidence, active BD contract, eight distinct older exports, PL guard and supplied PS motor settings.")
