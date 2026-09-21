#!/bin/sh
# Builds and runs the tests on the build host (docs/design.md §8).
# There is no compiler on the phone side of the workspace; this script is meant
# to be run over ssh on the Arch build host with gcc and Qt 5.
#
# Two groups, because they need two different amounts of machinery:
#   * the core tests — Qt-free, the whole point of src/core/;
#   * the Qt-layer tests — the fair-play lock (platform.md §3.7) and the
#     Lichess protocol, which need EngineProcess, the database and the client.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build-tests}"
MOC="${MOC:-moc-qt5}"
mkdir -p "$OUT"

CXXFLAGS="-std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter"
INC="-I$ROOT/src -isystem $ROOT/third_party/chess-library"

CORE="$ROOT/src/core/Position.cpp $ROOT/src/core/Uci.cpp $ROOT/src/core/WinProb.cpp \
      $ROOT/src/core/Taxonomy.cpp $ROOT/src/core/Skill.cpp $ROOT/src/core/Placement.cpp \
      $ROOT/src/core/Srs.cpp $ROOT/src/core/Card.cpp $ROOT/src/core/Routine.cpp \
      $ROOT/src/core/SolutionLine.cpp"

CORE_TESTS="perft uci taxonomy srs placement routine line"
QT_TESTS="fairplay lichess session bank feed"

for t in $CORE_TESTS; do
    printf 'building test_%s\n' "$t"
    # shellcheck disable=SC2086
    g++ $CXXFLAGS $INC -o "$OUT/test_$t" "$ROOT/tests/test_$t.cpp" $CORE
done

QT_INC="$(pkg-config --cflags Qt5Core Qt5Network Qt5Sql)"
QT_LIBS="$(pkg-config --libs Qt5Core Qt5Network Qt5Sql)"
QT_LAYER="$ROOT/src/EngineProcess.cpp $ROOT/src/Database.cpp $ROOT/src/Analyser.cpp \
          $ROOT/src/Sparring.cpp $ROOT/src/Lichess.cpp $ROOT/src/GameSync.cpp \
          $ROOT/src/TeacherEngine.cpp $ROOT/src/ItemBank.cpp $ROOT/src/TokenStore.cpp \
          $ROOT/src/SecretsTokenStore.cpp $ROOT/src/Themes.cpp $ROOT/src/PuzzleFeed.cpp"

mkdir -p "$OUT/moc"
for h in EngineProcess Database Analyser Sparring Lichess GameSync PuzzleFeed TeacherEngine; do
    "$MOC" -I"$ROOT/src" -I"$ROOT/third_party/chess-library" "$ROOT/src/$h.h" \
        -o "$OUT/moc/moc_$h.cpp"
done

for t in $QT_TESTS; do
    printf 'building test_%s\n' "$t"
    # SCHACH_SOURCE_DIR lets test_fairplay grep the tree for the Bot API
    # upgrade endpoint, which must not appear anywhere (platform.md §3.1).
    # shellcheck disable=SC2086
    g++ $CXXFLAGS -fPIC $INC $QT_INC -DSCHACH_SOURCE_DIR="\"$ROOT\"" \
        -o "$OUT/test_$t" "$ROOT/tests/test_$t.cpp" $CORE $QT_LAYER "$OUT"/moc/moc_*.cpp $QT_LIBS
done

rc=0
for t in $CORE_TESTS $QT_TESTS; do
    printf '\n=== test_%s ===\n' "$t"
    "$OUT/test_$t" || rc=1
done
exit $rc
