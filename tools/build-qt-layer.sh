#!/bin/sh
# Compiles the Qt layer on the build host so that -Wall -Wextra stays honest.
# The real build is CMake (owned by the UI/packaging side); this only checks
# that the sources and the moc output compile and link.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build-qt}"
MOC="${MOC:-moc-qt5}"
mkdir -p "$OUT"

FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -fPIC"
INC="-I$ROOT/src -isystem $ROOT/third_party/chess-library $(pkg-config --cflags Qt5Core Qt5Network Qt5Sql)"
LIBS="$(pkg-config --libs Qt5Core Qt5Network Qt5Sql)"

# Lichess and GameSync came with M8 (platform.md §3); they are QObjects like
# the rest and go through moc the same way.
for h in EngineProcess Database Analyser Sparring Lichess GameSync TeacherEngine; do
    "$MOC" -I"$ROOT/src" -I"$ROOT/third_party/chess-library" "$ROOT/src/$h.h" -o "$OUT/moc_$h.cpp"
done

# shellcheck disable=SC2086
g++ $FLAGS $INC -o "$OUT/qt-smoke" "$ROOT/tools/qt-smoke.cpp" \
    "$ROOT/src/EngineProcess.cpp" "$ROOT/src/Database.cpp" "$ROOT/src/Analyser.cpp" \
    "$ROOT/src/Sparring.cpp" "$ROOT/src/Lichess.cpp" "$ROOT/src/GameSync.cpp" \
    "$ROOT/src/TeacherEngine.cpp" "$ROOT/src/ItemBank.cpp" \
    "$ROOT/src/core/Position.cpp" "$ROOT/src/core/Uci.cpp" "$ROOT/src/core/WinProb.cpp" \
    "$ROOT/src/core/Taxonomy.cpp" "$ROOT/src/core/Skill.cpp" "$ROOT/src/core/Placement.cpp" \
    "$ROOT/src/core/Srs.cpp" "$ROOT/src/core/Card.cpp" "$ROOT/src/core/Routine.cpp" \
    "$OUT"/moc_*.cpp $LIBS
"$OUT/qt-smoke"
