#!/usr/bin/env bash
# Formats only the lines a change touches (.clang-format), never whole files: the code base was not written by a
# formatter, so whole-file formatting would rewrap thousands of untouched lines.
#   tools/format.sh          your uncommitted changes (against HEAD)
#   tools/format.sh BASE     everything since BASE (e.g. origin/dev before pushing)
# Needs clang-format-diff (Fedora: clang-tools-extra). Afterwards it undoes clang-format's one known misreading: a call
# statement like `own_function<void XB_THISCALL(uint32_t)>(&f)(x);` is also valid C++ for declaring a reference
# `f`, so clang-format writes it as a declaration, `...> (&f)(x);`.
set -euo pipefail
root=$(git rev-parse --show-toplevel)
cd "$root"
base=${1:-HEAD}
command -v clang-format-diff > /dev/null || { echo "clang-format-diff missing (Fedora: clang-tools-extra)" >&2; exit 1; }
# The changed lines since BASE (working tree included), formatted in place.
git diff -U0 --no-color "$base" -- 'src/*' 'tests/*' 'tools/*' | clang-format-diff -p1 -i \
    -iregex '.*\.(c|cc|cpp|h|hpp|inc)$'
git diff --name-only "$base" -- 'src/*' 'tests/*' 'tools/*' | { grep -E '\.(c|cc|cpp|h|hpp|inc)$' || true; } | while read -r file; do
    [[ -f $file ]] || continue
    python3 -I - "$file" <<'EOF'
import re, sys
path = sys.argv[1]
text = open(path, encoding="utf-8").read()
fixed = re.sub(r"(?m)^(\s*(?:return\s+)?[A-Za-z_][\w:]*<[^\n;]*>) \((&[A-Za-z_][\w:]*)\)\(", r"\1(\2)(", text)
if fixed != text:
    open(path, "w", encoding="utf-8").write(fixed)
EOF
done
