# The placement-test item bank

`placement.json` is the item bank that `src/ItemBank.cpp` loads for the adaptive
placement test (teacher.md §4). Without it the test refuses to ask anything —
that is deliberate: an empty bank is a stated condition, never a silent fallback
to the starting position.

Everything here is data. Nothing in it was taken on trust: every single item was
put to a real Stockfish before it was written to the file, and
`tools/verify_items.py` re-runs that check on demand.

## Format

A JSON array (an object with an `"items"` array is also accepted). One item:

```json
{
  "id": "tak-1254-0inao",
  "fen": "2bq2kr/p3pR2/1p5r/2pPp1pB/2P1P1P1/5Q2/P2N4/6K1 w - - 4 28",
  "solution": "f7g7",
  "alsoAccepted": [],
  "dimension": "TAK",
  "difficulty": 1254,
  "explanation": "Der Turm opfert sich, um den König nach g7 zu ziehen. …"
}
```

| field | meaning |
|---|---|
| `id` | unique, `<dimension>-<difficulty>-<source>`. Where the source part is five characters of Lichess' own puzzle id, the position can be looked up at `https://lichess.org/training/<id>`; otherwise it is a short name for a composed position. |
| `fen` | the position **as the learner sees it**. It is already that side's turn — no first move has to be applied. |
| `solution` | the one move that counts, in UCI (`e2e4`, `e7e8q`). |
| `alsoAccepted` | UCI moves the engine rated as good as the solution, so the learner is not punished for them. Usually empty. |
| `dimension` | one of `TAK`, `SRG`, `REC`, `END`, `STL`, `ERD` — exactly the keys `core::dimensionFromKey()` accepts (`src/core/Skill.h`). |
| `difficulty` | Elo-scale difficulty, 700…2050. For Lichess-derived items this is their Glicko-2 puzzle rating, taken over unchanged as teacher.md §4.2 prescribes; for composed items it is a set start value, and §4.4 keeps the uncertainty of such items artificially high. |
| `explanation` | one German sentence, second person, shown **after** the learner has answered. It says *why* the move is right, not what it wins. Never shown before — teacher.md §6.2 forbids announcing the theme. |

The item never names its theme, motif or dimension to the learner. That is the
whole point of §6.2, and the format has no field for it.

## What the bank contains

105 items, spread over all six dimensions and over nine ~150-point difficulty
bands from 700 to 2050, so the adaptive selection (§4.5, target `θ − 147`)
always finds something near its target:

```
dim     700-850  850-1000 1000-1150 1150-1300 1300-1450 1450-1600 1600-1750 1750-1900 1900-2050  total
TAK           3         2         2         2         2         2         2         2         2     19
SRG           .         1         2         2         2         2         2         2         2     15
REC           .         2         2         2         2         2         2         2         2     16
END           3         2         2         2         2         2         2         2         3     20
STL           .         .         .         3         3         3         3         3         3     18
ERD           1         2         2         2         2         2         2         2         2     17
all           7         9        10        13        13        13        13        13        14    105
```

**32 of the 105 items (30.5 %) are quiet**: the best move is neither a capture
nor a check nor a promotion, and it does not threaten to win material either.
teacher.md §6.3 calls that mandatory share the most important design decision in
the whole document, and it is: a bank of nothing but combinations teaches "there
is always a tactic here", which is exactly the habit the app exists to break.
The whole `STL` dimension is quiet, and so are the pawn-endgame, opposition and
key-square items in `END`.

`STL` has no items below 1150 and `SRG` none below 850. Quiet positional
problems that are genuinely *easy* barely exist — below 1000 Elo almost every
calibrated position is a capture or a mate — so the low bands are carried by
`END`, `TAK` and `ERD`, and `ItemBank::pick()` falls back across dimensions
anyway.

## Where the positions come from

Two sources, both public, both citable.

1. **The Lichess puzzle database** (`lichess_db_puzzle.csv`, 6 100 952 puzzles,
   CC0 — "use them for research, commercial purpose, publication, anything you
   like"), the source teacher.md §4.2 recommends by name. Filtered on
   `RatingDeviation ≤ 75…85`, `Popularity ≥ 80…90`, `NbPlays ≥ 400…1500`, then
   stratified over the rating bands and the dimensions, then **re-verified here
   from scratch** — the Glicko-2 rating is taken over, the claim that the
   solution is unique is not.
   Note the semantics that trips everyone up: in that file the FEN is the
   position *before* the opponent's move, and the position to show is the one
   after `Moves[0]`. `fen` in this bank is already the shown position and
   `solution` is `Moves[1]`.
2. **Composed and classic textbook positions**, ten of them: the Réti study
   (1921), the Saavedra position (1895), a key-square and an opposition position
   in king-and-pawn, and six opening traps that are common property — fool's
   mate, scholar's mate, the elephant trap, the Englund and Budapest traps, and
   the Dutch `Bg3` trap. These carry the `END` and `ERD` low and high ends,
   where the puzzle database is thin.

No commercial puzzle set was copied or consulted.

## How the bank was verified

```sh
tools/verify_items.py \
    --items assets/items/placement.json \
    --engine ~/bin/stockfish-host \
    --engine-dir ~/schach-build/Stockfish-sf_17.1/src \
    --movetime 2000 --threads 4 --hash 512
```

It needs `python-chess` (`pip install chess`) and a UCI engine. The run that
produced this bank used **Stockfish 17.1**, built natively on x86-64, with its
own `nn-37f18f62d772.nnue` (hence `--engine-dir`, so the engine finds it; the
alternative is `--eval-file`), **MultiPV 3, `go movetime 2000`, Threads 4,
Hash 512 MB**. All 105 items passed; the script exits non-zero if one does not.

For every item the script asserts:

1. the FEN is legal, the position is not over, and the side to move really has a
   choice (at least two legal moves);
2. `solution` is a legal move there;
3. with MultiPV 3 the solution is the engine's **best** move, and the gap to the
   best move that is not accepted is wide enough that the item has one answer;
4. no duplicate ids and no duplicate positions;
5. the claimed dimension is plausible for the position — an `END` item really is
   an endgame by material count, an `STL` item's best move really is quiet, a
   `REC` item's line really is forcing and at least five plies long, an `SRG`
   item really has something hanging or threatened, an `ERD` item really is
   still in the opening;
6. at least a quarter of the bank is quiet.

### The uniqueness bar, and why it is what it is

teacher.md §4.2 quotes the bar Lichess' own generator uses: the best move must
beat the second best by **0.7 on the `winningChances` scale** (35 percentage
points of win probability). The spec states it as the reason Lichess puzzles are
unique and calls it "far stricter than any error threshold". It is right for
forced combinations and *impossible* for the quiet items §6.3 makes mandatory —
a quiet position cannot swing by 35 points and stay quiet.

So the script uses the spec's own error ladder (§0.5, §2.3.1) instead, one rung
per kind of item, and prints which bar it applied:

| item | bar | why that number |
|---|---|---|
| forcing | gap ≥ **0.36 wc** (18 pp) **and** ≥ **150 cp** | 18 pp is the app's own blunder line (`src/core/WinProb.h`): playing the second-best move must cost at least a blunder. 150 cp is the floor asked for tactical items. |
| quiet | gap ≥ **0.20 wc** (10 pp) **and** ≥ **80 cp** | 10 pp is the Lichess "mistake" line (§0.5, in the corrected reading). Demanding a mistake's worth is the honest bar for a position where nothing is forced. |

Both are choices of this script, not numbers lifted from the spec, and they are
stated in its output. As it turns out the bank clears them comfortably: the
smallest gap in it is **0.38 wc**, the median **0.95 wc**, and about 80 of the
105 items (76 %) clear the strict Lichess 0.70 bar as well.

A fixed `movetime` makes the engine slightly non-deterministic, so those last
figures move by a point or two between runs and a borderline item could in
principle flip. Nothing in this bank is anywhere near the bar — the run margin
seen so far is a couple of centipawns on items that clear it by hundreds — but
if you want a bit-for-bit repeatable check, use `--depth 18` instead of
`--movetime 2000`; it is slower and not otherwise different.

Two further rules:

* **`alsoAccepted`.** A second move within **0.04 wc and 25 cp** of the best one
  is the same answer as far as the learner is concerned, so it is accepted
  instead of punished. The script computes the list and fails if the file
  disagrees; `--write-accepted` rewrites it. In this bank the list is empty for
  every item — the gaps are wide enough that no second move ever came close —
  but the mechanism is checked on every run, so an item that later develops a
  twin is caught rather than silently marking a good move wrong.
* **Three moves that close and the item is rejected.** Forced mates all count as
  equal here, because teacher.md §0.5 says plainly that a slower mate instead of
  a faster one is never a mistake. That rule has a consequence worth knowing:
  a plain "mate in one" with a lone king — K+Q or K+R against K — can *never*
  be an item, because every other move mates too, only later. The basic-mate
  end of `END` is therefore carried by positions where exactly one move makes
  progress (opposition, key squares, the Réti and Saavedra studies) and by mate
  positions from real games where only one move mates at all.

## Adding an item

1. Put the position, the move, the dimension, a difficulty and one German
   sentence into `placement.json`. The `fen` is the position the learner sees.
2. Run the script with `--write-accepted` so `alsoAccepted` is filled in by the
   engine rather than by hand:

   ```sh
   tools/verify_items.py --engine … --engine-dir … --write-accepted --only <id>
   ```

3. Run it again without `--write-accepted` over the whole bank, and only commit
   when it exits 0. `--only <id>` re-checks a single item while you iterate;
   the quiet-share and minimum-size checks are skipped in that mode.

Rules of thumb that save a round trip:

* Simple and teachable beats spectacular. This is a measurement instrument.
* Do not write the theme into the explanation *before* the move — the sentence
  is only ever shown afterwards, but keep it about the idea ("the knight hits
  both, and neither can move away") rather than the bare fact ("Nf7+ wins the
  queen").
* An item whose second-best move is nearly as good is not a hard item, it is a
  broken one. The script will say so.
* If you add a tactical item, add a quiet one too. The 25 % share in §6.3 is a
  floor, not a target, and it is the easiest thing in this bank to erode.
