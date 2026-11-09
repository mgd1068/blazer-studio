#!/usr/bin/env python3
"""Extract BlazeX M3 Pro tables without changing source values.

Usage: import_manufacturer_materials.py page.html --retrieved-on YYYY-MM-DD
Requires beautifulsoup4. No network access or updates are performed by this script.
"""
import argparse
from datetime import date
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
from bs4 import BeautifulSoup

EXPECTED_COUNTS = [15, 11, 14, 11, 13]
EXPECTED_SECTIONS = [(5, "engrave"), (5, "cut"), (10, "engrave"), (10, "cut"), (1, "engrave")]


def number_with_unit(text, unit):
    match = re.fullmatch(r"(\d+(?:\.\d+)?)" + re.escape(unit), text)
    if not match:
        raise ValueError(f"Unsupported value/unit: {text!r}; expected {unit!r}")
    return float(match[1])


def extract(source, retrieved_on):
    retrieved_on = date.fromisoformat(str(retrieved_on))
    soup = BeautifulSoup(source.decode("utf-8"), "html.parser")
    main = soup.find("main")
    if main is None:
        raise ValueError("No main content found")
    tables = []
    for table in main.find_all("table"):
        title = table.get("aria-label", "")
        heading = re.fullmatch(r"M3 Pro\s*-\s*(1|5|10)W\s+(Gravur|Schnitt)-Materialparameter", title)
        if not heading:
            raise ValueError(f"Unexpected table: {title!r}")
        module = int(heading[1])
        engraving = heading[2] == "Gravur"
        entries = []
        for row in table.find_all("tr"):
            cells = [x.get_text(" ", strip=True) for x in row.find_all("td", recursive=False)]
            if not cells or not any(cells):
                continue
            if len(cells) != 4 or not all(cells):
                raise ValueError(f"Incomplete row: {title}: {cells}")
            power = number_with_unit(cells[1], "%")
            speed = number_with_unit(cells[2], "mm/min")
            if engraving:
                interval = number_with_unit(cells[3], "")
                passes = None
                if interval <= 0:
                    raise ValueError("Invalid engraving interval")
            else:
                passes = re.fullmatch(r"([1-9]\d*) Durchgang", cells[3])
                if not passes:
                    raise ValueError("Invalid cutting pass count")
                passes, interval = int(passes[1]), None
            if not 0 <= power <= 100 or speed <= 0:
                raise ValueError("Power/speed out of range")
            entries.append({"material": cells[0], "powerPercent": power, "speedMmMin": speed,
                            "intervalMm": interval, "passes": passes, "sourceCells": cells})
        tables.append({"sourceTitle": title, "model": "M3 Pro", "modulePowerW": module,
                       "operation": "engrave" if engraving else "cut",
                       "cuttingRecommended": False if module == 1 else None,
                       "sourceColumns": [x.get_text(" ", strip=True) for x in table.find_all("th")],
                       "entries": entries})
    if ([len(t["entries"]) for t in tables] != EXPECTED_COUNTS or
            [(t["modulePowerW"], t["operation"]) for t in tables] != EXPECTED_SECTIONS):
        raise ValueError("Source table structure changed; inspect before updating")
    return {"schemaVersion": 1,
            "sourceUrl": "https://de.blazexlaser.com/pages/maschineneinstellung",
            "retrievedOn": retrieved_on.isoformat(), "sourceSha256": hashlib.sha256(source).hexdigest(),
            "manufacturerModel": "M3 Pro", "installedProfileModel": "M3",
            "applicability": "unverified-model-mismatch", "hardwareTested": False,
            "note": "Herstellerreferenz für M3 Pro. Zuordnung zum vorhandenen M3 nicht bestätigt. Fehlende Gravur-Durchgänge sind null, nicht als 1 ergänzt.",
            "sourceAnomalies": ["5-W-Schnitt: Sperrholz 10 mm mit 1 Durchgang, 8 mm mit 6 Durchgängen. Originalwerte unverändert; nicht am Gerät geprüft."],
            "tables": tables}


def write_atomic(path, catalog):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=path.parent,
                                         prefix=path.name + ".", delete=False) as stream:
            temporary = Path(stream.name)
            json.dump(catalog, stream, indent=2, ensure_ascii=False, allow_nan=False)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("html", type=Path)
    parser.add_argument("--retrieved-on", required=True, type=date.fromisoformat)
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parents[1] / "profiles/manufacturer-materials.json")
    args = parser.parse_args()
    catalog = extract(args.html.read_bytes(), args.retrieved_on)
    write_atomic(args.output, catalog)
    print(f"5 Tabellen, 64 Einträge: {args.output}")


if __name__ == "__main__":
    main()
