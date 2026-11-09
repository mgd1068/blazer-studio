"""Validate the shipped source snapshot, extraction boundaries, and UTF-8 labels."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extractor", ROOT / "tools/import_manufacturer_materials.py")
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


class CatalogTests(unittest.TestCase):
    def test_shipped_snapshot(self):
        catalog = json.loads((ROOT / "profiles/manufacturer-materials.json").read_text())
        self.assertEqual(catalog["applicability"], "unverified-model-mismatch")
        self.assertEqual([len(t["entries"]) for t in catalog["tables"]], [15, 11, 14, 11, 13])
        self.assertEqual(len(catalog["sourceSha256"]), 64)
        for table in catalog["tables"]:
            for entry in table["entries"]:
                raw = entry["sourceCells"]
                self.assertEqual(entry["material"], raw[0])
                self.assertEqual(entry["powerPercent"], float(raw[1][:-1]))
                self.assertTrue(raw[2].endswith("mm/min"))
                self.assertEqual(entry["speedMmMin"], float(raw[2][:-6]))
                if table["operation"] == "engrave":
                    self.assertIsNone(entry["passes"])
                    self.assertEqual(entry["intervalMm"], float(raw[3]))
                else:
                    self.assertIsNone(entry["intervalMm"])
                    self.assertEqual(entry["passes"], int(raw[3].split()[0]))
        self.assertEqual(catalog["tables"][0]["entries"][5]["material"], "Acryl (geschwärzt)")
        self.assertEqual(catalog["tables"][1]["entries"][3]["passes"], 1)

    def fixture(self):
        parts = ["<main>"]
        for (module, mode), count in zip(extractor.EXPECTED_SECTIONS, extractor.EXPECTED_COUNTS):
            name = "Gravur" if mode == "engrave" else "Schnitt"
            parts.append(f'<table aria-label="M3 Pro - {module}W {name}-Materialparameter">')
            for _ in range(count):
                fourth = "0.10" if mode == "engrave" else "2 Durchgang"
                parts.append(f"<tr><td>Ölholz</td><td>75%</td><td>250mm/min</td><td>{fourth}</td></tr>")
            parts.append("<tr><td></td><td></td><td></td><td></td></tr></table>")
        return ("".join(parts) + "</main>").encode()

    def test_complete_source(self):
        result = extractor.extract(self.fixture(), "2026-11-09")
        self.assertEqual(result["tables"][0]["entries"][0]["material"], "Ölholz")
        self.assertIsNone(result["tables"][0]["entries"][0]["passes"])

    def test_source_changes_fail_closed(self):
        for source in [b"<html/>", self.fixture().replace(b"mm/min", b"mm/s"),
                       self.fixture().replace(b"75%", b"175%"),
                       self.fixture().replace(b"2 Durchgang", b"0 Durchgang"),
                       self.fixture().replace(b"<td>75%</td>", b"<td></td>", 1),
                       self.fixture().replace(b"0.10", b"NaN")]:
            with self.subTest(source=source[:80]), self.assertRaises(ValueError):
                extractor.extract(source, "2026-11-09")

    def test_atomic_output(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "materials.json"
            extractor.write_atomic(target, {"name": "Ölholz"})
            self.assertEqual(json.loads(target.read_text()), {"name": "Ölholz"})
            self.assertEqual(list(Path(directory).iterdir()), [target])


if __name__ == "__main__":
    unittest.main()
