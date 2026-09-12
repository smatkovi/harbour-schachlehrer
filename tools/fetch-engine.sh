#!/bin/bash
# Builds Stockfish 17.1 for both Sailfish targets and fetches the Syzygy
# tablebases. The results are binaries and data, so they are not in git; run
# this once before building the RPM. See docs/design.md §5 and
# ../chess-spec/platform.md §1 and §2 for why each step is the way it is.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WORK:-$ROOT/.engine-build}"
SDK="${SDK:-sfossdk52}"          # the Sailfish SDK docker container
NET=nn-37f18f62d772.nnue         # the small net, 3.36 MiB
mkdir -p "$WORK" && cd "$WORK"

[ -f sf_17.1.tar.gz ] || curl -sSL -o sf_17.1.tar.gz \
  https://github.com/official-stockfish/Stockfish/archive/refs/tags/sf_17.1.tar.gz
[ -f "$NET" ] || curl -sSL -o "$NET" "https://tests.stockfishchess.org/api/nn/$NET"
# The file name carries its own hash, so this is a real check.
sha256sum "$NET" | grep -q "^37f18f62d772" || { echo "net checksum mismatch"; exit 1; }

rm -rf Stockfish-sf_17.1 && tar xzf sf_17.1.tar.gz
cd Stockfish-sf_17.1/src
# Small-net-only: both network instances use the small architecture, so one
# 3.36 MiB file serves for everything. 273 MiB RSS become 57 MiB.
sed -i 's/^constexpr IndexType TransformedFeatureDimensionsBig = 3072;/constexpr IndexType TransformedFeatureDimensionsBig = 128;/' nnue/nnue_architecture.h
sed -i "s/^#define EvalFileDefaultNameBig.*/#define EvalFileDefaultNameBig   \"$NET\"/" evaluate.h
cp -f "$WORK/$NET" .                 # so the Makefile does not try to download it
cd "$WORK"

docker exec -u root "$SDK" bash -lc 'rm -rf /home/mersdk/sf-src'
docker cp Stockfish-sf_17.1 "$SDK":/home/mersdk/sf-src
docker exec -u root "$SDK" bash -lc 'chown -R mersdk:mersdk /home/mersdk/sf-src'
for pair in "aarch64:armv8" "armv7hl:armv7-neon"; do
  target="${pair%%:*}"; sfarch="${pair##*:}"
  docker exec "$SDK" bash -lc "cd ~/sf-src/src && make clean >/dev/null 2>&1;
    sb2 -t SailfishOS-5.2.0.15-$target make -j\$(nproc) ARCH=$sfarch COMP=gcc \
        EXTRACXXFLAGS=-DNNUE_EMBEDDING_OFF build >/dev/null &&
    sb2 -t SailfishOS-5.2.0.15-$target strip -s stockfish &&
    mkdir -p ~/sf-out/$target && cp stockfish ~/sf-out/$target/"
  mkdir -p "$ROOT/third_party/prebuilt/$target"
  docker cp "$SDK":/home/mersdk/sf-out/$target/stockfish "$ROOT/third_party/prebuilt/$target/"
done
cp -f "$WORK/$NET" "$ROOT/assets/"

mkdir -p "$ROOT/assets/syzygy" && cd "$ROOT/assets/syzygy"
BASE=https://tablebase.lichess.ovh/tables/standard
for t in KBBvK KBNvK KBPvK KBvK KBvKB KBvKN KBvKP KNNvK KNPvK KNvK KNvKN KNvKP \
         KPPvK KPvK KPvKP KQBvK KQNvK KQPvK KQQvK KQRvK KQvK KQvKB KQvKN KQvKP \
         KQvKQ KQvKR KRBvK KRNvK KRPvK KRRvK KRvK KRvKB KRvKN KRvKP KRvKR; do
  [ -f "$t.rtbw" ] || curl -sSLO "$BASE/3-4-5-wdl/$t.rtbw"
  [ -f "$t.rtbz" ] || curl -sSLO "$BASE/3-4-5-dtz/$t.rtbz"
done
echo "engine, net and $(ls | wc -l) tablebase files ready"
