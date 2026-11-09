#!/usr/bin/env bash
set -euo pipefail
blazer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python3 - "$blazer_dir" <<'PY'
from pathlib import Path
import os, sys
root = Path(sys.argv[1])
apps = Path(os.environ.get('XDG_DATA_HOME', str(Path.home()/'.local/share'))) / 'applications'
apps.mkdir(parents=True, exist_ok=True)
launcher = apps / 'blazer-studio.desktop'
text = (root/'packaging/blazer-studio.desktop.in').read_text().replace('@PROJECT_DIR@', str(root))
if launcher.exists() and str(root) not in launcher.read_text():
    raise SystemExit('Vorhandener fremder Starter wird nicht überschrieben: ' + str(launcher))
launcher.write_text(text)
print(launcher)
PY
