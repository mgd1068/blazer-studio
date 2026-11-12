#!/usr/bin/env bash
# Use only in a disposable, clean Ubuntu 26.04 container as root.
set -euo pipefail
package_dir="$(cd -- "$1" && pwd)"
[[ "${BLAZER_DISPOSABLE_CONTAINER:-}" == 1 ]] || {
    echo 'This test installs/removes packages; use BLAZER_DISPOSABLE_CONTAINER=1 in a disposable container.' >&2
    exit 2
}
(cd "$package_dir" && sha256sum -c SHA256SUMS)
# Ubuntu's container image excludes documentation by default. Preserve this package's notices.
printf '%s\n' 'path-include=/usr/share/doc/blazer-studio/*' >> /etc/dpkg/dpkg.cfg.d/excludes
export DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 LC_ALL=C.UTF-8
apt-get update
useradd --create-home --shell /bin/bash beta
runuser -u beta -- mkdir -p /home/beta/.local/share/BlazerStudio '/home/beta/.config/BlazerStudio' /home/beta/runtime
chmod 700 /home/beta/runtime
runuser -u beta -- sh -c 'echo preserve > /home/beta/.local/share/BlazerStudio/package-test-sentinel'
# Own rectangle fixture: persisted inputs and an independent expected controller program.
# All CLI exports below force HardwareAccessDisabled; no device is mounted here.
user_data() {
    runuser -u beta -- env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        XDG_RUNTIME_DIR=/home/beta/runtime python3 - "$1" <<'FIXTURE'
import hashlib, json, pathlib, subprocess, sys
home = pathlib.Path('/home/beta')
docs = home / 'Documents'
settings = home / '.config/BlazerStudio/Blazer Studio.conf'
record = home / 'runtime-fixture.json'
mode = sys.argv[1]
if mode == 'prepare':
    docs.mkdir(exist_ok=True)
    obj = dict(id='c0d02751-703f-4a20-a6ec-8b76007d6c0a', name='Rectangle', kind='rect', text='',
               x=65, y=55, width=37, height=30, layer=1, visible=True, page=0,
               path=[], pathFillRule=0, style={}, settings={}, relief={},
               extraOperations=[], groupId='', groupName='', depthMap='', svg='')
    project = dict(format='blazer-studio', version=7, units='mm', origin='bottom-left',
                   title='Package upgrade fixture', pages=['Sheet 1'], activePage=0,
                   selected=0, selectedRows=[0], objects=[obj], material={}, layers=[
                       dict(name='Engrave', mode='Gravur', speed=3000, power=20,
                            passes=1, enabled=True, interval=0.1),
                       dict(name='Cut', mode='Schnitt', speed=600, power=80,
                            passes=1, enabled=True, interval=0.1)])
    (docs / 'legacy.blazer').write_text(json.dumps(project))
    project.update(format='blazer-studio-template', templateVersion=1, version=8,
                   notes='Own reusable upgrade fixture')
    (docs / 'reusable.blazertemplate').write_text(json.dumps(project))
    settings.write_text('[onboarding]\nv1\\complete=true\n[projects]\nv1\\recent=file:///home/beta/Documents/legacy.blazer\n')
    inputs = [docs / 'legacy.blazer', docs / 'reusable.blazertemplate', settings]
    record.write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                                  for p in inputs}))
else:
    for name, digest in json.loads(record.read_text()).items():
        assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest() == digest, name
    if mode != 'preserved':
        expected = ('M5\n(Blazer Studio: GRBL laser mode, $30=1000, $32=1, G54)\n'
                    'G21\nG90\nG54\nG17\nG94\nS0\nG0 X65.000 Y55.000\nM4 S800\n'
                    'G1 X102.000 Y55.000 F600.000\nG1 X102.000 Y85.000\n'
                    'G1 X65.000 Y85.000\nG1 X65.000 Y55.000\nM5 S0\nG0\n')
        sources = [('--open-project', 'legacy.blazer')]
        if mode == 'current':
            sources.append(('--new-from-template', 'reusable.blazertemplate'))
        for option, name in sources:
            target = home / 'runtime' / (name + '.gcode')
            target.unlink(missing_ok=True)
            subprocess.run(['blazer-studio', option, str(docs / name),
                            '--export-gcode', str(target)], check=True)
            assert target.read_bytes() == expected.encode(), (name, target.read_text())
        for name, digest in json.loads(record.read_text()).items():
            assert hashlib.sha256(pathlib.Path(name).read_bytes()).hexdigest() == digest, name
print('USER DATA CHECK PASSED:', mode)
FIXTURE
}
fixture_prepared=0
if [[ -n "${BLAZER_PREVIOUS_PACKAGE:-}" ]]; then
    apt-get install --no-install-recommends -y "$BLAZER_PREVIOUS_PACKAGE"
    runuser -u beta -- env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        XDG_RUNTIME_DIR=/home/beta/runtime blazer-studio --self-test
    user_data prepare
    user_data previous
    fixture_prepared=1
fi
apt-get install --no-install-recommends -y "$package_dir"/blazer-studio_*.deb
[[ "$fixture_prepared" == 1 ]] || user_data prepare
user_data current
test "$(cat /home/beta/.local/share/BlazerStudio/package-test-sentinel)" == preserve
# No compiler, CMake or development Qt packages may have entered the runtime image.
if command -v c++ || command -v cmake; then
    echo 'Unexpected compiler or CMake in runtime image.' >&2
    exit 1
fi
[[ "$(dpkg-query -W -f='${db:Status-Status}' qt6-base-dev 2>/dev/null || true)" != installed ]]
test -f /usr/share/applications/blazer-studio.desktop
test -f /usr/share/mime/packages/blazer-studio.xml
test -f /usr/share/icons/hicolor/scalable/apps/blazer-studio.svg
test -f /usr/share/doc/blazer-studio/LICENSE
test -f /usr/share/doc/blazer-studio/LICENSES/LGPL-3.0.txt
python3 /usr/share/blazer-studio/tools/check-doc-links.py /usr/share/doc/blazer-studio
test -f /usr/share/blazer-studio/tools/photo_depth.py
self_test() {
    runuser -u beta -- env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
        XDG_RUNTIME_DIR=/home/beta/runtime blazer-studio --self-test > /tmp/blazer-self-test.json
    python3 - "$package_dir/BUILD-MANIFEST.json" /tmp/blazer-self-test.json <<'PY'
import json, subprocess, sys
manifest, actual = [json.load(open(path)) for path in sys.argv[1:]]
assert actual['selfTest'] == 'passed' and actual['simulation'] is True
assert actual['hardwareTested'] is False
assert actual['templateMimeType'] == 'application/x-blazer-template', actual
assert actual['version'] == manifest['version'], (actual, manifest)
assert actual['revision'] == manifest['revision'], (actual, manifest)
installed = subprocess.check_output(['dpkg-query', '-W', '-f=${Version}', 'blazer-studio'], text=True)
assert installed == manifest['version'].replace('-', '~'), installed
assert subprocess.check_output(['blazer-studio', '--version'], text=True).strip() == 'Blazer Studio ' + manifest['version']
print(json.dumps(actual))
PY
}
self_test
# Exercise the desktop entry's no-file command too (a missing %f must not make it exit).
[[ "$(sed -n 's/^Exec=//p' /usr/share/applications/blazer-studio.desktop)" == 'blazer-studio %f' ]]
set +e
runuser -u beta -- env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    XDG_RUNTIME_DIR=/home/beta/runtime timeout 3s blazer-studio
menu_status=$?
set -e
[[ "$menu_status" == 124 ]]
# Reinstall path preserves user files.
apt-get install --reinstall --no-install-recommends -y "$package_dir"/blazer-studio_*.deb
self_test
user_data current
apt-get remove -y blazer-studio
test "$(cat /home/beta/.local/share/BlazerStudio/package-test-sentinel)" == preserve
user_data preserved
echo 'PACKAGE RUNTIME TEST PASSED (install, non-root launch, codecs, project roundtrip, render, reinstall, removal).'
