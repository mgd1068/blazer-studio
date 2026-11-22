"""Pure oracle and early-guard tests; never install a package or launch the app."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "tools/test-installed-package.sh"
FIXTURE = SCRIPT.read_text().split("<<'FIXTURE'\n", 1)[1].split("\nFIXTURE", 1)[0]
FUNCTION = next(node for node in ast.parse(FIXTURE).body
                if isinstance(node, ast.FunctionDef) and node.name == "runtime_expected_program")
NAMESPACE = {}
exec(compile(ast.Module(body=[FUNCTION], type_ignores=[]), str(SCRIPT), "exec"), NAMESPACE)
ORACLE = NAMESPACE["runtime_expected_program"]


class PackageRuntimeOracleTest(unittest.TestCase):
    def identity(self, version="0.9.0-beta.6-dev", revision="f" * 40):
        return dict(version=version, revision=revision, selfTest="passed",
                    simulation=True, hardwareTested=False)

    def test_current_exact_manifest_never_allows_auxiliary_commands(self):
        identity = self.identity()
        actual = ORACLE("current", identity, identity)
        self.assertNotIn(b"\nM8\n", actual)
        self.assertNotIn(b"\nM9\n", actual)
        self.assertIn(b"S0\nG0 X65.000 Y55.000\nM4 S800\n", actual)
        self.assertTrue(actual.endswith(b"M5 S0\nG0\n"))
        self.assertEqual(actual.count(b"\nG1 "), 4)
        for field in ("version", "revision"):
            mismatch = dict(identity, **{field: "different"})
            with self.assertRaises(AssertionError):
                ORACLE("current", identity, mismatch)
        with self.assertRaises(AssertionError):
            ORACLE("current", dict(identity, hardwareTested=True), identity)

    def test_historical_identities_have_distinct_fixed_programs(self):
        cases = [
            ("0.9.0-beta.5", "26e1464dd0d1ec13081c76011fb3bbeebec6f240", False),
            ("0.9.0-beta.5.1", "f7ebbf9537023a03f01227a005ea54e7e2473437", False),
            ("0.9.0-beta.5.2-local", "5e66eb1c8bdea14f14218866c035d2e159be2469", True),
            ("0.9.0-beta.7-dev", "9a6abbc72af8ac7ebbf6523c53e0f91b4176a90b", False),
            ("0.9.0-beta.8-dev", "de274e8ae9c8ec291668e499a1099fe7854871d2", False),
        ]
        baseline = ORACLE("current", self.identity(), self.identity())
        for version, revision, auxiliary in cases:
            with self.subTest(version=version):
                actual = ORACLE("previous", self.identity(version, revision), {})
                expected = baseline
                if auxiliary:
                    expected = expected.replace(b"S0\nG0 X65", b"S0\nM8\nG0 X65")
                    expected = expected.replace(b"M5 S0\nG0\n", b"M5 S0\nM9\nG0\n")
                self.assertEqual(actual, expected)
                with self.assertRaises(AssertionError):
                    ORACLE("previous", self.identity(version, "0" * 40), {})
        with self.assertRaises(AssertionError):
            ORACLE("previous", self.identity("unknown", cases[-1][1]), {})
        with self.assertRaises(AssertionError):
            ORACLE("typo", self.identity(), {})

    def test_default_guard_rejects_before_checksum_or_package_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            environment = dict(os.environ, BLAZER_DISPOSABLE_CONTAINER="")
            result = subprocess.run(["bash", str(SCRIPT), directory], env=environment,
                                    capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("disposable container", result.stderr)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_embedded_fixture_is_syntactically_valid_without_executing_it(self):
        ast.parse(FIXTURE)
        subprocess.run(["bash", "-n", str(SCRIPT)], check=True)


if __name__ == "__main__":
    unittest.main()
