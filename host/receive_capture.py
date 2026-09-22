#!/usr/bin/env python3
"""Request the last completed capture with 'd', save it and optionally plot it."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

from capture_protocol import CaptureDecoder, ProtocolError


def receive(port, timeout):
    decoder = CaptureDecoder()
    port.reset_input_buffer()
    port.write(b"d")
    deadline = time.monotonic() + timeout
    while not decoder.complete and time.monotonic() < deadline:
        decoder.feed(port.read(4096))
    decoder.require_complete()
    return bytes(decoder.data), decoder.metadata


def save_capture(output, data, metadata, overwrite=False):
    """Called only after verification. Exclusive create protects existing files."""
    sidecar = output.with_suffix(".json")
    if sidecar == output:
        raise ValueError("Use a .bin output filename.")
    for path in (output, sidecar):
        if path.exists() and not overwrite:
            raise FileExistsError(f"{path} already exists; choose a new name or use --overwrite.")
    with output.open("wb" if overwrite else "xb") as stream:
        stream.write(data)
    with sidecar.open("w" if overwrite else "x", encoding="utf-8") as stream:
        json.dump(metadata, stream, indent=2)
        stream.write("\n")
    return sidecar


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Board UART port, for example COM5")
    parser.add_argument("--output", type=Path, default=Path("capture.bin"))
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30, help="Whole transfer timeout in seconds")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--plot", action="store_true")
    args = parser.parse_args()
    if args.timeout <= 0 or args.baud <= 0:
        parser.error("timeout and baud must be positive")
    if args.output.suffix.lower() != ".bin":
        parser.error("output must end in .bin")
    if not args.output.parent.is_dir():
        parser.error("output directory does not exist")
    if not args.overwrite:
        for path in (args.output, args.output.with_suffix(".json")):
            if path.exists():
                parser.error(f"{path} exists; use a new filename or --overwrite")
    try:
        import serial
    except ImportError:
        parser.exit(1, "Install the receiver dependency: py -m pip install pyserial\n")
    try:
        # Configure control lines before opening; do not deliberately reset the board.
        with serial.Serial(port=None, baudrate=args.baud, timeout=0.1, write_timeout=1) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            print("Requesting the completed capture. Keep the application running.")
            data, metadata = receive(port, args.timeout)
        sidecar = save_capture(args.output, data, metadata, args.overwrite)
    except (OSError, ValueError, serial.SerialException) as error:
        parser.exit(1, f"Download failed: {error}\n")
    print(f"Saved {len(data)} verified bytes to {args.output.resolve()}")
    print(f"Metadata: {sidecar.resolve()} (CRC32 {metadata['crc32']})")
    print("Transfer integrity verified; check the plots to assess the microphone signal.")
    if args.plot:
        subprocess.run([sys.executable, str(Path(__file__).with_name("pdm_plot.py")),
                        str(args.output), "--pdm-hz", str(metadata["pdm_hz"]),
                        "--bit-order", metadata["bit_order"], "--words", str(metadata["words"])],
                       check=True)


if __name__ == "__main__":
    main()
