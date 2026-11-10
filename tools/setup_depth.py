#!/usr/bin/env python3
"""Install optional local depth inference runtime and pinned Apache-2.0 model."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.request
import venv

ROOT = Path(os.environ.get('XDG_CACHE_HOME', str(Path.home() / '.cache'))) / 'blazer-studio/depth-runtime'
REVISION = '4472b7362082ad9968fee890ca0f1e5aca36b93d'
BASE = 'https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/' + REVISION
EXPECTED = {'preprocessor_config.json': '03576db3c13dd0471fdf5f5e1428befcb95de063fe699879150b293dc9e0a2c6', 'config.json': '3aee5b9bc4f711ee885c2526d871f0c8c6c8c4b26b8e04253d0167f6a83264f5', 'onnx/model_quantized.onnx': 'fcf51f1b230362b28690bb9d1809bf0431f29cad20534e3f589bd7285547f20d'}

if __name__ == '__main__':
    ROOT.mkdir(parents=True, exist_ok=True)
    if not (ROOT / 'bin/python').exists():
        venv.EnvBuilder(with_pip=True).create(ROOT)
    subprocess.run([str(ROOT / 'bin/python'), '-m', 'pip', 'install', 'onnxruntime==1.31.1', 'Pillow==12.3.0', 'numpy==2.5.4'], check=True)
    hashes = {}
    for name in ['preprocessor_config.json', 'config.json', 'onnx/model_quantized.onnx']:
        target = ROOT / Path(name).name
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != EXPECTED[name]:
            temporary = target.with_suffix(target.suffix + '.download')
            urllib.request.urlretrieve(BASE + '/' + name, temporary)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != EXPECTED[name]:
                temporary.unlink()
                raise ValueError("Model download checksum mismatch")
            temporary.replace(target)
        hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
    (ROOT / 'manifest.json').write_text(json.dumps({'revision': REVISION, 'sha256': hashes}, indent=2) + '\n')
    print('Installed local depth runtime:', ROOT)
