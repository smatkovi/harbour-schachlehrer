#!/bin/sh
# Builds the MeeGo Harmattan (Nokia N9 / N950) edition of Tarock on the build
# machine.
#
#   meego/build.sh arm     -> build/meego/arm/harbour-schachlehrer
#
# MADDE's own GCC 4.4 cannot compile the C++17 core, so this uses the GCC 14
# cross toolchain from harbour-snapszer/meego/toolchain.sh (XGCC) against the
# MADDE sysroot. moc and lrelease come from the Qt Simulator's Qt 4.7.4, which
# is the version on the device.
set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
MODE=${1:-arm}
XGCC=${XGCC:-/tmp/xgcc-harmattan}
SYSROOT=${SYSROOT:-$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim}
SIMQT=${SIMQT:-$HOME/QtSDK/Simulator/Qt/gcc}
JOBS=${JOBS:-8}
OUT=$HERE/build/meego/$MODE
mkdir -p "$OUT"

# SecretsTokenStore.cpp is left out: it is the Sailfish Secrets backend and is
# guarded by SCHACH_HAVE_SAILFISH_SECRETS, which is simply not defined here.
# The plain TokenStore takes its place, as on any Sailfish build without the
# Secrets development package.
ENGINE_SRC="src/Analyser.cpp src/Database.cpp src/EngineProcess.cpp src/GameSync.cpp \
 src/ItemBank.cpp src/Lichess.cpp src/PuzzleFeed.cpp src/Sparring.cpp \
 src/TeacherEngine.cpp src/Themes.cpp src/TokenStore.cpp \
 src/core/Card.cpp src/core/Placement.cpp src/core/Position.cpp src/core/Routine.cpp \
 src/core/Skill.cpp src/core/SolutionLine.cpp src/core/Srs.cpp src/core/Taxonomy.cpp \
 src/core/Uci.cpp src/core/WinProb.cpp"
MOC_HEADERS="src/Analyser.h src/Database.h src/EngineProcess.h src/GameSync.h \
 src/Lichess.h src/PuzzleFeed.h src/Sparring.h src/TeacherEngine.h"

QT4_FLAGS="-std=gnu++17 -O2 -Wall -Wno-register -Wno-deprecated-declarations -Wno-nonnull \
 -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DQT_NO_DEBUG \
 -I$HERE/meego/compat -include $HERE/meego/compat/qt4compat.h \
 -include $HERE/meego/compat/c99math.h -I$HERE/src -I$HERE/src/core -I$HERE/third_party/chess-library"
QT4_MODULES="QtCore QtGui QtNetwork QtScript QtDeclarative QtSql"

case "$MODE" in
arm)
    CXX=$XGCC/bin/arm-none-linux-gnueabi-g++
    [ -x "$CXX" ] || { echo "cross compiler missing: $CXX" >&2; exit 1; }
    MOC=$SIMQT/bin/moc
    QTINC=$SYSROOT/usr/include/qt4
    CXXFLAGS="--sysroot=$SYSROOT $QT4_FLAGS -I$QTINC"
    for m in $QT4_MODULES; do CXXFLAGS="$CXXFLAGS -I$QTINC/$m"; done
    # Hard-float Harmattan still uses ld-linux.so.3; --exclude-libs keeps the
    # static libstdc++/libgcc private so Qt stays on its own GCC 4.4 runtime.
    LDFLAGS="--sysroot=$SYSROOT -static-libstdc++ -static-libgcc -Wl,-O1 -Wl,--as-needed \
 -Wl,--exclude-libs,ALL -Wl,--dynamic-linker=/lib/ld-linux.so.3"
    LIBS="-lQtDeclarative -lQtScript -lQtSql -lQtNetwork -lQtGui -lQtCore -lpthread"
    ;;
*)
    echo "usage: $0 arm" >&2; exit 2 ;;
esac

MK=$OUT/Makefile
{
    echo "CXX=$CXX"; echo "MOC=$MOC"; echo "CXXFLAGS=$CXXFLAGS"
    echo "LDFLAGS=$LDFLAGS"; echo "LIBS=$LIBS"; echo "SRC=$HERE"; echo
    objs=
    for s in $ENGINE_SRC; do
        o=$(basename "$s" .cpp).o; objs="$objs $o"
        echo "$o: \$(SRC)/$s"; printf '\t$(CXX) $(CXXFLAGS) -c $< -o $@\n'
    done
    for h in $MOC_HEADERS; do
        n=$(basename "$h" .h); objs="$objs moc_$n.o"
        echo "moc_$n.cpp: \$(SRC)/$h"; printf '\t$(MOC) $< -o $@\n'
        echo "moc_$n.o: moc_$n.cpp"; printf '\t$(CXX) $(CXXFLAGS) -c $< -o $@\n'
    done
    echo "ENGINE_OBJS=$objs"; echo
    echo "all: harbour-schachlehrer"
    echo "main.moc: \$(SRC)/meego/main.cpp"; printf '\t$(MOC) $< -o $@\n'
    echo "main.o: \$(SRC)/meego/main.cpp main.moc"; printf '\t$(CXX) $(CXXFLAGS) -I. -c $< -o $@\n'
    echo "harbour-schachlehrer: main.o \$(ENGINE_OBJS)"
    printf '\t$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)\n'
} > "$MK"

nice make -C "$OUT" -j"$JOBS" all

# Translations. Qt 4.7's lrelease only knows TS version 2.0 while the
# catalogues say 2.1, so the header is rewritten on the way in.
LRELEASE=$SIMQT/bin/lrelease
mkdir -p "$OUT/translations"
# The generated one carries the German UI text; see meego/german-catalogue.py.
for lang in de; do
    src=$HERE/meego/harbour-schachlehrer-$lang.ts
    [ -f "$src" ] || continue
    sed 's/<TS version="2\.1"/<TS version="2.0"/' "$src" > "$OUT/harbour-schachlehrer-$lang.ts"
    # The same Latin-1 substitution the QML got, so the keys still match.
    python3 "$HERE/meego/ascii-qstr.py" ts "$OUT/harbour-schachlehrer-$lang.ts" >/dev/null
    "$LRELEASE" -silent "$OUT/harbour-schachlehrer-$lang.ts" \
        -qm "$OUT/translations/harbour-schachlehrer-$lang.qm"
done

echo "== built $OUT/harbour-schachlehrer"
