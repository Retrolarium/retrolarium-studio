"""An SDK upgrade never changes the selected base or hides build errors."""

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "release_sdk", Path(__file__).resolve().parents[1] / "tools/prepare_release_sdk.py"
)
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class ReleaseSdkTests(unittest.TestCase):
    def test_known_linker_versions_require_pe_directory_support(self):
        for version, expected in (
            ("GNU ld (GNU Binutils) 2.30", False),
            ("GNU ld (GNU Binutils) 2.44", False),
            ("GNU ld (GNU Binutils) 2.45.1", True),
            ("GNU ld (GNU Binutils) 2.46.1", True),
        ):
            self.assertEqual(expected, sdk.supports_load_config(version))
        with self.assertRaises(ValueError):
            sdk.supports_load_config("unrecognized linker")

    def test_capable_sdk_is_reused_by_immutable_id(self):
        image_id = "sha256:" + "a" * 64
        with (
            mock.patch.object(sdk, "output", side_effect=[image_id, "GNU ld (GNU Binutils) 2.45.1"]),
            mock.patch.object(sdk.subprocess, "run") as run,
        ):
            self.assertEqual(image_id, sdk.prepare("moving:tag"))
        run.assert_not_called()

    def test_old_linker_uses_a_separate_image_tied_to_the_resolved_base(self):
        base = "sha256:" + "a" * 64
        derived = "sha256:" + "b" * 64
        with (
            mock.patch.object(sdk, "output", side_effect=[base, "GNU ld (GNU Binutils) 2.30", derived]),
            mock.patch.object(sdk.subprocess, "run") as run,
        ):
            self.assertEqual(derived, sdk.prepare("qt:old"))
        command = run.call_args.args[0]
        self.assertIn(f"QT_SDK=retrolarium-studio-sdk-base:{base[7:]}", command)
        self.assertEqual(
            ["docker", "tag", base, "retrolarium-studio-sdk-base:" + base[7:]], run.call_args_list[0].args[0]
        )
        self.assertNotIn("qt:old", command)

    def test_failed_upgrade_never_falls_back_to_the_old_sdk(self):
        with (
            mock.patch.object(sdk, "output", side_effect=["sha256:" + "a" * 64, "GNU ld (GNU Binutils) 2.30"]),
            mock.patch.object(sdk.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "docker")),
        ):
            with self.assertRaises(subprocess.CalledProcessError):
                sdk.prepare("qt:old")

    def test_invalid_image_id_is_rejected_before_running_the_image(self):
        with mock.patch.object(sdk, "output", return_value="moving:tag") as output:
            with self.assertRaises(ValueError):
                sdk.prepare("qt:old")
        self.assertEqual(1, output.call_count)
