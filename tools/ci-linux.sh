#!/usr/bin/env bash
set -euo pipefail
source_dir="$(cd -- "${1:-$(dirname -- "$0")/..}" && pwd)"
build_dir="${2:-$source_dir/build-ci}"
if [[ "${BLAZER_INSTALL_DEPS:-0}" == 1 ]]; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install --no-install-recommends -y build-essential cmake ninja-build git pkg-config python3 python3-bs4 libglib2.0-bin shared-mime-info xdg-utils libfile-mimeinfo-perl \
        qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-serialport-dev libqrcodegencpp-dev \
        qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
        qml6-module-qtquick-dialogs qml6-module-qtquick-window qml6-module-qtquick-templates \
        qml6-module-qtqml-workerscript qml6-module-qtqml-models fonts-dejavu-core inkscape
fi
if [[ "${BLAZER_DEPS_ONLY:-0}" == 1 ]]; then exit 0; fi
export LANG=C.UTF-8 LC_ALL=C.UTF-8
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/blazer-ci-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
cmake -S "$source_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON -DBLAZER_SANITIZERS="${BLAZER_SANITIZERS:-OFF}"
cmake --build "$build_dir" --parallel "${BLAZER_BUILD_JOBS:-4}"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
ctest --test-dir "$build_dir" --output-on-failure
