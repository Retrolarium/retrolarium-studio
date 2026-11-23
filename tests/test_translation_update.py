"""Translation refresh uses the same extraction rules with either SDK."""

import importlib.util
from pathlib import Path
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "translation_update", Path(__file__).resolve().parents[1] / "tools/update_translations.py"
)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class TranslationUpdateTests(unittest.TestCase):
    def test_local_sdk_paths_and_the_alias_are_passed_as_separate_arguments(self):
        prefix = Path("Qt SDK with spaces")
        command = tool.command(Path("checkout with spaces"), "ru", prefix)
        self.assertEqual(
            str(prefix / "bin" / ("lupdate.exe" if tool.sys.platform == "win32" else "lupdate")), command[0]
        )
        self.assertIn("translate+=rlTr", command)
        self.assertNotIn("-no-obsolete", command)
        self.assertNotIn("-pluralonly", command)
        self.assertEqual("resources/i18n/retrolarium_studio_ru.ts", command[-1])

    def test_english_only_extracts_plural_forms_with_both_toolchains(self):
        for prefix in (None, Path("Qt SDK")):
            self.assertIn("-pluralonly", tool.command(Path("checkout"), "en", prefix))

    def test_docker_calls_lupdate_directly_and_rejects_an_invalid_locale(self):
        command = tool.command(Path("checkout with spaces"), "pt_BR")
        self.assertEqual("/usr/local/Qt-5.15/bin/lupdate", command[command.index("--entrypoint") + 1])
        self.assertNotIn("sh", command)
        with self.assertRaises(ValueError):
            tool.command(Path("checkout"), "../bad")

    def test_posix_docker_writes_as_the_host_user_and_windows_needs_no_uid(self):
        root = Path("checkout")
        with (
            mock.patch.object(tool.os, "name", "posix"),
            mock.patch.object(tool.os, "getuid", return_value=1234, create=True),
            mock.patch.object(tool.os, "getgid", return_value=5678, create=True),
        ):
            command = tool.command(root, "ru")
            self.assertEqual("1234:5678", command[command.index("--user") + 1])
        with mock.patch.object(tool.os, "name", "nt"):
            self.assertNotIn("--user", tool.command(root, "ru"))
