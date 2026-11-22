"""The child program of process_runner_test.

process_child.py exit <code>         prints a JSON answer, exits with <code>
process_child.py sleep               sleeps a minute
process_child.py grandchild <file>   starts a Python that sleeps a minute,
                                     writes its process id to <file>, sleeps
"""

import json
from pathlib import Path
import subprocess
import sys
import time


def main() -> int:
    mode = sys.argv[1]
    if mode == "exit":
        print(json.dumps({"status": "ok"}))
        print("a line on stderr", file=sys.stderr)
        return int(sys.argv[2])
    if mode == "grandchild":
        child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        target = Path(sys.argv[2])
        partial = target.with_suffix(".partial")
        partial.write_text(str(child.pid), encoding="ascii")
        partial.replace(target)
    print("sleeping", file=sys.stderr, flush=True)
    time.sleep(60)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
