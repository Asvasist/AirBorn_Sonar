#!/usr/bin/env python3
"""Plot a short PDM capture dumped from the Zybo (sonar_mic_buffer).

Made for the 3750-word (50 ms) capture the PL actually produces, which is far
too short to judge by ear. Shows four panels:

  1. raw PDM bit density  - a moving average of the 1-bit stream. This is the
     locally averaged bit stream. A mean near 0.5 alone cannot establish
     whether a microphone is working or whether sound is present.
  2. demodulated waveform - after low-pass filtering and decimation.
  3. spectrum             - where the energy actually is.
  4. spectrogram          - how frequency changes over time. A chirp shows up
     here as a diagonal streak; compare it with the transmitted waveform.

Needs:  pip install numpy scipy matplotlib
Usage:  py pdm_plot.py capture.bin
        py pdm_plot.py capture.bin --decim 12        # 200 kHz, keeps ultrasound
        py pdm_plot.py capture.bin --bit-order lsb
        py pdm_plot.py capture.bin --save plot.png   # write instead of show
        py pdm_plot.py capture.bin --wav out.wav     # also write audio
"""
import argparse
import os
import sys
import wave

import numpy as np
from scipy import signal

import matplotlib
import matplotlib.pyplot as plt


def stages(q):
    """Split decimation factor q into stages of at most 10 each."""
    primes, n, p = [], q, 2
    while n > 1:
        while n % p == 0:
            primes.append(p)
            n //= p
        p += 1
    out, cur = [], 1
    for f in sorted(primes, reverse=True):
        if cur * f <= 10:
            cur *= f
        else:
            out.append(cur)
            cur = f
    out.append(cur)
    return [s for s in out if s > 1]


def unpack(words, order):
    if order == "msb":   # bit 31 is the oldest sample in each word
        return np.unpackbits(words.astype(">u4").view(np.uint8))
    return np.unpackbits(words.astype("<u4").view(np.uint8), bitorder="little")


def pdm_to_pcm(bits, decim):
    x = bits.astype(np.float32) * 2.0 - 1.0
    for q in stages(decim):
        if len(x) <= 27 * q:          # decimate needs room for its filter
            return np.array([], dtype=np.float32)
        x = signal.decimate(x, q, ftype="fir", zero_phase=True).astype(np.float32)
    return x - np.mean(x)


def write_wav(path, x, rate):
    peak = float(np.max(np.abs(x))) or 1.0
    pcm = np.clip(x / peak * 0.9 * 32767.0, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("--pdm-hz", type=int, default=2_400_000, help="PDM clock (SONAR_MIC_PDM_HZ)")
    ap.add_argument("--decim", type=int, default=50, help="decimation; 2.4 MHz/50 = 48 kHz")
    ap.add_argument("--bit-order", choices=["msb", "lsb"], default="msb")
    ap.add_argument("--words", type=int, default=0,
                    help="use only the first N words (e.g. 3750 if the rest is padding)")
    ap.add_argument("--save", help="write the figure to this file instead of opening a window")
    ap.add_argument("--wav", help="also write the demodulated audio here")
    a = ap.parse_args()

    if a.pdm_hz <= 0 or a.decim < 1 or a.words < 0 or a.pdm_hz % a.decim:
        ap.error("use positive PDM rate and decimation with an integer output rate; words must be nonnegative")
    with open(a.input, "rb") as stream:
        raw = stream.read()
    if len(raw) % 4:
        sys.exit("input length must be a multiple of four bytes")
    words = np.frombuffer(raw, dtype="<u4")
    if words.size == 0:
        sys.exit("input file is empty")

    print(f"file: {words.size} words")
    if a.words:
        if a.words > words.size:
            sys.exit("requested word count exceeds the file length")
        words = words[:a.words]
    # Zero-valued words are data too. Exported files contain no DMA padding.

    bits = unpack(words, a.bit_order)
    density = bits.mean()
    duration = bits.size / a.pdm_hz
    rate = a.pdm_hz // a.decim
    print(f"{words.size} words = {bits.size} PDM bits = {duration * 1000:.1f} ms "
          f"-> {rate} Hz after decimation")
    print(f"mean bit density = {density:.4f} (0.5 alone does not establish signal quality)")
    if density in (0.0, 1.0):
        print("WARNING: captured bits are constant; check capture, microphone clock and wiring")

    pcm = pdm_to_pcm(bits, a.decim)
    if pcm.size < 16:
        sys.exit(f"only {bits.size} PDM bits - too short for decimation by {a.decim}. "
                 f"Try a smaller --decim.")
    print(f"demodulated: {pcm.size} samples, rms={np.sqrt(np.mean(pcm ** 2)):.5f}, "
          f"peak={np.max(np.abs(pcm)):.5f}")

    if a.wav:
        write_wav(a.wav, pcm, rate)
        print(f"wrote {a.wav}")

    # ---- plots -------------------------------------------------------------
    fig, ax = plt.subplots(4, 1, figsize=(11, 10))
    fig.suptitle(f"{os.path.basename(a.input)} - {bits.size} PDM bits "
                 f"({duration * 1000:.1f} ms at {a.pdm_hz / 1e6:.2f} MHz, "
                 f"{a.bit_order} bit order)")

    # 1. moving-average bit density
    win = max(16, min(256, bits.size // 400))
    dens = np.convolve(bits.astype(np.float32), np.ones(win) / win, mode="valid")
    ax[0].plot(np.arange(dens.size) / a.pdm_hz * 1000, dens, lw=0.7)
    ax[0].axhline(0.5, color="r", ls="--", lw=0.8, label="0.5 reference")
    ax[0].set(title=f"1. Raw PDM bit density (moving average of {win} bits)",
              xlabel="ms", ylabel="fraction of 1s", ylim=(0, 1))
    ax[0].legend(loc="upper right", fontsize=8)

    # 2. demodulated waveform
    ax[1].plot(np.arange(pcm.size) / rate * 1000, pcm, lw=0.7)
    ax[1].set(title=f"2. Demodulated waveform at {rate} Hz", xlabel="ms", ylabel="amplitude")

    # 3. spectrum
    nfft = min(8192, 1 << int(np.floor(np.log2(max(pcm.size, 2)))))
    freq, psd = signal.welch(pcm, rate, nperseg=min(nfft, pcm.size))
    ax[2].semilogy(freq / 1000, np.maximum(psd, 1e-20), lw=0.8)
    ax[2].set(title="3. Spectrum", xlabel="kHz", ylabel="power")
    ax[2].grid(alpha=0.3)

    # 4. spectrogram - a chirp appears as a diagonal streak
    nper = max(32, min(256, pcm.size // 16))
    if pcm.size >= nper * 2:
        f2, t2, sxx = signal.spectrogram(pcm, rate, nperseg=nper, noverlap=nper // 2)
        ax[3].pcolormesh(t2 * 1000, f2 / 1000, 10 * np.log10(np.maximum(sxx, 1e-20)),
                         shading="auto")
        ax[3].set(title="4. Spectrogram (a chirp = diagonal streak)", xlabel="ms", ylabel="kHz")
    else:
        ax[3].text(0.5, 0.5, "capture too short for a spectrogram",
                   ha="center", va="center", transform=ax[3].transAxes)
        ax[3].set_axis_off()

    fig.tight_layout(rect=(0, 0, 1, 0.97))
    if a.save:
        fig.savefig(a.save, dpi=130)
        print(f"wrote {a.save}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
