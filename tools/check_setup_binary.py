"""Verify the standalone x64 installer's pre-main DLL search policy in the PE file."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


def dependent_load_flags(data: bytes) -> int:
    """Read IMAGE_LOAD_CONFIG_DIRECTORY64 without executing the candidate."""
    try:
        if data[:2] != b"MZ":
            raise ValueError("not a PE file")
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe : pe + 4] != b"PE\0\0":
            raise ValueError("invalid PE signature")
        machine, sections = struct.unpack_from("<HH", data, pe + 4)
        optional_size = struct.unpack_from("<H", data, pe + 20)[0]
        optional = pe + 24
        if machine != 0x8664 or struct.unpack_from("<H", data, optional)[0] != 0x20B:
            raise ValueError("not an x64 PE32+ image")
        if optional_size < 112 + 11 * 8 or struct.unpack_from("<I", data, optional + 108)[0] < 11:
            raise ValueError("no PE load-config directory")
        rva, size = struct.unpack_from("<II", data, optional + 112 + 10 * 8)
        if not rva or size < 80:
            raise ValueError("no dependent-load flags")
        table = optional + optional_size
        for index in range(sections):
            header = table + index * 40
            virtual_size, start, raw_size, raw = struct.unpack_from("<IIII", data, header + 8)
            if start <= rva < start + max(virtual_size, raw_size):
                offset = rva - start
                if offset + 80 > raw_size or raw + offset + 80 > len(data):
                    raise ValueError("truncated PE load config")
                config = raw + offset
                if struct.unpack_from("<I", data, config)[0] < 80:
                    raise ValueError("short PE load config")
                return struct.unpack_from("<H", data, config + 78)[0]
        raise ValueError("unmapped PE load config")
    except struct.error as error:
        raise ValueError("truncated PE image") from error


def check(path: Path) -> None:
    flags = dependent_load_flags(path.read_bytes())
    if flags != 0x800:
        raise ValueError(f"{path.name}: DependentLoadFlags is {flags:#x}, expected 0x800 (System32 only)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("setup", type=Path)
    args = parser.parse_args()
    try:
        check(args.setup)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(f"{args.setup.name}: DependentLoadFlags = 0x800 (System32 only)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
