#!/usr/bin/env python3
# Copyright (C) 2026 smatkovi
#
# This file is part of harbour-schachlehrer.
#
# harbour-schachlehrer is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# harbour-schachlehrer is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with harbour-schachlehrer. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a multi-move item bank out of the Lichess puzzle database.

    tools/import_lichess_puzzles.py --input lichess_db_puzzle.csv.zst \
                                    --output assets/items/puzzles.json

chess-spec/teacher.md §4.2 is the reason this exists, and it states the design
advantage in one line: **we do not have to calibrate anything.** Lichess has
already done it, on millions of solvers, and publishes the result under CC0:

    "Database exports are released under the Creative Commons CC0 license.
     Use them for research, commercial purpose, publication, anything you
     like."

`Rating` is a Glicko-2 value produced by treating every solve attempt as a
rated game between solver and puzzle. That is exactly the item difficulty the
placement test of §4.4 wants, and it costs us nothing.

The one semantic detail that ruins everything if it is missed
-------------------------------------------------------------
Quoted in §4.2 from the database's own documentation:

    "FEN is the position **before** the opponent makes their move. The
     position to present to the player is **after** applying the first move to
     that FEN."

So `Moves[0]` is the opponent's move into the puzzle, and the learner's line is
`Moves[1:]`. Getting this wrong shows every position one move too early, and it
looks almost right, which is worse.

What this tool does *not* solve
-------------------------------
The 10 % "Hier ist nichts" quota of §6.3 — positions where **no** move gains
anything and the task is to play something solid. It cannot come from here: a
Lichess puzzle by construction has a winning line. Those items have to be
generated against the engine from the Lichess evaluation database, and the
summary at the end says so rather than letting the quota look met.
"""

import argparse
import csv
import io
import json
import os
import random
import subprocess
import sys
from collections import Counter, defaultdict

try:
    import chess
except ImportError:
    sys.stderr.write("needs python-chess:  pip install chess\n")
    raise SystemExit(2)


# --- the six dimensions, from the Lichess themes ---------------------------
#
# The tables are *not* here. They are in assets/items/themes.json, because
# src/PuzzleFeed.cpp needs exactly the same answers for the puzzles the Lichess
# API hands the phone at run time. Two copies would drift apart silently, and
# the two halves of the same bank would then measure different things.

THEMES_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           os.pardir, "assets", "items", "themes.json")


class Themes(object):
    """assets/items/themes.json, loaded once."""

    def __init__(self, path):
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
        self.order = data["dimensionOrder"]
        self.by_dimension = dict((key, set(value))
                                 for key, value in data["dimensionThemes"].items())
        self.default = data["defaultDimension"]
        self.motifs = set(data["motifThemes"])
        self.quota = data["quota"]
        self.sorts = dict((key, set(value)) for key, value in data["sortThemes"].items())
        self.sentences = [(entry["theme"], entry["text"]) for entry in data["sentences"]]

    def dimension_of(self, themes):
        """Which of the six dimensions this puzzle measures (teacher.md §3.1)."""
        for key in self.order:
            if not themes & self.by_dimension[key]:
                continue
            # A quiet position with no motif in it is the STL question: what is
            # there to *do* here? A quiet move that executes a fork is a TAK
            # item that happens to be quiet — the two questions are separate,
            # and keeping them separate is what lets §6.3's quota be filled
            # across all six dimensions instead of piling every quiet position
            # into STL.
            if key == "STL" and (themes & self.motifs):
                continue
            return key
        return self.default

    def sort_of(self, themes):
        """Which of §6.3's three sorts this is. Quiet wins over defensive: a
        move that is both is more interesting as the quiet one, because that is
        the rarer sort and the one the spec calls the important design
        decision."""
        for key in ("quiet", "defensive"):
            if themes & self.sorts[key]:
                return key
        return "other"

    def sentence_for(self, themes):
        for key, sentence in self.sentences:
            if key in themes:
                return sentence
        return ""


THEMES = Themes(THEMES_JSON)
QUOTA_QUIET = THEMES.quota["quiet"]            # §6.3: >= 25 %
QUOTA_DEFENSIVE = THEMES.quota["defensive"]    # §6.3: >= 15 %

dimension_of = THEMES.dimension_of
sort_of = THEMES.sort_of
sentence_for = THEMES.sentence_for


# Piece values in pawns, for the fallback sentence below. The king is left out:
# it cannot be won, and a line that ends in mate gets its own sentence.
PIECE_VALUES = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3,
                chess.ROOK: 5, chess.QUEEN: 9}


def material(board, white):
    total = 0
    for piece_type, value in PIECE_VALUES.items():
        total += value * len(board.pieces(piece_type, white))
    return total


def computed_sentence(board, line):
    """A sentence for a puzzle whose themes say nothing we have words for.

    Everything here is *measured* on the board, never guessed: whether the line
    ends in mate, and what it wins. teacher.md §6.6 forbids a bare "falsch" and
    forbids a number as the answer; it does not permit inventing a reason, so
    this says only what can be checked.
    """
    learner = board.turn
    before = material(board, learner) - material(board, not learner)
    probe = board.copy()
    for move in line:
        probe.push_uci(move)
    if probe.is_checkmate():
        return "Am Ende steht das Matt."
    won = (material(probe, learner) - material(probe, not learner)) - before
    if won >= 9:
        return "Die Folge gewinnt die Dame."
    if won >= 5:
        return "Die Folge gewinnt einen Turm."
    if won >= 3:
        return "Die Folge gewinnt eine Leichtfigur."
    if won >= 1:
        return "Die Folge gewinnt einen Bauern."
    if won <= -1:
        # It gives material away and is still the best move — which is worth
        # saying, because it is the case a learner distrusts most.
        return "Die Folge gibt Material und ist trotzdem die stärkste."
    return "Die Folge hält die Stellung; ein anderer Zug gibt etwas her."


def trim_to_quota(items):
    """Drop general items until the quiet and defensive shares of §6.3 hold.
    Returns how many were dropped."""
    quiet = sum(1 for entry in items if entry["sort"] == "quiet")
    defensive = sum(1 for entry in items if entry["sort"] == "defensive")
    if not items:
        return 0
    allowed = int(min(quiet / QUOTA_QUIET, defensive / QUOTA_DEFENSIVE))
    excess = len(items) - allowed
    if excess <= 0:
        return 0

    by_cell = defaultdict(list)
    for index, entry in enumerate(items):
        if entry["sort"] == "other":
            by_cell[(entry["dimension"], int(entry["difficulty"]) // 100)].append(index)
    # Round-robin over the cells, always taking from a fullest one, so that no
    # band or dimension is emptied to keep another full.
    doomed = set()
    while excess > 0:
        cells_left = [key for key, rows in by_cell.items() if rows]
        if not cells_left:
            break
        cells_left.sort(key=lambda key: -len(by_cell[key]))
        for key in cells_left:
            if excess <= 0:
                break
            doomed.add(by_cell[key].pop())
            excess -= 1
    items[:] = [entry for index, entry in enumerate(items) if index not in doomed]
    return len(doomed)


def cell_room(per_cell, sort):
    """How many items of that sort a single (band, dimension) cell takes."""
    if sort == "quiet":
        return max(1, int(round(per_cell * QUOTA_QUIET)))
    if sort == "defensive":
        return max(1, int(round(per_cell * QUOTA_DEFENSIVE)))
    return per_cell - max(1, int(round(per_cell * QUOTA_QUIET))) \
                   - max(1, int(round(per_cell * QUOTA_DEFENSIVE)))


def open_rows(path):
    """The CSV, whether it is plain or zstd-compressed."""
    if path.endswith(".zst"):
        process = subprocess.Popen(["zstd", "-dc", path], stdout=subprocess.PIPE)
        stream = io.TextIOWrapper(process.stdout, encoding="utf-8")
    else:
        stream = open(path, encoding="utf-8")
    return csv.DictReader(stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True,
                        help="lichess_db_puzzle.csv or .csv.zst from database.lichess.org")
    parser.add_argument("--output", required=True, help="the JSON item bank to write")
    parser.add_argument("--exclude", action="append", default=[],
                        help="another item bank whose positions must not be repeated; "
                             "pass assets/items/placement.json, which was itself built "
                             "from Lichess puzzles and shares a few of them")
    # The filter of §4.2, verbatim. They are options so that a smaller or a
    # stricter bank can be built without editing the script.
    parser.add_argument("--min-popularity", type=int, default=80)
    parser.add_argument("--min-plays", type=int, default=500)
    parser.add_argument("--max-deviation", type=int, default=80)
    parser.add_argument("--min-rating", type=int, default=600)
    parser.add_argument("--max-rating", type=int, default=2200)
    parser.add_argument("--band", type=int, default=100, help="rating band width")
    parser.add_argument("--per-cell", type=int, default=60,
                        help="items per (rating band x dimension); 60 gives about 6000")
    parser.add_argument("--max-line", type=int, default=7,
                        help="longest solution in plies; §6.5's ladder tops out at four")
    parser.add_argument("--seed", type=int, default=20260917,
                        help="the draw is random but reproducible, so two builds agree")
    args = parser.parse_args()

    random.seed(args.seed)
    # Reservoir sampling per cell: one pass over six million rows, constant
    # memory, and every puzzle that passes the filter has the same chance.
    cells = defaultdict(list)
    seen_cell = Counter()
    kept_themes = Counter()
    read = passed = 0

    for row in open_rows(args.input):
        read += 1
        try:
            rating = int(row["Rating"])
            deviation = int(row["RatingDeviation"])
            popularity = int(row["Popularity"])
            plays = int(row["NbPlays"])
        except (KeyError, ValueError):
            continue
        if popularity < args.min_popularity or plays < args.min_plays:
            continue
        if deviation > args.max_deviation:
            continue
        if rating < args.min_rating or rating >= args.max_rating:
            continue

        moves = row["Moves"].split()
        # Moves[0] is the opponent's move *into* the position (§4.2). What is
        # left is the learner's line, and it has to start and end with them.
        if len(moves) < 2:
            continue
        line = moves[1:]
        if len(line) > args.max_line or len(line) % 2 == 0:
            continue

        themes = set(row.get("Themes", "").split())
        dimension = dimension_of(themes)
        sort = sort_of(themes)
        band = (rating // args.band) * args.band
        # Three reservoirs per cell, sized by §6.3's quota. Sampling uniformly
        # into one reservoir gives the *natural* mixture of the database, and
        # that mixture is 5 % quiet — which is the very thing §6.3 calls the
        # defect of every existing collection. The quota has to be reserved,
        # not hoped for.
        key = (band, dimension, sort)
        room = cell_room(args.per_cell, sort)
        if room == 0:
            continue

        seen_cell[key] += 1
        item = (row["PuzzleId"], row["FEN"], moves[0], line, rating, themes)
        bucket = cells[key]
        if len(bucket) < room:
            bucket.append(item)
        else:
            # Reservoir: replace with probability room / seen.
            index = random.randrange(seen_cell[key])
            if index < room:
                bucket[index] = item
        passed += 1

    items = []
    dropped_illegal = 0
    dropped_duplicate = 0
    # Two puzzles can share a position (different games, same place). Asking
    # the same position twice measures memory the second time, so the later one
    # goes — the same rule tools/verify_items.py applies to the written bank.
    positions = set()
    for path in args.exclude:
        with open(path, encoding="utf-8") as handle:
            loaded = json.load(handle)
        for entry in (loaded if isinstance(loaded, list) else loaded.get("items", [])):
            try:
                positions.add(chess.Board(entry["fen"]).epd())
            except (KeyError, ValueError):
                continue
    for (band, dimension, sort), bucket in sorted(cells.items()):
        for puzzle_id, fen, opening_move, line, rating, themes in bucket:
            # Apply the opponent's move and check the whole line while we are
            # at it. A bank entry that does not play through is worse than no
            # entry — tools/verify_items.py makes the same demand of the
            # hand-written bank.
            board = chess.Board(fen)
            try:
                board.push_uci(opening_move)
                probe = board.copy()
                for move in line:
                    probe.push_uci(move)
            except (ValueError, AssertionError):
                dropped_illegal += 1
                continue

            epd = board.epd()
            if epd in positions:
                dropped_duplicate += 1
                continue
            positions.add(epd)

            items.append({
                "id": "li-%s" % puzzle_id,
                "fen": board.fen(),
                "solution": line[0],
                "line": line,
                "alsoAccepted": [],
                "dimension": dimension,
                "difficulty": rating,
                "explanation": sentence_for(themes) or computed_sentence(board, line),
                "source": "lichess",
                # Kept so that the mixture of §6.3 can be checked in the
                # shipped file and not only in this script's output.
                "sort": sort,
                "themes": sorted(themes),
            })
            kept_themes.update(themes)

    # The quota is a floor on a *share*, so where a cell could not supply its
    # reserved quiet or defensive items the only honest way to reach it is to
    # carry fewer of the others. teacher.md §6.3 calls the mixture "die
    # wichtigste Entwurfsentscheidung"; a bank that misses it teaches "there is
    # always a combination here", which is the habit the whole section exists
    # to break. Coverage is the cheaper thing to give up, so general items go,
    # taken from the fullest cells first so the spread stays even.
    trimmed = trim_to_quota(items)

    items.sort(key=lambda entry: (entry["dimension"], entry["difficulty"], entry["id"]))
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump({"items": items}, handle, ensure_ascii=False, indent=1)
        handle.write("\n")

    # --- the summary, including what is *not* in the bank ------------------
    total = len(items)
    multi = sum(1 for entry in items if len(entry["line"]) > 1)
    quiet = sum(1 for entry in items if entry["sort"] == "quiet")
    defensive = sum(1 for entry in items if entry["sort"] == "defensive")
    per_dimension = Counter(entry["dimension"] for entry in items)

    print("read %d rows, %d passed the filter, wrote %d items to %s"
          % (read, passed, total, args.output))
    if trimmed:
        print("  %d allgemeine Aufgaben verworfen, damit die Quote aus §6.3 hält"
              % trimmed)
    if dropped_illegal:
        print("  %d dropped because the line did not play through" % dropped_illegal)
    if dropped_duplicate:
        print("  %d dropped as a repeat of a position already in the bank"
              % dropped_duplicate)
    if args.exclude:
        print("  positions excluded from: %s" % ", ".join(args.exclude))
    print("  per dimension: %s"
          % ", ".join("%s %d" % (k, per_dimension[k]) for k in sorted(per_dimension)))
    print("  more than one move: %d (%.0f %%)" % (multi, 100.0 * multi / max(total, 1)))
    print("")
    print("teacher.md §6.3 asks for a mixture, and this is what came out:")
    print("  stiller Zug      %5d (%4.1f %%, gefordert >= 25 %%)"
          % (quiet, 100.0 * quiet / max(total, 1)))
    print("  Verteidigungszug %5d (%4.1f %%, gefordert >= 15 %%)"
          % (defensive, 100.0 * defensive / max(total, 1)))
    print("  \"Hier ist nichts\"    0 ( 0.0 %, gefordert >= 10 %)")
    print("")
    print("The last line is not a bug in this script and cannot be fixed in it:")
    print("a Lichess puzzle has a winning line by construction, so the sort of")
    print("position where *nothing* wins has to be generated against the engine")
    print("from the Lichess evaluation database (§6.3, \"Erzeugung\"). Until that")
    print("exists, the bank teaches \"there is always something here\" — which is")
    print("the assumption §6.3 was written to destroy.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
