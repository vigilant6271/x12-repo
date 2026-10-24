# AGENTS.md — working in this repository

## What this project actually is

X12-Repo is **not a web application**. It is the build infrastructure for the
Termux `x11-packages` repository rebuilt with the X12 Adaptive Performance
Engine. The runnable pieces are:

| Piece | How to run it | What it produces |
|---|---|---|
| C engine (`engine/`) | `make -C engine all` | `build/libx12engine.so` + `build/x12-info` diagnostics |
| APT repository (`repo/`) | served over HTTP (see below) | the package indexes clients download |
| Repo generator (`scripts/`) | `python3 scripts/build_repo.py` | `repo/packages.json`, `dists/x12/**`, `scripts/setup-x12-repo.sh` |
| Build driver (`ci/orchestrator.py`) | `python3 ci/orchestrator.py --dry-run` | `ci/build.log`, `ci/build_report.json` |

There are **no environment variables, no database and no external credentials**.
Nothing here needs secrets.

## Base44 dev environment

`docker compose -f docker-compose.base44.yml up -d` starts:

- **`apt-repo`** (web, host port 3000) — `nginx:alpine` serving `./repo`, i.e. the
  same tree the project publishes to GitHub Pages. `nginx.base44.conf` enables
  directory listings and serves extension-less files (`Release`, `Packages`) as
  `text/plain` so they render instead of downloading. This is what the preview
  shows, and what `apt` would download from.
- **`engine`** (worker, one-shot) — builds `libx12engine` + `x12-info` from
  `./engine` and prints the engine's hardware/mode report. The engine source is
  mounted read-only and built in `/tmp` inside the container, so the checkout
  stays clean. Because it is a job that finishes (exit 0), it sits behind the
  `tools` profile and is **not** started by `up` — run it on demand:
  ```bash
  docker compose -f docker-compose.base44.yml run --rm engine
  ```
  `docker compose ps` therefore only lists the (long-running) repository server.

Live reload: there is no dev server here, but the nginx bind mount means edits to
files under `repo/` are served immediately — no restart needed.

### Verifying it works

```bash
curl -s http://localhost:3000/dists/x12/Release | head        # APT Release metadata
curl -s http://localhost:3000/dists/x12/main/binary-aarch64/Packages | head
docker compose -f docker-compose.base44.yml run --rm engine   # engine diagnostics
```

The engine report must show an `[X12 Engine v1.0.0]` block with CPU/RAM/Mode
(`PERFORMANCE` on the sandbox, since the sandbox reports 0 thermal zones → cool).

## Non-obvious gotchas

- **`Release` must stay in sync with `Packages`.** `repo/dists/x12/Release` lists
  the MD5/SHA1/SHA256 + size of every `binary-*/Packages` file; `apt` rejects the
  repository ("Hash Sum mismatch") when they disagree. As imported they were
  **stale** (Release described 317964-byte indexes while the committed indexes
  were 307482/303670/306529), so the Release was regenerated. Re-check after any
  index change: the `Packages` sizes in the Release must equal the actual file sizes.
- **`python3 scripts/build_repo.py` rewrites `scripts/setup-x12-repo.sh`** from an
  embedded template inside `build_repo.py`, which **reverts the hand-fixed
  on-device bootstrap installer** (commits `048b920`, `4519a05`) back to the
  older apt-source version. To refresh only the APT metadata, call the generator
  functions instead of `main()`:
  ```bash
  python3 -c "import sys; sys.path.insert(0,'scripts'); import build_repo; build_repo.write_release({})"
  ```
  (regenerate the `Packages` indexes with `build_repo.write_packages_index(pkgs, arch)`
  only if `packages/_stats.json` changed).
- **`python3 scripts/generate_packages.py` rewrites all 952 `packages/*/build.sh`**
  files. Only run it when you intend to regenerate every package definition.
- `ci/orchestrator.py` writes `ci/build.log` and `ci/build_report.json` **into the
  repo**. With no `termux-create-package` / `termux-build-package` on the sandbox
  it falls back to `bash -n` syntax validation, so a full run validates the
  scripts without producing real `.deb` artifacts.
- The engine `Makefile` only cross-compiles when an Android toolchain
  (`*-linux-android-clang`) is on PATH; on the sandbox it builds a native Linux
  `.so`. Its `-Wextra` warnings (printf `%llu` on `uint64_t`, unused
  `x12_time_us`) are pre-existing, not caused by the sandbox.
- The Release advertises Termux architecture names (`aarch64`, `arm`, `x86_64`),
  not Debian ones (`amd64`, `arm64`), so a stock Debian `apt-get update` cannot
  consume this repository as-is. Integrity is checked by comparing the hashes
  above instead.
- `repo/` and `ci/build_report.json` are committed build artifacts — do not
  delete them to "clean up"; they are the published repository content.
