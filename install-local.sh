#!/usr/bin/env bash
set -euo pipefail
blazer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python3 - "$blazer_dir" <<'PY'
from pathlib import Path
import os, shutil, subprocess, sys
root = Path(sys.argv[1])
data = Path(os.environ.get('XDG_DATA_HOME', str(Path.home()/'.local/share')))
apps = data / 'applications'
apps.mkdir(parents=True, exist_ok=True)
launcher = apps / 'blazer-studio.desktop'
def desktop_value(value):
    return str(value).replace('\\', '\\\\').replace('\n', '\\n').replace('\r', '\\r').replace('\t', '\\t')
script = str(root / 'run.sh').replace('%', '%%')
for character in ('\\', '"', '`', '$'):
    script = script.replace(character, '\\' + character)
source = desktop_value(root)
text = (root/'packaging/blazer-studio.desktop.in').read_text().replace('@PROJECT_SCRIPT@', desktop_value(script)).replace('@PROJECT_ICON@', desktop_value(root/'assets/blazer-studio.svg')).replace('@PROJECT_SOURCE@', source)
if launcher.exists() and 'X-Blazer-Source=' + source not in launcher.read_text() and str(root) not in launcher.read_text():
    raise SystemExit('Vorhandener fremder Starter wird nicht überschrieben: ' + str(launcher))
mime = data / 'mime/packages/blazer-studio.xml'
if mime.exists() and 'application/x-blazer-studio' not in mime.read_text():
    raise SystemExit('Vorhandene fremde Dateitypdefinition wird nicht überschrieben: ' + str(mime))
mime.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(root/'packaging/blazer-studio.xml', mime)
launcher.write_text(text)
if shutil.which('update-mime-database'):
    subprocess.run(['update-mime-database', str(data/'mime')], check=True)
else:
    print('Dateitypdefinition gespeichert; shared-mime-info wird für die Dateizuordnung benötigt.')
print(launcher)
PY
