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
apt-get install --no-install-recommends -y "$package_dir"/blazer-studio_*.deb
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
test -f /usr/share/blazer-studio/tools/photo_depth.py
useradd --create-home --shell /bin/bash beta
runuser -u beta -- mkdir -p /home/beta/.local/share/BlazerStudio '/home/beta/.config/BlazerStudio' /home/beta/runtime
chmod 700 /home/beta/runtime
runuser -u beta -- sh -c 'echo preserve > /home/beta/.local/share/BlazerStudio/package-test-sentinel'
runuser -u beta -- env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    XDG_RUNTIME_DIR=/home/beta/runtime blazer-studio --self-test
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
runuser -u beta -- env QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    XDG_RUNTIME_DIR=/home/beta/runtime blazer-studio --self-test
apt-get remove -y blazer-studio
test "$(cat /home/beta/.local/share/BlazerStudio/package-test-sentinel)" == preserve
echo 'PACKAGE RUNTIME TEST PASSED (install, non-root launch, codecs, project roundtrip, render, reinstall, removal).'
