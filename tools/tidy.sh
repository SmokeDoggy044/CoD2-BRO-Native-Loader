#!/usr/bin/env bash
# clang-tidy (.clang-tidy: bug-finding checks) on the C++ files you changed, or on the paths given.
#   tools/tidy.sh                files changed against HEAD (uncommitted work)
#   tools/tidy.sh BASE           files changed since BASE (e.g. origin/dev)
#   tools/tidy.sh src/render     every file under the paths given (a path must exist)
# Uses the compile commands of build/linux-loader-release (configure it first). The decompiled engine's quirks copy
# the original on purpose: read its findings with that in mind.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
db=build/linux-loader-release
[[ -f $db/compile_commands.json ]] || { echo "no $db/compile_commands.json: cmake --preset linux-loader-release" >&2; exit 1; }
command -v run-clang-tidy > /dev/null || { echo "run-clang-tidy missing (Fedora: clang-tools-extra)" >&2; exit 1; }
if [[ $# -gt 0 && -e $1 ]]; then
    pattern=$(printf '%s|' "$@")
    pattern="(${pattern%|})"
else
    files=$(git diff --name-only "${1:-HEAD}" -- '*.cpp' '*.cc' '*.c' | tr '\n' '|')
    [[ -n $files ]] || { echo "no changed C/C++ files"; exit 0; }
    pattern="(${files%|})\$"
fi
# GCC-only flags in the compile commands are not errors for this purpose.
run-clang-tidy -p "$db" -quiet -j "$(nproc)" -extra-arg=-Wno-unknown-warning-option \
    -extra-arg=-Wno-ignored-optimization-argument "$pattern" 2>&1 | grep -E "warning:|error:|^ +[0-9]+ \|" || true
