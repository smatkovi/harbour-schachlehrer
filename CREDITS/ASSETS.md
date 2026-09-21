# Asset credits

Every image, font and data file that ships with harbour-schachlehrer, with its
source, URL, retrieval date, licence, author and the changes made to it.
`AboutPage.qml` points here and the RPM installs this file to
`/usr/share/doc/harbour-schachlehrer/`, so keep it readable as plain text.

## Chess pieces — cburnett

| | |
|---|---|
| Files | `assets/pieces/cburnett/Chess_{k,q,r,b,n,p}{l,d}t45.svg` — twelve files |
| Source | Wikimedia Commons |
| URL | `https://commons.wikimedia.org/wiki/Special:FilePath/Chess_<piece><colour>t45.svg` |
| Author | Colin M. L. Burnett (User:Cburnett) |
| Retrieved | 2026-09-12 |
| Licence | **BSD-3-Clause** |
| Changes | none — the files are byte-for-byte as served by Wikimedia |

The originals on Commons are quadruple-licensed at the user's choice: GFDL 1.2+,
CC BY-SA 3.0, **BSD 3-Clause** and GPL v2+. This project takes the BSD-3-Clause
option, which is why they are fetched **from Wikimedia Commons** and not from
`lichess-org/lila`: lila's `COPYING.md` puts its own copy of the same artwork
under GPLv2+ only (chess-spec/platform.md §4.3).

The twelve files, with the Commons file name each was fetched under:

| Piece | White | Black |
|---|---|---|
| King | `Chess_klt45.svg` | `Chess_kdt45.svg` |
| Queen | `Chess_qlt45.svg` | `Chess_qdt45.svg` |
| Rook | `Chess_rlt45.svg` | `Chess_rdt45.svg` |
| Bishop | `Chess_blt45.svg` | `Chess_bdt45.svg` |
| Knight | `Chess_nlt45.svg` | `Chess_ndt45.svg` |
| Pawn | `Chess_plt45.svg` | `Chess_pdt45.svg` |

BSD 3-Clause requires the copyright notice and the disclaimer to be reproduced
with binary distributions. This file is that notice, and the RPM installs it:

> Copyright (c) 2007, Colin M.L. Burnett. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without
> modification, are permitted provided that the following conditions are met:
> redistributions of source code must retain the above copyright notice, this
> list of conditions and the following disclaimer; redistributions in binary
> form must reproduce the above copyright notice, this list of conditions and
> the following disclaimer in the documentation and/or other materials provided
> with the distribution; neither the name of the copyright holder nor the names
> of its contributors may be used to endorse or promote products derived from
> this software without specific prior written permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
> AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
> IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
> ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
> LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
> CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
> SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
> INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
> CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
> ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
> POSSIBILITY OF SUCH DAMAGE.

The board itself is drawn in `Board.qml` — two colours, no image. Board graphics
from `lila/public/images/board` are AGPLv3+ and are deliberately not used.

## The board squares

| | |
|---|---|
| Files | none — `sailfish/Board.qml`, `Style.lightSquare` / `Style.darkSquare` |
| Author | smatkovi |
| Licence | GPL-3.0-or-later, same as the code |

## Application icon

| | |
|---|---|
| Files | `sailfish/icons/icon-{86,108,128,172,256}.png` |
| Source | drawn for this project by `tools/make_icon.py` |
| Author | smatkovi |
| Licence | GPL-3.0-or-later, same as the code |
| Changes | — |

A board corner in two wood tones with a knight on it. The knight is the glyph
U+265E of DejaVu Sans (Bitstream Vera / public-domain-equivalent licence),
rasterised at build time — no font file ships with the package.

## Placement and drill items — Lichess puzzle database

| | |
|---|---|
| File | `assets/items/puzzles.json` — 3 944 items |
| Source | Lichess database exports |
| URL | `https://database.lichess.org/lichess_db_puzzle.csv.zst` |
| Author | Lichess and its solvers; the ratings are Glicko-2 values earned by millions of solve attempts |
| Retrieved | 2026-09-17 (file dated 2026-09-09, 6 100 952 puzzles) |
| Licence | **CC0 1.0**, verbatim: "Database exports are released under the Creative Commons CC0 license. Use them for research, commercial purpose, publication, anything you like." |
| Changes | filtered, stratified and reshaped by `tools/import_lichess_puzzles.py`; see `assets/items/README.md` |

CC0 requires no attribution. It is given anyway, because the calibration is the
valuable part and it was not ours to make: chess-spec/teacher.md §4.2 builds the
whole measurement on it.

The hand-written `assets/items/placement.json` draws on the same database for
some of its positions; its ids carry five characters of the Lichess puzzle id,
so each can be looked up at `https://lichess.org/training/<id>`.

## Syzygy endgame tablebases

| | |
|---|---|
| Files | `assets/syzygy/*.rtbw`, `assets/syzygy/*.rtbz` — 70 files, 4 350 176 bytes |
| Content | all tables for three and four men, WDL and DTZ |
| Source | `https://tablebase.lichess.ovh/tables/standard/3-4-5-wdl/` and `.../3-4-5-dtz/` |
| Author | Ronald de Man (generator `syzygy1/tb`) |
| Retrieved | 2026-09-12 |
| Licence | public domain — the tables are computed facts about the rules of chess; the generator is under a BSD-style licence and the tables themselves carry no claim |
| Changes | none |

35 WDL and 35 DTZ files: KBBvK KBNvK KBPvK KBvK KBvKB KBvKN KBvKP KNNvK KNPvK
KNvK KNvKN KNvKP KPPvK KPvK KPvKP KQBvK KQNvK KQPvK KQQvK KQRvK KQvK KQvKB
KQvKN KQvKP KQvKQ KQvKR KRBvK KRNvK KRPvK KRRvK KRvK KRvKB KRvKN KRvKP KRvKR.
Five-man tables are not shipped and not offered as a download
(chess-spec/platform.md §2, Empfehlung 2).

## Stockfish

| | |
|---|---|
| Files | `/usr/share/harbour-schachlehrer/bin/stockfish` in the package, built from `third_party/prebuilt/<arch>/stockfish` |
| Source | `https://github.com/official-stockfish/Stockfish`, release 17.1 |
| Licence | **GPL-3.0-or-later** |
| Changes | built with `-DNNUE_EMBEDDING_OFF` and the two-line small-net-only patch of chess-spec/platform.md §8.4 |

Shipped as a **separate program**, started with `QProcess` and spoken to over
UCI — not linked into the application (platform.md §1.7, §1.9). The licence, the
authors file and the patch are installed to
`/usr/share/licenses/harbour-schachlehrer/stockfish/` by the packaging step
that builds it.

## NNUE evaluation net

| | |
|---|---|
| Files | `assets/nn-37f18f62d772.nnue`, packaged as `/usr/share/harbour-schachlehrer/net/nn-37f18f62d772.nnue` |
| Source | `https://tests.stockfishchess.org/api/nn/nn-37f18f62d772.nnue` |
| Author | the Stockfish project (Fishtest contributors) |
| Retrieved | 2026-09-12 |
| Licence | **GPL-3.0-or-later**, as part of Stockfish |
| Changes | none — the file name carries its own SHA-256 prefix, which `tools/fetch-engine.sh` verifies |

The small net. The engine is built with `-DNNUE_EMBEDDING_OFF` and the
small-net-only patch, so this one 3,36 MiB file serves for every evaluation
instead of the 71 MiB big net; engine RSS drops from 273 MiB to 57 MiB, which
is the difference between running and being killed on a 1 GB phone
(chess-spec/platform.md §1.4, §6.3).

## Chess model

| | |
|---|---|
| Files | `third_party/chess-library/` |
| Source | `https://github.com/Disservin/chess-library` |
| Licence | MIT |
| Changes | vendored unchanged; the PGN writer is written for this project |
