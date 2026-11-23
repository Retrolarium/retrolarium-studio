"""Release builds must carry the policy, regardless of linker options."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "setup_binary", Path(__file__).resolve().parents[1] / "tools/check_setup_binary.py"
)
binary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binary)


def image(flags=0x800, config_size=160):
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    struct.pack_into("<H", data, 0x98, 0x20B)
    struct.pack_into("<I", data, 0x98 + 108, 16)
    struct.pack_into("<II", data, 0x98 + 112 + 80, 0x1000, config_size)
    struct.pack_into("<IIII", data, 0x98 + 240 + 8, config_size, 0x1000, 512, 512)
    struct.pack_into("<I", data, 512, config_size)
    struct.pack_into("<H", data, 512 + 78, flags)
    return data


class SetupBinaryTests(unittest.TestCase):
    def test_reads_the_directory_by_rva_instead_of_a_fixed_file_offset(self):
        self.assertEqual(0x800, binary.dependent_load_flags(image()))
        data = image()
        struct.pack_into("<IIII", data, 0x98 + 240 + 8, 160, 0x2000, 512, 512)
        struct.pack_into("<II", data, 0x98 + 112 + 80, 0x2000, 160)
        self.assertEqual(0x800, binary.dependent_load_flags(data))

    def test_rejects_missing_truncated_or_unmapped_load_config(self):
        for data in (b"", image()[:512], image()[:580]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                binary.dependent_load_flags(data)
        for rva, size in ((0, 0), (0x3000, 112), (0x1000, 32)):
            data = image()
            struct.pack_into("<II", data, 0x98 + 112 + 80, rva, size)
            with self.subTest(rva=rva, size=size), self.assertRaises(ValueError):
                binary.dependent_load_flags(data)

    def test_a_default_or_expanded_search_policy_fails_the_release_check(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "setup.exe"
            for flags in (0, 0xA00, 0x1000):
                path.write_bytes(image(flags))
                with self.subTest(flags=flags), self.assertRaisesRegex(ValueError, "expected 0x800"):
                    binary.check(path)
            path.write_bytes(image())
            binary.check(path)

    def test_legacy_prefixes_with_the_correct_flag_still_fail_verification(self):
        for size in (80, 112, 128, 144):
            with self.subTest(size=size), self.assertRaises(ValueError):
                binary.dependent_load_flags(image(config_size=size))
        data = image()
        struct.pack_into("<I", data, 512, 112)
        with self.assertRaises(ValueError):
            binary.dependent_load_flags(data)
