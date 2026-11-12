#!/usr/bin/env python3
"""Check local Markdown targets in a source checkout or shipped documentation."""
from pathlib import Path
import re
import sys
from urllib.parse import unquote, urlsplit

root = Path(sys.argv[1]).resolve()
pages = [root / name for name in ('README.md', 'START.md', 'TEST_PROTOCOL.md', 'KNOWN_LIMITATIONS.md')]
pages += sorted((root / 'docs').rglob('*.md'))
errors = []
checked = 0
for page in pages:
    if not page.exists():
        if page.name == 'README.md':
            errors.append(f'Missing entry point: {page}')
        continue
    # The project documentation uses inline Markdown links and images.
    for link in re.findall(r'\]\(([^)]+)\)', page.read_text()):
        url = urlsplit(link.strip('<>'))
        if url.scheme or url.netloc or not url.path:
            continue
        target = (page.parent / unquote(url.path)).resolve()
        checked += 1
        if not target.is_relative_to(root) or not target.exists():
            errors.append(f'{page.relative_to(root)}: broken local target {link}')
if errors:
    print('\n'.join(errors), file=sys.stderr)
    sys.exit(1)
print(f'Documentation: {checked} local targets resolve in {root}')
