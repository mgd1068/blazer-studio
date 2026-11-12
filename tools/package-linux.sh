#!/usr/bin/env bash
# Build a source-matched Debian candidate. Run inside Ubuntu 26.04 amd64.
set -euo pipefail
export LANG=C.UTF-8 LC_ALL=C.UTF-8
source_dir="$(cd -- "$(dirname -- "$0")/.." && pwd)"
output_dir="${1:-$source_dir/dist}"
mkdir -p "$output_dir"
output_dir="$(cd -- "$output_dir" && pwd)"
python3 - "$output_dir" <<'PY'
import pathlib, sys
if any(pathlib.Path(sys.argv[1]).iterdir()):
    sys.exit('Use an empty output directory for a new candidate')
PY
. /etc/os-release
[[ "$ID" == ubuntu && "$VERSION_ID" == 26.04 && "$(dpkg --print-architecture)" == amd64 ]] || {
    echo 'Package platform must be Ubuntu 26.04 amd64.' >&2; exit 2;
}
[[ -z "$(git -C "$source_dir" status --porcelain --untracked-files=normal)" ]] || {
    echo 'Commit changes before packaging: binaries and source must match an exact revision.' >&2; exit 2;
}
revision="$(git -C "$source_dir" rev-parse HEAD)"
version="$(cmake -DBLAZER_PRINT_VERSION=ON -P "$source_dir/cmake/Version.cmake")"
work_dir="$(mktemp -d /tmp/blazer-package.XXXXXXXX)"
trap 'rm -rf -- "$work_dir"' EXIT
archive_root="blazer-studio-$version"
mkdir "$work_dir/$archive_root"
git -C "$source_dir" archive HEAD | tar -x -C "$work_dir/$archive_root"
printf '%s\n' "$revision" > "$work_dir/$archive_root/SOURCE_REVISION"
# Reproducible source archive; SOURCE_REVISION supplies provenance outside Git.
SOURCE_DATE_EPOCH="$(git -C "$source_dir" show -s --format=%ct HEAD)"
export SOURCE_DATE_EPOCH
tar --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 --numeric-owner \
    -czf "$output_dir/$archive_root-source.tar.gz" -C "$work_dir" "$archive_root"
cmake -S "$work_dir/$archive_root" -B "$work_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$work_dir/build" --parallel "${BLAZER_BUILD_JOBS:-2}"
cpack --config "$work_dir/build/CPackConfig.cmake" -G DEB -B "$output_dir"
python3 - "$output_dir" "$revision" "$version" <<'PY'
import glob, json, os, platform, subprocess, sys
out, revision, version = sys.argv[1:]
debs = glob.glob(os.path.join(out, 'blazer-studio_*.deb'))
assert len(debs) == 1, 'Use an empty output directory for a new candidate'
manifest = dict(application='Blazer Studio', version=version, revision=revision,
                platform='Ubuntu 26.04 amd64', qt=subprocess.check_output(
                    ['pkg-config', '--modversion', 'Qt6Core'], text=True).strip(),
                compiler=subprocess.check_output(['c++', '-dumpfullversion'], text=True).strip(),
                dependencies=subprocess.check_output(['dpkg-deb', '-f', debs[0], 'Depends'], text=True).strip(),
                bundledQt=False, hardwareAcceptance='pending', externalUsabilityTest='pending')
with open(os.path.join(out, 'BUILD-MANIFEST.json'), 'w') as f:
    json.dump(manifest, f, indent=2); f.write('\n')
PY
cmake --install "$work_dir/build" --component Documentation --prefix "$work_dir/documentation-root"
cp -a "$work_dir/documentation-root/share/doc/blazer-studio/." "$output_dir/"
python3 "$work_dir/$archive_root/tools/check-doc-links.py" "$output_dir"
rm -rf -- "$output_dir/_CPack_Packages"
python3 - "$output_dir" <<'PY'
import hashlib, pathlib, sys
out = pathlib.Path(sys.argv[1])
files = sorted(p for p in out.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
with (out / 'SHA256SUMS').open('w') as f:
    for path in files:
        with path.open('rb') as source:
            digest = hashlib.file_digest(source, 'sha256').hexdigest()
        f.write(f'{digest}  {path.relative_to(out)}\n')
PY
echo "Candidate ready in $output_dir (hardware and external usability acceptance pending)."
