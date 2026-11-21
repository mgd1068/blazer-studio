#!/usr/bin/env python3
"""Hand-authored fixture and adversarial modal cases; no product planner/encoder."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("outline_oracle", Path(__file__).parents[1] / "tools/check-outline-incident.py")
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

# Fixed independent dimensions/coordinates/parameters, not generated from JobPlan.
VALID = b"""M5
(GRBL fixture; metadata $30=1000)
G21
G90
G54
G17
G94
S0
G0 X0 Y0
M4 S1000
G1 X200 Y0 F5000
G1 X200 Y150
G1 X0 Y150
G1 X0 Y0
M5 S0
G0
"""
PROJECT = {
    "format": "blazer-studio", "version": 12, "units": "mm", "origin": "bottom-left", "activePage": 0,
    "objects": [{"kind": "rect", "visible": True, "page": 0, "x": 0, "y": 0, "width": 200, "height": 150,
                 "layer": 0, "toolLayer": 0, "engraveOutline": True, "extraOperations": [], "settings": {},
                 "path": [], "svg": "", "depthMap": ""}],
    "layers": [{"mode": "Gravur", "enabled": True, "power": 100, "speed": 5000, "passes": 1},
               {"mode": "Schnitt", "enabled": False}],
    "material": {"engrave": {"enabled": True, "power": 100, "speed": 5000, "passes": 1}, "cut": {"enabled": False}},
    "deviceProfile": {"workspace": {"width": 200, "height": 150, "origin": "bottom-left", "units": "mm"},
                      "driver": {"id": "grbl-serial", "settings": {"powerScale": 1000}}},
}


class OutlineOracleTest(unittest.TestCase):
    def reject_code(self, data):
        with self.assertRaises(oracle.Rejected):
            oracle.check_gcode(data)

    def test_independent_expected_geometry_and_modal_feed(self):
        report = oracle.check_gcode(VALID)
        self.assertEqual(report["poweredEdges"], 4)
        self.assertEqual(report["poweredDistanceMm"], 700)
        self.assertEqual(report["xyRequests"], 5)
        # Omitted X/Y, modal feed, and reversed word ordering are interpreted.
        alternative = VALID.replace(b"G1 X200 Y150", b"Y150").replace(b"G1 X0 Y150", b"X0")
        alternative = alternative.replace(b"M4 S1000\nG1 X200 Y0 F5000", b"G1 X200 Y0 F5000 S1000 M4")
        self.assertEqual(oracle.check_gcode(alternative)["poweredDistanceMm"], 700)

    def test_approach_must_be_explicit_absolute_known_output_off(self):
        cases = [VALID.replace(b"G0 X0 Y0\n", b""),
                 VALID.replace(b"G0 X0 Y0", b"G1 X0 Y0"),
                 VALID.replace(b"G0 X0 Y0", b"G0 X1 Y0"),
                 VALID.replace(b"G0 X0 Y0", b"G0 X0"),
                 VALID.replace(b"G0 X0 Y0\nM4 S1000", b"M4 S1000\nG0 X0 Y0"),
                 VALID.replace(b"M5\n", b"", 1),
                 VALID.replace(b"S0\nG0", b"S1000\nG0", 1)]
        for code in cases:
            with self.subTest(code=code): self.reject_code(code)

    def test_coordinates_offsets_and_unsupported_axes_rejected(self):
        for old, new in [(b"G90", b"G91"), (b"G21", b"G20"), (b"G54", b"G55"),
                         (b"G54", b"G54\nG92 X0 Y0"), (b"G54", b"G54\nG10 L20 P1 X0 Y0"),
                         (b"G1 X200 Y0", b"G1 X200.01 Y0"),
                         (b"G1 X200 Y0", b"G1 X200 Y0 Z0")]:
            with self.subTest(new=new): self.reject_code(VALID.replace(old, new))

    def test_equal_total_length_wrong_edges_does_not_pass(self):
        # 200+200+150+150=700, same envelope, but two repeated sides, not a rectangle.
        bad = VALID.replace(b"G1 X200 Y150", b"G1 X0 Y0")
        self.reject_code(bad)

    def test_raster_repeat_connector_return_and_zero_length_rejected(self):
        cases = [VALID.replace(b"G1 X200 Y150", b"G1 X200 Y1\nG1 X200 Y150"),
                 VALID.replace(b"M5 S0", b"G1 X200 Y0\nM5 S0"),
                 VALID + b"G0 X0 Y0\n",
                 VALID.replace(b"G1 X200 Y150", b"G0 X200 Y150"),
                 VALID.replace(b"G1 X200 Y150", b"G1 X200 Y0\nG1 X200 Y150")]
        for code in cases:
            with self.subTest(code=code): self.reject_code(code)

    def test_power_feed_output_shutdown_and_accessories_rejected(self):
        for old, new in [(b"S1000", b"S500"), (b"F5000", b"F600"),
                         (b"M4 S1000", b"M3 S1000"),
                         (b"M5 S0", b"M5 S0\nM3 S1000\nM5 S0"),
                         (b"M5 S0", b"M5 S0\nM4 S1000\nM5 S0"),
                         (b"M5 S0", b"M5"), (b"M5 S0", b"S0"),
                         (b"M4 S1000", b"M4 S1000\nM8"),
                         (b"M5 S0", b"M5 S0\nM9"), (b"M4 S1000", b"M7\nM4 S1000")]:
            with self.subTest(new=new): self.reject_code(VALID.replace(old, new))
        self.assertEqual(oracle.check_gcode(VALID + b"(M8 M9 are comments only)\n")["poweredEdges"], 4)

    def test_unknown_syntax_conflicting_modes_and_resource_budgets(self):
        for suffix in (b"G0 X0 X1\n", b"G0 G1\n", b"%\n", b"(unclosed\n", b"((nested))\n",
                       b"M2\nG0\n", b"a" * 65537, b"\xff\n", b"\n" * 129):
            with self.subTest(suffix=suffix[:30]): self.reject_code(VALID + suffix)

    def test_saved_project_intent_is_checked_independently(self):
        source = json.dumps(PROJECT).encode()
        self.assertEqual(oracle.inspect_project(source)["savedPowerPercent"], 100)
        alterations = [lambda p: p["objects"][0].update(engraveOutline=False),
                       lambda p: p["objects"][0].update(width=201),
                       lambda p: p["objects"][0].update(extraOperations=[1]),
                       lambda p: p["objects"][0].update(toolLayer=1),
                       lambda p: p["layers"][0].update(passes=2),
                       lambda p: p["layers"][0].update(passes=True),
                       lambda p: p["material"]["engrave"].update(power=50),
                       lambda p: p.update(placement={"mode": "current"}),
                       lambda p: p["layers"][1].update(enabled=True)]
        for alter in alterations:
            project = copy.deepcopy(PROJECT)
            alter(project)
            with self.subTest(project=project), self.assertRaises(oracle.Rejected):
                oracle.inspect_project(json.dumps(project).encode())

    def test_offline_cli_isolated_input_environment_and_binary_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "fake-app"
            binary.write_text("#!/usr/bin/python3\nimport os,pathlib,sys\n"
                              "root=pathlib.Path.cwd()\n"
                              "assert sys.argv[1]=='--open-project' and sys.argv[3]=='--export-gcode'\n"
                              "assert os.environ['QT_QPA_PLATFORM']=='offscreen'\n"
                              "assert not any(k in os.environ for k in ('DISPLAY','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS'))\n"
                              "assert all(pathlib.Path(os.environ[k]).parent==root for k in ('XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','XDG_RUNTIME_DIR'))\n"
                              f"assert os.environ.get('HOME')=={os.environ.get('HOME')!r}\n"
                              f"assert os.environ.get('CODEX_HOME')=={os.environ.get('CODEX_HOME')!r}\n"
                              "assert pathlib.Path(sys.argv[2]).parent==root\n"
                              f"pathlib.Path(sys.argv[4]).write_bytes({VALID!r})\n")
            sha = oracle.digest(binary.read_bytes())
            source = json.dumps(PROJECT).encode()
            self.assertEqual(oracle.export_offline(binary, sha, source), VALID)
            with self.assertRaises(oracle.Rejected):
                oracle.export_offline(binary, "0" * 64, source)
            binary.write_text("#!/usr/bin/python3\nimport pathlib,sys\npathlib.Path(sys.argv[2]).write_text('modified')\n"
                              f"pathlib.Path(sys.argv[4]).write_bytes({VALID!r})\n")
            with self.assertRaisesRegex(oracle.Rejected, "temporary_input_changed"):
                oracle.export_offline(binary, oracle.digest(binary.read_bytes()), source)


if __name__ == "__main__":
    unittest.main()
