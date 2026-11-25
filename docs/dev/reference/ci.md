# CI

CI is moving from Forgejo Actions (`.forgejo/workflows/`) to CircleCI (`.circleci/`). Until the cutover both run:
CircleCI runs the checks, and Forgejo Actions still publishes the releases and deploys the website. Neither needs the
game: every check is game-free.

**Run CI's checks yourself:** `mise run ci` runs what CI checks on every change: the pinned test tools (`test-deps`),
every game-free check (`tests/run_all.py --strict`, the same line CI runs in parts) and the website's build and checks
(`scripts/website_check.sh`). `mise run test` runs the checks alone, and takes `run_all.py`'s arguments.

## CircleCI

CircleCI uses the team's orb, `magicbeandev/ci` (alias `mb`). `.circleci/config.yml` has one `mb/setup` job per event;
it compares the changed files with `.circleci/mapping.conf` and continues with that event's workflow:

| Event | Workflow | What runs |
|---|---|---|
| Pull request (same repository) | `events/pr.yml` | The checks the changed paths call for, a website preview, then `gate` |
| Push to `main` | `events/main.yml` | Every check, then the nightly release (dry run until the cutover) and the website deploy (dry run) |
| Tag `v*` | `events/release.yml` | Every check, the Windows ones included, before anything is built, signed or published (dry run) |
| Schedule `nightly` | `events/nightly.yml` | `third-party-upstream`: the `third_party/` parts against their upstream archives |
| Schedule `website_refresh` | `events/scheduled.yml` | The website deploy (dry run) |
| API run | `events/manual.yml` | A release dry run, or the Windows Vulkan back end linked with DLSS |
| Fork pull request | `events/fork.yml` | The pull request checks, with no secrets (below) |

Every workflow ends at `mb/gate`, which posts the `ci/circleci: gate` status on the commit.

The checks, in `.circleci/shared.yml`:

| Job | What it checks |
|---|---|
| `lint` | pre-commit on the changed files, and actionlint (`mise run lint`) |
| `third-party` | `scripts/check_third_party.py` |
| `guide-screenshots` | the player guide's screenshots and their source review |
| `test-linux` | `tests/run_all.py`, in 4 parts on x86-64 |
| `rust` | the Rust workspace's checks (`builder/`'s crates and `longshot-rs/`): rustfmt, clippy with warnings as errors, `cargo test`, with the distribution's clang, lld and llvm-mc (`LLVM_MC`), which its compile, link and disassembler tests need |
| `e2e` | the builder end to end on a synthetic game (`builder/crates/synthetic-game`): a made-up AArch64 program packed as a game, lifted, compiled, linked and run; no game data |
| `host-windows` | every host unit a Windows game compiles, `-fsyntax-only` with llvm-mingw (`scripts/check_host_compile.py`) |
| `spirv-fixtures` | the shaders' SPIR-V fixtures |
| `linux-longshot-builder` | `longshot-builder-<v>-linux-x64`, the static program with its pack, built in Ubuntu 22.04 |
| `windows-longshot-builder` | `longshot-builder-<v>-windows-x64.exe` with its pack, cross-compiled on Linux and started under Wine |
| `windows-vulkan` | the Windows Vulkan back end, cross-compiled with llvm-mingw |
| `vulkan-dlss` | `longshot_vk.dll` compiled and linked with DLSS and FSR 4 (by hand for now) |
| `website` | the website's build and checks |

What CircleCI does not do: make and publish tools.lock's archives (Forgejo's Make tools workflow, `tools.yml`, which
needs `PACKAGES_TOKEN_SEALED` and the deploy runner). Its release only checks that every line's archive is published
(`release-version`), as Forgejo's does. Nor does its `release-stage` make RecompLauncher's zips yet, which Forgejo's build jobs make
(`longshot-builder dev release recomplauncher`, after `dev release files`): a TODO there says so for the cutover.

`mapping.conf` turns on areas by path (`tests`, `gpu`, `linux`, `windows`, `website`, or `full` for everything: the
workspace's `Cargo.toml` and `Cargo.lock` among them; `longshot-rs/` and `game/` turn on `tests`, `third_party/renhdrum/` both
builder builds);
`lint`, `third-party`, `guide-screenshots` and `website` (the site's checks) always run, and a docs-only change runs
nothing else; `website` turns on the site's preview (pull requests) and deploy (`main`). `main`, tags and API
runs run everything.

**Fork pull requests.** CircleCI's Forgejo integration skips them, so a second CircleCI project (`Longshot-forks`)
runs `.circleci/forks.yml` from a Forgejo webhook. A maintainer adds the label `ci: approved` after reading the change;
the doorbell job checks the pull request (open, from a fork, labelled, its head commit) and runs `events/fork.yml` on
that commit with no secrets. Every push runs again while the label is on; remove it to stop, add it again to rerun.
After 14 days with no push the label is removed.

**Contexts** (secrets): `release-signing`, `forgejo-bot`, `cloudflare-prod`, `forgejo-comment`.

**Checking a change to the config:** `MB_ORB_DIR=<a checkout of MagicBeanDev/orb> pre-commit run --hook-stage manual circleci-config-validate`.

## Forgejo Actions

| Workflow | When | What it does |
|---|---|---|
| `ci.yml` | Pull request, push to `main` | The pull request's title; `rust`: rustfmt, clippy and `cargo test` (with clang, lld and llvm-mc); `e2e`: the synthetic game end to end; `host-windows`: the host's C for Windows with llvm-mingw; actionlint; the website's checks (`scripts/website_check.sh`). `tests/run_all.py` and the SPIR-V fixtures run on CircleCI only. On a pull request, `changes` skips `rust`, `e2e` and `host-windows` when [only docs change](#what-a-pull-request-runs) |
| `pr-description.yml` | Pull request | The description against the template (`scripts/check_pr_description.py`) |
| `third-party.yml` | Pull request, push, nightly | `scripts/check_third_party.py`, and the upstream comparison at night |
| `guide-screenshots.yml` | Pull request, push | The guide screenshots' source review |
| `release.yml` | Push to `main`, tag `v*` | The nightly or the stable release: names the version, calls `build-linux.yml` and `build-windows.yml` (each checks every tools.lock line's archive is in the store before it uploads anything; a release uploads no tool), signs and publishes ([releases](releases.md)) |
| `tools.yml` | By hand (any branch; uploads on `main` only) | Make tools: the build tools named made twice from their pins (`builder/tools/make.sh --check`), uploaded to the store and given as `tools.lock` lines ([bumping a tool](releases.md#bumping-a-tool)) |
| `build-linux.yml`, `build-windows.yml` | Called by `release.yml` | `longshot-builder-<v>-<p>` per platform, its deltas and manifest (`scripts/longshot_builder.py build`; then the runner's longshot-builder's `dev tools check`, `dev release`: `sources`, `dry-run`, `files`), then the back end's build checks in the same job |
| `website.yml` | Push to `main` that changes the site's sources, hourly, pull request | Deploys longshot.magicbean.dev (hourly too, for the progress data the build collects); a pull request that changes the site's sources gets a preview. Its build and checks are `ci.yml`'s `website` job |
| `vulkan-dlss.yml` | By hand | The Windows Vulkan back end built with NVIDIA's DLSS SDK and AMD FSR 4 |
| `vulkan-fixtures.yml` | By hand | The authored Vulkan fixtures on Mesa's software driver |

### Runners

Every job runs on Linux; there is no Windows runner. Windows files are cross-compiled with llvm-mingw's Linux build and
started under Wine. The labels the workflows ask for, all on the image `docker://ghcr.io/catthehacker/ubuntu:act-24.04`:

| Label | For |
|---|---|
| `ubuntu-latest`, `ubuntu-24.04` | the checks. No secrets: a runner anyone in the team lends can carry these |
| `quick` | jobs of a few minutes (title, `changes`, description, third-party, actionlint, guide screenshots), on a runner kept for them so they never wait behind a build |
| `release` | the release's `longshot-builder` builds, and the Make tools workflow's build of `main`'s tools (another branch's runs on `ubuntu-24.04`). **Only the server's release runner, `unraid-linux-1`**, which carries no other label, so CI never takes it; never anyone else's runner. Each such job starts with [scripts/ci_release_runner.sh](../../../scripts/ci_release_runner.sh), which fails unless the job reaches the seal broker with its pinned key, so a runner given the label anywhere else builds nothing. A pull request's own code never runs on it. It needs about 50 GB of disk |
| `deploy` | the jobs that sign, publish or deploy: a release's version and publish, the Make tools workflow's upload, the website's deploy and preview upload. Only the server's quick runner, on the seal broker's network ([below](#secrets-only-our-runners-can-open)). A pull request's own code never runs on it |

The server's runners share 2 cores (MagicBeanDev/Infra, `runner-dind`): a release's jobs run there one at a time.

Register a runner with `forgejo-runner register --instance https://git.magicbean.dev --token <token> --labels ...` from
MagicBeanDev's *Settings > Actions > Runners*, and run `forgejo-runner daemon`. A runner lent by someone else gets
`ubuntu-latest` and `ubuntu-24.04` only. git.magicbean.dev is behind Cloudflare, which cuts uploads at 100 MB: the
organization variable `RELEASE_UPLOAD_API` gives a release runner the server's direct address ([one-time
setup](releases.md#one-time-setup)).

### Caches

Each runner keeps its own cache (forgejo-runner's, on its disk): `ci.yml`'s jobs read it on every run and write it only
on a push to `main`. forgejo-runner 13 keeps a pull request's writes to that pull request (its write isolation key), so a
pull request can't put anything there that another run reads. The jobs that sign or publish a release, and the
website's deploy, read no cache at all; the jobs that build a release read only [their own](#the-releases-cache).

| Cache (key) | What it holds | Who reads it |
|---|---|---|
| `git-base` | main's tree, a bare repository at depth 1 ([scripts/ci_git_base.sh](../../../scripts/ci_git_base.sh)) | `rust`, `e2e`, `host-windows`, `website`: before `actions/checkout`, the workspace borrows its objects, so the fetch takes only what the commit adds (a lent runner fetched the whole tree, 26 MB, through Cloudflare in 2 to 4.5 minutes: now seconds). After the checkout every object is copied in and checked against its name (`git fsck`) |
| `rust-deps-<Rust>-<Cargo.lock>`, `rust-e2e-deps-...` | the registry crates, downloaded and built (dev, release), without the workspace's own: a checkout's new file times make cargo build those again whatever a cache holds ([scripts/ci_cargo_prune.py](../../../scripts/ci_cargo_prune.py)). One entry per lock, written when missing. When the lock has changed, the newest entry for the same Rust is restored instead, and cargo builds only the crates that changed | `rust`, `e2e`; `host-windows` reads `rust-deps` |
| `rustup-<Rust>-<job>` | `~/.rustup` and `~/.cargo/bin`: mise.toml's toolchain with the job's components, as [scripts/ci_rust.sh](../../../scripts/ci_rust.sh) installs it (its rustup checked by SHA-256, the toolchain by rustup against the release's manifest). With it, `ci_rust.sh` finds the toolchain installed and downloads nothing | `rust`, `e2e`, `host-windows` |
| `apt-<job>-<month>` | apt's package lists and the packages it downloaded ([scripts/ci_apt.sh](../../../scripts/ci_apt.sh)): the job installs clang, lld and llvm from them without `apt-get update`, and updates only when that fails. apt checks each package against the lists' hashes as it installs it; a new month starts from the last month's entry | `rust`, `e2e` |
| `<llvm-mingw>-archive`, `windows-tools-downloads-<tools.lock>` | the downloads, each checked by its size and SHA-256 when taken, written when missing | `host-windows` |

`ci.yml`'s jobs run with `CARGO_INCREMENTAL=0` and `CARGO_PROFILE_DEV_DEBUG=0`: incremental state is only ever
written in CI, and the dev builds link faster without debug info (a failing test still names its file and line).

### The release's cache

`build-linux.yml` and `build-windows.yml` keep `release-downloads-<platform>-<Cargo.lock, renhdrum.py, tools.lock>`: the
downloads only, never anything unpacked or compiled. That is the pinned Rust's archives (`$RUNNER_TEMP/rust/*.tar.xz`),
Cargo.lock's `.crate` files (`rust/crates`), tools.lock's archives (`tools-cache`) and, on Windows, llvm-mingw's archive
(`llvm/.cache`). It is restored and saved only by a run of `main` or a `v*` tag, on the server's release runner. A key
change restores the newest entry for the platform.

Why it keeps the guarantee that what a release ships comes from nothing a cache holds:

- **Every file is checked by its SHA-256 each time it is used**, against the pins in the release's own checkout:
  `scripts/renhdrum.py`'s `fetch()` and `vendor_crates()` hash each archive and crate before unpacking it, and download
  it again when it differs. `scripts/longshot_builder.py llvm-mingw` does the same for its archive, and
  `longshot-builder dev tools install` checks each tool by its size and SHA-256. A wrong file costs a download, never a
  different build.
- **Nothing unpacked or built is kept.** The Rust toolchain is unpacked again from the checked archives every run
  (renhdrum.py trusts an unpacked toolchain without checking, so it is never cached). Every program, object and file
  that publish signs is compiled in that run, from that checkout.
- **Who can write a `release-` key:** forgejo-runner's cache is per runner and per repository, and keeps a pull
  request's writes to that pull request. A run of another branch on the release runner could still save a key of this
  name, since the label only routes a job. That is why nothing rests on who wrote the cache: the hash checks above.

### What a pull request runs

`ci.yml`'s `changes` job reads the pull request's changed files from the API ([scripts/ci_changes.py](../../../scripts/ci_changes.py))
and skips `rust`, `e2e` and `host-windows` when every one of them is in a place those jobs never read: `website/`,
`docs/` (but `docs/dev/reference/build-events.md`, which the builder's tests compare with its codes), `.circleci/`, the
other workflows' files and the top-level documents. The Rust workspace's tests and build scripts read `host/`, `gpu/`,
`game/`, `assets/`, `third_party/`, `scripts/`, `tests/` and `tools.lock`, so any other change runs them all. Anything
unsure runs them too: a push to `main`, a run by hand, an API that doesn't answer. The title, actionlint and the
website's checks run on every pull request (the site reads files from all over the repository).

A skipped job is not a failed one: `main` requires no status checks, and Tooling's `forge.py pr status` counts
`skipped` as passed. A pull request's newer push cancels its older runs of `ci.yml`, `third-party.yml`,
`guide-screenshots.yml` and `pr-description.yml` (one run per pull request at a time, the newest); `release.yml` is never
cancelled, and runs of `main` aren't either.

### Secrets only our runners can open

A label only routes a job: a runner says its own labels each time it starts, so anyone's runner can ask for `deploy`
or `release`, and Forgejo hands it the job and the job's secrets. So the secrets that sign or deploy are kept in
Forgejo **sealed** ([scripts/ci_secret.sh](../../../scripts/ci_secret.sh), AES-256 with PBKDF2), and the key that
opens them, `seal.key`, is on the server only, in the **seal broker** (MagicBeanDev/Infra's README, *The seal
broker*). The broker hands it to a job of **main or a `v*` tag** and to no other, and it knows which run a job is from
by the ID token Forgejo signs for the job, not by anything the workflow says: a workflow on another branch, or a pull
request's copy of one, sets its own env, steps and container, and a label only routes it. A job that unseals:

- has `enable-openid-connect: true` (that job only: any step of it can ask for the token) and
  `SEAL_BROKER_PIN: ${{ vars.SEAL_BROKER_PIN }}` (the broker's pinned key, an organization variable);
- runs `scripts/ci_secret.sh unseal`, which fetches the key from the broker the first time (to `$RUNNER_TEMP`);
- runs only code of main or the tag: a job that checks out a pull request must not turn on `enable-openid-connect`.
  The website's preview is two jobs for that reason: the PR is built on `ubuntu-latest`, and main's code uploads its
  `dist/` from `deploy` (`website.yml`).

The broker gives the key to `push` on main or a `v*` tag, `schedule` and `workflow_dispatch` on main, and
`pull_request_target` (main's own workflow file) for Longshot only; anything else gets a refusal that says why. This
rests on main and the `v*` tags being protected: whoever may push to main or push a `v*` tag may have the key.

- A runner that takes such a job and can't reach the broker (anyone else's) gets text it can't open: the job fails, and
  signs or deploys nothing.
- A job of a branch, or of a pull request, gets no key, on any runner.
- **CircleCI's contexts** that hold secrets must be restricted to `main` and the release tags in CircleCI's own settings
  (Organization Settings > Contexts > the context's security: an expression on the branch and tag); the config cannot
  restrict them itself.

| Secret (organization) | Sealed | Used by |
|---|---|---|
| `RELEASE_SIGNING_KEY_SEALED` | the release key's private PEM, `release.pem` | `release.yml` publish ([signing](releases.md#signing)) |
| `PACKAGES_TOKEN_SEALED` | a Forgejo token with `write:package` alone | `tools.yml` publish: the build tools' archives uploaded as BeanTools ([releases](releases.md#one-time-setup)) |
| `CLOUDFLARE_API_TOKEN_SEALED` | the Cloudflare token | `website.yml`, deploy and preview |
| `PREVIEW_COMMENT_TOKEN_SEALED` | optional, a Forgejo token with `write:issue` | `website.yml`, the preview's comment on a fork's PR |

`CLOUDFLARE_ACCOUNT_ID` is not sealed: it opens nothing. To seal a secret, on a PC you trust with a copy of `seal.key`:
`scripts/ci_secret.sh seal --key seal.key < release.pem`, and paste the line into the Forgejo secret. A new
`seal.key` (`scripts/ci_secret.sh newkey`) means sealing every secret again.

What sealing doesn't cover:

- A pull request's or a branch's run may still ask for `deploy` and run there: it gets no key, but it shares the runner
  (and its Docker) with the real deploy jobs. A fork's run waits for a maintainer's approval: read its `.forgejo/`
  changes before you approve one, and don't mark a fork's author as always trusted.
- A job's own token (`forgejo.token`) comes with every job, whichever runner takes it, and on a push it may write to
  the repository (releases, tags).
- A runner that takes `release` builds the files publish signs.

What the `deploy` jobs run is kept to what they need, since every step of such a job can read what it mounts:

- `release.yml`'s and `tools.yml`'s `publish` compile no Rust and run no program the checkout builds (a build script or a proc
  macro would run where `seal.key` is). The build jobs make every file publish signs, RecompLauncher's zips too;
  `tools.yml`'s uploads with `scripts/ci_tools_publish.py`, Python's standard library, the archives its make job made
  twice on a `release` runner, refusing one whose version `tools.lock` names with other bytes. The version job compiles
  nothing: tools.lock is checked by the build jobs, with the builder they build anyway.
- A secret, sealed or the job's token, is given in the `env:` of the steps that use it, never the job's.
- The jobs that make a release's files (`build-linux.yml`, `build-windows.yml`) restore only [the release's
  cache](#the-releases-cache), of downloads each checked by its SHA-256 when used: a cache's key is anyone's to save
  first. Their build checks compile into an object store of the run's own, after `dist/` is uploaded.

Lend runners only to people you'd trust that far.

## Before you push

Run `mise run lint` and `mise run test` and say so in the pull request. CircleCI's runs, logs and reruns are visible to
members of the MagicBeanDev organization on CircleCI ([CONTRIBUTING.md](../../../CONTRIBUTING.md#ci)).
