#!/usr/bin/env python3
"""Extract the vendor's five material tables without changing their values.

Usage: python3 tools/import_manufacturer_materials.py downloaded-page.html --retrieved-on YYYY-MM-DD
Requires beautifulsoup4. The source URL and retrieval date are recorded explicitly.
"""
from pathlib import Path
import hashlib
import json
import re
import argparse
from datetime import date
from bs4 import BeautifulSoup

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('html', type=Path)
parser.add_argument('--retrieved-on', required=True, type=date.fromisoformat)
args = parser.parse_args()
source = args.html.read_bytes()
soup = BeautifulSoup(source.decode('utf-8'), 'html.parser')
tables = []
for table in soup.find('main').find_all('table'):
    title = table.get('aria-label', '')
    module = int(re.search(r'(\d+)W', title).group(1))
    engraving = 'Gravur' in title
    entries = []
    for row in table.find_all('tr'):
        cells = [x.get_text(' ', strip=True) for x in row.find_all('td', recursive=False)]
        if not cells or not any(cells):
            continue
        if len(cells) != 4 or not all(cells):
            raise ValueError(f'Incomplete row: {title}: {cells}')
        power = float(cells[1].removesuffix('%'))
        speed = float(cells[2].removesuffix('mm/min'))
        entry = {'material': cells[0], 'powerPercent': power, 'speedMmMin': speed,
                 'intervalMm': float(cells[3]) if engraving else None,
                 'passes': None if engraving else int(re.match(r'\d+', cells[3]).group()),
                 'sourceCells': cells}
        assert 0 <= power <= 100 and speed > 0
        entries.append(entry)
    tables.append({'sourceTitle': title, 'model': 'M3 Pro', 'modulePowerW': module,
                   'operation': 'engrave' if engraving else 'cut',
                   'cuttingRecommended': False if module == 1 else None,
                   'sourceColumns': [x.get_text(' ', strip=True) for x in table.find_all('th')],
                   'entries': entries})
assert [len(t['entries']) for t in tables] == [15, 11, 14, 11, 13]
catalog = {'schemaVersion': 1, 'sourceUrl': 'https://de.blazexlaser.com/pages/maschineneinstellung',
           'retrievedOn': args.retrieved_on.isoformat(), 'sourceSha256': hashlib.sha256(source).hexdigest(),
           'manufacturerModel': 'M3 Pro', 'installedProfileModel': 'M3',
           'applicability': 'unverified-model-mismatch', 'hardwareTested': False,
           'note': 'Herstellerreferenz für M3 Pro. Zuordnung zum vorhandenen M3 nicht bestätigt. Fehlende Gravur-Durchgänge sind null, nicht als 1 ergänzt.',
           'sourceAnomalies': ['5-W-Schnitt: Sperrholz 10 mm mit 1 Durchgang, 8 mm mit 6 Durchgängen. Originalwerte unverändert; nicht am Gerät geprüft.'],
           'tables': tables}
output = Path(__file__).resolve().parents[1]/'profiles/manufacturer-materials.json'
output.write_text(json.dumps(catalog, indent=2, ensure_ascii=False)+'\n')
print(f'{len(tables)} Tabellen, {sum(len(t["entries"]) for t in tables)} Einträge: {output}')
