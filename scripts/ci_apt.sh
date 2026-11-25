#!/usr/bin/env bash
# Distribution packages for a CI job, with apt's package lists and downloads kept in a folder that actions/cache keeps
# between runs (.forgejo/workflows/ci.yml; docs/dev/reference/ci.md, Caches). `apt-get update` fetched 35 MB of lists at
# under 1 MB/s, most of the minute the rust and e2e jobs spent on clang. With the folder restored, the packages install
# from it with no update; when that fails (no folder yet, or a package the mirror has replaced) the lists are fetched
# again and the install retried. apt checks every package it installs against the lists' hashes, and the lists against
# the distribution's signed Release files when it fetches them, as without a cache.
#
#   scripts/ci_apt.sh DIR package...      DIR: the cached folder (lists/ and archives/ in it)
set -euo pipefail
dir=${1:?usage: ci_apt.sh DIR package...}
shift
mkdir -p "$dir/lists/partial" "$dir/archives/partial"
sudo=()
[[ $(id -u) == 0 ]] || sudo=(sudo)
# (afterwards the folder holds only what the cache keeps, readable by the job's user whoever ran apt: actions/cache
# saves it as that user)
tidy() { "${sudo[@]}" rm -rf "$dir/lists/partial" "$dir/archives/partial" "$dir/lists/lock" "$dir/archives/lock"
         "${sudo[@]}" chmod -R a+rX "$dir"; }
trap tidy EXIT
apt=("${sudo[@]}" apt-get -o "Dir::State::Lists=$dir/lists" -o "Dir::Cache::Archives=$dir/archives" -y -q)
if compgen -G "$dir/lists/*Packages*" > /dev/null &&
    "${apt[@]}" install --no-install-recommends "$@"; then
  echo "ci_apt: installed from the cached lists"
  exit 0
fi
"${apt[@]}" update
"${apt[@]}" install --no-install-recommends "$@"
