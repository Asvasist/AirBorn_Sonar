"""Decode the sonar UART capture format, independently of serial-port access."""
import struct
import zlib

HEADER = struct.Struct("<4sBBHIIIHH")
MAX_BYTES = 0x03FFFFFF


class ProtocolError(ValueError):
    pass


class CaptureDecoder:
    """Accept fragmented packets and diagnostic text between packets.

    A completed capture requires contiguous data, valid packet CRCs and a
    matching whole-capture CRC. No partial capture is exposed as complete.
    """

    def __init__(self):
        self.pending = bytearray()
        self.data = bytearray()
        self.metadata = None
        self.complete = False

    def feed(self, chunk):
        if self.complete:
            return
        self.pending.extend(chunk)
        while not self.complete:
            start = self.pending.find(b"ASPD")
            if start < 0:
                # Keep a possible partial magic sequence; discard console text.
                self.pending[:] = self.pending[-3:]
                return
            del self.pending[:start]
            if len(self.pending) < HEADER.size:
                return
            _, version, kind, size, ident, offset, total, count, reserved = HEADER.unpack_from(self.pending)
            if (version != 1 or kind not in (1, 2, 3) or size != HEADER.size or
                    reserved or count > 128 or not ident or
                    not 0 < total <= MAX_BYTES or total % 4):
                raise ProtocolError("Invalid capture packet header; retry the download.")
            packet_size = HEADER.size + count + 4
            if len(self.pending) < packet_size:
                return
            packet = bytes(self.pending[:packet_size])
            del self.pending[:packet_size]
            expected, = struct.unpack_from("<I", packet, packet_size - 4)
            if zlib.crc32(packet[:-4]) != expected:
                raise ProtocolError("Packet CRC mismatch; no capture saved.")
            payload = packet[HEADER.size:-4]
            self._packet(kind, ident, offset, total, payload)

    def _packet(self, kind, ident, offset, total, payload):
        if kind == 1:
            if self.metadata is not None or offset or len(payload) != 24:
                raise ProtocolError("Unexpected capture start.")
            generation, hz, request, completion, tick_hz, flags = struct.unpack("<6I", payload)
            if not hz or not tick_hz or flags not in (5, 7):
                raise ProtocolError("Unsupported capture metadata.")
            self.metadata = dict(transfer_id=ident, bytes=total, words=total // 4,
                                 generation=generation, pdm_hz=hz, request_tick=request,
                                 completion_tick=completion, tick_hz=tick_hz,
                                 word_endianness="little", bit_order="msb",
                                 overflow_checked=bool(flags & 2),
                                 nominal_duration_seconds=total * 8 / hz)
            return
        if self.metadata is None:
            raise ProtocolError("Capture start missing; wait for the old download to finish and retry.")
        if ident != self.metadata["transfer_id"] or total != self.metadata["bytes"]:
            raise ProtocolError("Capture identity or size changed during download.")
        if offset != len(self.data):
            raise ProtocolError("Missing, duplicated or reordered capture data.")
        if kind == 2:
            if not payload or len(self.data) + len(payload) > total:
                raise ProtocolError("Invalid data length.")
            self.data.extend(payload)
        elif len(payload) != 4 or len(self.data) != total:
            raise ProtocolError("Incomplete capture at end of transfer.")
        else:
            checksum, = struct.unpack("<I", payload)
            if zlib.crc32(self.data) != checksum:
                raise ProtocolError("Whole-capture CRC mismatch; no capture saved.")
            self.metadata["crc32"] = f"{checksum:08x}"
            self.complete = True

    def require_complete(self):
        if not self.complete:
            raise ProtocolError("Download incomplete; no capture saved. Check MIC READY, COM port and baud rate.")
