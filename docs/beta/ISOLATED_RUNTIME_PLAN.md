# Isolated beta.6 package acceptance preparation

This is a preparation plan, not evidence that a new package has passed. Do not
install a candidate on the host or connect a live application/device. The failed
physical acceptance of installed beta.5.2-local remains unchanged.

Read-only inventory on 21 November 2026 found Docker, an amd64 Ubuntu 26.04 image
`sha256:febc1a2eec1fdaad5db7e542ce1fefaee9656b6e3a177f0c703d616e77b86a62`,
and a cached Ubuntu builder
`sha256:a3a6c2d249f561c0fc08a6d37204d2306c24d7bb203d1cd0491ed39ec05366d7`
(`blazer09-builder:latest`). No Blazer container was running. Existing unrelated
containers must remain untouched. New runtime tests must start from the clean
Ubuntu image, never from the SDK builder or an old modified runtime container.

## Corrected runtime oracle

The current source emits no auxiliary M8/M9 commands without verified mapping.
`test-installed-package.sh` now expects the exact current manifest version and
revision, followed by the exact independent rectangle GCode without M8/M9.
The optional older installed package has a separate explicit version+revision
oracle, based on existing saved manifests and source history:

| Source version | Exact source revision | Historical rectangle output |
|---|---|---|
| 0.9.0-beta.5 | 26e1464dd0d1ec13081c76011fb3bbeebec6f240 | No M8/M9 |
| 0.9.0-beta.5.1 | f7ebbf9537023a03f01227a005ea54e7e2473437 | No M8/M9 |
| 0.9.0-beta.5.2-local | 5e66eb1c8bdea14f14218866c035d2e159be2469 | Exact old M8/M9 sequence |

An unknown version/revision pair fails. Auxiliary commands are not accepted as
optional variations. Historical M8/M9 proves only exported software behavior;
it did not establish fan operation or hardware acceptance. The superseded
package-make artifact with the same beta.5.2-local version is deliberately not
accepted as the final previous identity.

The script retains exact legacy-project/template export and user-file hash
checks, fresh install/optional upgrade, reinstall, removal, menu/MIME/licenses,
non-root launch and no-development-SDK assertions. It adds an explicit transient
offscreen `--screenshot` check alongside `--self-test`; both force hardware access
disabled. CLI fixture/self-test processes have 60-second limits, screenshot 30s.
The normal no-file desktop-entry smoke check is confined to the disposable
container with no hardware, host display, session bus, or user-home mounts.
The environment opt-in now additionally requires a container marker and Ubuntu
26.04 amd64 root before any package mutation.

## Reproducible later build

Run only after the final source is committed, tests pass, and the package build
is authorized. Use a **new** empty evidence directory. A Git worktree's `.git`
file points outside the worktree; mounting only that worktree at `/source` does
not make a valid container checkout. Create an isolated standalone local clone:

```bash
set -euo pipefail
root=/home/mathias/Projekte/Laser/blazer-studio-rebuild
revision=$(git -C "$root" rev-parse HEAD)
test -z "$(git -C "$root" status --porcelain --untracked-files=normal)"
evidence=/home/mathias/benslab/analysis/blazer-beta6-FINAL-REVISION
mkdir "$evidence"
git clone --local --no-hardlinks "$root" "$evidence/source"
git -C "$evidence/source" checkout --detach "$revision"
test "$(git -C "$evidence/source" rev-parse HEAD)" = "$revision"
mkdir "$evidence/package"
docker run --rm --user "$(id -u):$(id -g)" \
  --env HOME=/tmp/blazer-builder-home \
  --mount "type=bind,src=$evidence/source,dst=/source,readonly" \
  --mount "type=bind,src=$evidence/package,dst=/output" \
  sha256:a3a6c2d249f561c0fc08a6d37204d2306c24d7bb203d1cd0491ed39ec05366d7 \
  bash -c 'mkdir -p "$HOME"; git config --global --add safe.directory /source; bash /source/tools/package-linux.sh /output' \
  > "$evidence/build.log" 2>&1
```

`package-linux.sh` archives that exact Git commit and supplies SOURCE_REVISION,
then independently configures/builds Release, creates the DEB, manifest, complete
documentation/source archive and recursive SHA256SUMS. No host package command
is involved. Review manifest/revision/version/checksums before running containers.
The cached builder is a known local baseline, not proof that future SDK
requirements or live apt repositories remain identical; failed builds must not
reuse a stale DEB. Never package from a sanitizer build tree.

## Fresh install and previous-package upgrade

Create separate named containers so their test output and screenshot can be
copied after removal of the package, before destroying the containers. No source
workspace or evidence directory is mounted writable into the runtime. Network
is used only for package dependencies. No `--device`, `--privileged`, host DBus,
X11/Wayland socket, host `/dev`, Docker socket, or real user home is mounted.

```bash
runtime=sha256:febc1a2eec1fdaad5db7e542ce1fefaee9656b6e3a177f0c703d616e77b86a62
fresh=blazer-beta6-fresh-FINAL-REVISION
docker create --name "$fresh" --security-opt no-new-privileges=true --pids-limit 512 \
  --env BLAZER_DISPOSABLE_CONTAINER=1 \
  --mount "type=bind,src=$evidence/package,dst=/packages,readonly" \
  --mount "type=bind,src=$evidence/source/tools/test-installed-package.sh,dst=/test-package.sh,readonly" \
  "$runtime" bash /test-package.sh /packages
docker start -a "$fresh" > "$evidence/fresh-runtime.log" 2>&1
test "$(docker wait "$fresh")" = 0
docker cp "$fresh:/home/beta/runtime/package-render.png" "$evidence/fresh-runtime.png"
docker cp "$fresh:/tmp/blazer-self-test.json" "$evidence/fresh-self-test.json"
docker rm "$fresh"

previous=/home/mathias/benslab/analysis/blazer-beginner-outline-20261120/package-final
upgrade=blazer-beta6-upgrade-FINAL-REVISION
docker create --name "$upgrade" --security-opt no-new-privileges=true --pids-limit 512 \
  --env BLAZER_DISPOSABLE_CONTAINER=1 \
  --env BLAZER_PREVIOUS_PACKAGE=/previous/blazer-studio_0.9.0~beta.5.2~local_amd64.deb \
  --mount "type=bind,src=$previous,dst=/previous,readonly" \
  --mount "type=bind,src=$evidence/package,dst=/packages,readonly" \
  --mount "type=bind,src=$evidence/source/tools/test-installed-package.sh,dst=/test-package.sh,readonly" \
  "$runtime" bash /test-package.sh /packages
docker start -a "$upgrade" > "$evidence/upgrade-runtime.log" 2>&1
test "$(docker wait "$upgrade")" = 0
docker cp "$upgrade:/home/beta/runtime/package-render.png" "$evidence/upgrade-runtime.png"
docker cp "$upgrade:/tmp/blazer-self-test.json" "$evidence/upgrade-self-test.json"
docker rm "$upgrade"
```

A failed container should be retained only long enough to copy its bounded
fixture logs. Stop/remove only these exact named containers; never perform a
host-wide Docker cleanup. Visually inspect the resulting PNGs as a separate
step. Runtime screenshots are offscreen/software-rendered, not native desktop,
HiDPI, human usability, or hardware acceptance.

The prior 20 November clean-runtime log and final manifest belong to
beta.5.2-local, revision 5e66eb1. Their software pass cannot be transferred to
beta.6; VERIFIED.json separately records physical collision/fan acceptance failure.

## No-install validation of this script correction

```bash
bash -n tools/test-installed-package.sh
python3 tests/package_runtime_oracle_test.py
```

The Python tests execute only the pure extracted GCode-oracle function and the
script's earliest refusal guard. They do not invoke apt/dpkg installation,
launch Blazer Studio, create containers, or access physical endpoints. Actual
fresh/upgrade/reinstall/removal acceptance is still pending a newly frozen DEB.
