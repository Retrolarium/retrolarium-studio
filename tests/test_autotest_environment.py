"""Interface scenarios select developer tools only for source builds."""

import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location(
    "autotest_run", Path(__file__).resolve().parents[1] / "tools/autotest/run.py"
)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class AutotestEnvironmentTests(unittest.TestCase):
    def test_source_scenarios_enable_developer_tool_discovery_explicitly(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.dict(os.environ, {"RETROLARIUM_DEV": "0"}):
            env = runner.environment(Path(folder), Path(folder) / "home", None)
        self.assertEqual("1", env["RETROLARIUM_DEV"])
        self.assertIn("RETROLARIUM_KIT", env)

    def test_packaged_scenarios_cannot_inherit_developer_mode(self):
        with tempfile.TemporaryDirectory() as folder, mock.patch.dict(os.environ, {"RETROLARIUM_DEV": "1"}):
            root = Path(folder)
            (root / "kit").mkdir()
            (root / "kit/rltool.py").write_text("")
            env = runner.environment(root, root / "home", None)
        self.assertNotIn("RETROLARIUM_DEV", env)
