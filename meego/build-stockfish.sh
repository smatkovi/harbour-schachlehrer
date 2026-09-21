#!/bin/sh
# Cross-builds Stockfish 17.1 for MeeGo Harmattan (Nokia N9 / N950).
#
#   meego/build-stockfish.sh      -> build/meego/stockfish  (stripped, ~8.4 MB)
#
# The prebuilt engine in third_party/prebuilt/armv7hl is for Sailfish: armhf,
# dynamically linked against a modern glibc, so it does not start on
# Harmattan's 2.10. This builds one that does.
#
# Four things had to be got right; each cost a failed build, so they are
# written down rather than rediscovered:
#
#  1. std::round and every other C99 maths function is missing from namespace
#     std in this toolchain's libstdc++ (::round exists, std::round does not),
#     because it was configured against glibc 2.10. meego/compat/c99math.h is
#     force-included to pull them in. Not the long double variants -- on ARM
#     glibc 2.10 roundl is not declared at all.
#
#  2. A fully static binary segfaults on the first std::thread, right after
#     clone(). Static glibc and pthreads do not go together here. glibc is
#     therefore linked dynamically -- the device has exactly this 2.10 -- and
#     only libstdc++/libgcc are static.
#
#  3. Harmattan has no libatomic.so.1. Stockfish's Makefile appends -latomic
#     after EXTRALDFLAGS, so -Wl,-Bstatic cannot win; the flag is removed from
#     the Makefile and libatomic.a passed by path instead.
#
#  4. The loader is /lib/ld-linux.so.3, not the armhf name GCC would ask for.
#
# Both NNUE slots are pointed at the small net (chess-spec/platform.md 8.4),
# which is what keeps the resident set at 50 MB instead of 273 MB.
#
# Measured on an N950: 50.888 kB VmRSS, depth 14 in 4.1 s, 33-72k nodes/s,
# and it answers e2e4 from the start position.
set -e
cd "$(dirname "$0")/.."

XGCC=${XGCC:-/tmp/xgcc-harmattan}
SYSROOT=${SYSROOT:-$HOME/QtSDK/Madde/sysroots/harmattan_sysroot_10.2011.34-1_slim}
REMOTE=${REMOTE:-/tmp/sf-harmattan}
NET=nn-37f18f62d772.nnue

HOST=$(sh "$HOME/ps/nfsshift-sfos/tools/buildhost.sh")
echo "== build host: $HOST"

[ -f "assets/$NET" ] || { echo "assets/$NET missing" >&2; exit 1; }
ssh "$HOST" "mkdir -p $REMOTE"
rsync -a -e ssh "assets/$NET" meego/compat/c99math.h "$HOST:$REMOTE/"

ssh "$HOST" "set -e
    cd $REMOTE
    [ -d Stockfish ] || git clone --branch sf_17.1 --depth 1 \
        https://github.com/official-stockfish/Stockfish.git
    cd Stockfish/src

    # Both network slots on the small net: 50 MB resident instead of 273 MB.
    sed -i 's/TransformedFeatureDimensionsBig = 3072;/TransformedFeatureDimensionsBig = 128;/' \
        nnue/nnue_architecture.h
    sed -i 's/#define EvalFileDefaultNameBig \"nn-1c0000000000.nnue\"/#define EvalFileDefaultNameBig \"$NET\"/' \
        evaluate.h
    sed -i 's/ -latomic//g' Makefile
    cp $REMOTE/$NET .

    make clean >/dev/null 2>&1 || true
    nice -n 15 make -j6 ARCH=armv7-neon COMP=gcc \
        CXX=$XGCC/bin/arm-none-linux-gnueabi-g++ \
        EXTRACXXFLAGS='--sysroot=$SYSROOT -include $REMOTE/c99math.h' \
        EXTRALDFLAGS='--sysroot=$SYSROOT -static-libstdc++ -static-libgcc -pthread \
 -Wl,--dynamic-linker=/lib/ld-linux.so.3 $XGCC/arm-none-linux-gnueabi/lib/libatomic.a' \
        build
    $XGCC/bin/arm-none-linux-gnueabi-strip -s stockfish
    readelf -d stockfish | grep NEEDED
"

mkdir -p build/meego
rsync -a -e ssh "$HOST:$REMOTE/Stockfish/src/stockfish" build/meego/
ls -la build/meego/stockfish
