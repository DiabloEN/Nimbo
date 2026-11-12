import contextlib
import io
from pathlib import Path
import tempfile
import unittest

import nimbo_cli


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.first = {"friends": {"directory": "/srv/friends"}}
        self.both = {**self.first, "games": {"directory": "/opt/nimbo-9443"}}

    def test_one_instance_uses_registered_directory(self):
        for command in ("help", "status", "check", "owner-code"):
            self.assertEqual(
                nimbo_cli.resolve_command([command], self.first), [str(Path("/srv/friends/nimbo")), command]
            )

    def test_multiple_instances_require_selection(self):
        with self.assertRaisesRegex(ValueError, "friends, games"):
            nimbo_cli.resolve_command(["owner-code"], self.both)
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertIsNone(nimbo_cli.resolve_command(["help"], self.both))
        self.assertIn("friends, games", output.getvalue())
        self.assertEqual(
            nimbo_cli.resolve_command(["--instance", "games", "status"], self.both),
            [str(Path("/opt/nimbo-9443/nimbo")), "status"],
        )
        with self.assertRaises(ValueError):
            nimbo_cli.resolve_command(["--instance", "missing", "status"], self.both)

    def test_install_is_repeatable_and_preserves_an_existing_command(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "nimbo"
            self.assertTrue(nimbo_cli.install_launcher(target))
            self.assertTrue(nimbo_cli.install_launcher(target))
            target.write_bytes(b"existing command")
            self.assertFalse(nimbo_cli.install_launcher(target))
            self.assertEqual(target.read_bytes(), b"existing command")
