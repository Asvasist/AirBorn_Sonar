#!/usr/bin/env python3
"""Plot one of 16 channels from a Stage 2 capture and its JSON sidecar."""
import argparse
import json
from pathlib import Path


def channel_bits(payload, channel):
    if not 0 <= channel < 16 or len(payload) % 4:
        raise ValueError("Use channel 0..15 and a whole number of 32-bit words")
    # DDR packing: byte 0 rising, byte 1 falling; repeated for next PDM cycle.
    return [((payload[i + (channel // 8)] >> (channel % 8)) & 1)
            for i in range(0, len(payload), 2)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--channel", type=int, choices=range(16), default=0)
    parser.add_argument("--decimate", type=int, default=50)
    parser.add_argument("--save", type=Path)
    args = parser.parse_args()
    if args.decimate < 1:
        parser.error("--decimate must be positive")
    import numpy as np
    import matplotlib.pyplot as plt
    meta = json.loads(args.capture.with_suffix(".json").read_text())
    bits = np.asarray(channel_bits(args.capture.read_bytes(), args.channel), dtype=float)
    n = len(bits) // args.decimate
    if n == 0:
        parser.error("Capture too short for chosen decimation")
    # Boxcar density is a diagnostic view, not a calibrated acoustic filter.
    waveform = (bits[:n * args.decimate].reshape(n, args.decimate).mean(axis=1) * 2 - 1)
    times = (np.arange(n) + .5) * args.decimate / meta["pdm_hz"] * 1000
    plt.plot(times, waveform)
    plt.xlabel("Time from capture start (ms)")
    plt.ylabel("PDM density (diagnostic)")
    plt.title(f"Cycle {meta['cycle']}, microphone channel {args.channel}, flags 0x{meta['flags']:x}")
    plt.grid(True)
    plt.tight_layout()
    if args.save:
        plt.savefig(args.save)
    else:
        plt.show()


if __name__ == "__main__":
    main()
