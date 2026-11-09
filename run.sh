#!/usr/bin/env bash
set -euo pipefail
blazer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$blazer_dir/build/blazer-studio" ]]; then
    cmake -S "$blazer_dir" -B "$blazer_dir/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
    cmake --build "$blazer_dir/build" --parallel 4
fi
exec "$blazer_dir/build/blazer-studio" "$@"
