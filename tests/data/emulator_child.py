"""A ROM-free emulator fixture: acknowledge controls or deliberately linger."""

import os
from pathlib import Path
import sys
import time


def main():
    mode, ready = sys.argv[1:3]
    script = Path(sys.argv[sys.argv.index("-autoboot_script") + 1])
    folder = script.parent
    status = folder / "status.txt"
    ack = 0
    recording = mode in {"record", "stall"}
    flush_at = None
    flush_serial = 0
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if mode != "stall":
            try:
                commands = (folder / "actions.txt").read_text(encoding="utf-8").splitlines()
            except OSError:
                commands = []
            for command in commands:
                fields = command.split()
                if len(fields) < 2 or int(fields[0]) <= ack:
                    continue
                serial, verb = int(fields[0]), fields[1]
                if verb == "stopmovie":
                    if flush_at is None:
                        flush_at, flush_serial = time.monotonic() + 0.2, serial
                    if time.monotonic() < flush_at:
                        break
                    recording = False
                    ack = flush_serial
                elif verb == "exit":
                    return 0
                else:
                    ack = serial
        text = f"ack {ack}\nmode {'record' if recording else 'none'}\nframe 1\nlength 1\npaused 0\n"
        try:
            temporary = status.with_suffix(".partial")
            temporary.write_text(text, encoding="utf-8")
            temporary.replace(status)
            Path(ready).write_text(str(os.getpid()), encoding="ascii")
        except OSError:
            pass
        time.sleep(0.01)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
