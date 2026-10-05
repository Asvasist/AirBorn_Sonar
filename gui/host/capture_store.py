"""Store raw PDM frames by their board capture-time second."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def write_atomic(path, data):
    """Publish a complete file only after its content has been flushed."""
    temporary = path.with_name(path.name + ".part")
    with temporary.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def write_json(path, value):
    write_atomic(path, (json.dumps(value, indent=2) + "\n").encode("utf-8"))


class CaptureStore:
    def __init__(self, folder):
        self.folder = Path(folder)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.batch_us = None

    def save(self, meta, payload):
        second = meta["trigger_request_us"] // 1_000_000
        folder = self.folder / f"second_{second:010d}"
        folder.mkdir(parents=True, exist_ok=True)
        if not payload:
            self.batch_us = meta["trigger_request_us"]
            write_json(folder / "batch.json", {
                "kind": "batch_start", "board_batch_release_us": self.batch_us,
                "pc_received_utc": utc_now(),
                "note": "A batch marker is not microphone data. Recordings are capture_*.bin files.",
            })
            return None

        stem = folder / f"capture_{meta['sequence']:010d}"
        binary, description = stem.with_suffix(".bin"), stem.with_suffix(".json")
        if binary.exists() and binary.read_bytes() != payload:
            raise ValueError(f"Conflicting duplicate: {binary}")
        if description.exists():
            old = json.loads(description.read_text(encoding="utf-8"))
            if any(old.get(key) != value for key, value in meta.items()):
                raise ValueError(f"Conflicting metadata: {description}")
        else:
            old = dict(meta, pc_received_utc=utc_now(), board_batch_release_us=self.batch_us)

        # Rewriting an identical retry is safe and re-flushes both files before ACK.
        write_atomic(binary, payload)
        write_json(description, old)
        return binary
