#!/bin/sh
# Builds and runs the Qt-free core tests on the build host (docs/design.md §8).
# There is no compiler on the phone side of the workspace; this script is meant
# to be run over ssh on the Arch build host with gcc and Qt 5.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build-tests}"
mkdir -p "$OUT"

CXXFLAGS="-std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter"
INC="-I$ROOT/src -isystem $ROOT/third_party/chess-library"

CORE="$ROOT/src/core/Position.cpp $ROOT/src/core/Uci.cpp $ROOT/src/core/WinProb.cpp \
      $ROOT/src/core/Taxonomy.cpp $ROOT/src/core/Skill.cpp $ROOT/src/core/Placement.cpp \
      $ROOT/src/core/Srs.cpp $ROOT/src/core/Card.cpp $ROOT/src/core/Routine.cpp"

for t in perft uci taxonomy srs placement routine; do
    printf 'building test_%s\n' "$t"
    # shellcheck disable=SC2086
    g++ $CXXFLAGS $INC -o "$OUT/test_$t" "$ROOT/tests/test_$t.cpp" $CORE
done

rc=0
for t in perft uci taxonomy srs placement routine; do
    printf '\n=== test_%s ===\n' "$t"
    "$OUT/test_$t" || rc=1
done
exit $rc
