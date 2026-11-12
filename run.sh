#!/usr/bin/env bash
set -euo pipefail
blazer_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -f "$blazer_dir/build/CMakeCache.txt" ]]; then
    cmake -S "$blazer_dir" -B "$blazer_dir/build" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
else
    cmake -S "$blazer_dir" -B "$blazer_dir/build"
fi
cmake --build "$blazer_dir/build" --target blazer-studio --parallel "${BLAZER_BUILD_JOBS:-4}"
exec "$blazer_dir/build/blazer-studio" "$@"
