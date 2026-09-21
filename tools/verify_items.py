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
"""Verify the placement-test item bank against a real engine.

The bank (assets/items/placement.json) is data, and data that nobody checks
rots.  This script is the check: it drives Stockfish over UCI and refuses to
let an item into the bank unless the engine agrees that the item has exactly
one answer.

    tools/verify_items.py --engine ~/bin/stockfish-host \
                          --engine-dir ~/schach-build/Stockfish-sf_17.1/src

Needs python-chess (`pip install chess`) and a UCI engine binary.  Exits
non-zero if any item fails, so it can be wired into CI.

What is checked, item by item
-----------------------------
1.  The FEN is legal, the position is not over, and the side to move really
    has a choice (at least two legal moves).
2.  `solution` is a legal move in that position.
3.  With MultiPV 3 the solution is the engine's *best* move, and the gap to
    the best move that is not accepted is wide enough that the item has one
    answer.  Moves within a hair of the best one are collected into
    `alsoAccepted`; if three moves are that close the item is rejected.
4.  No duplicate positions, no duplicate ids, and the `line` of a multi-move
    item plays through and ends on the learner's move (teacher.md §6.5).
5.  The claimed dimension is plausible for the position (see DIMENSION_CHECKS).
6.  At least a quarter of the bank is quiet: best move neither capture nor
    check nor promotion, and not an immediate threat to win material either
    (teacher.md §6.3 — a bank of nothing but combinations teaches "there is
    always a tactic here").

The uniqueness bar
------------------
teacher.md §4.2 quotes the bar that Lichess' own generator uses: the best move
must beat the second best by 0.7 on the `winningChances` scale (= 35
percentage points of win probability).  That bar is stated there as the reason
Lichess puzzles are unique, and the spec itself calls it "far stricter than any
error threshold".  It is right for forced combinations and impossible for the
quiet positional items that §6.3 makes mandatory, so this script uses the
spec's own error ladder (§0.5, §2.3.1) instead, one rung per kind of item:

  * forcing items -- gap >= 0.36 wc (18 pp), the app's own blunder line
    (src/core/WinProb.h), and additionally >= 150 cp.  Playing the second-best
    move must cost at least a blunder.
  * quiet items -- gap >= 0.20 wc (10 pp), the Lichess "mistake" line, and
    additionally >= 80 cp.  A quiet position cannot swing by a blunder's worth
    and still be quiet; demanding a mistake's worth is the honest bar.

Both are choices of this script, not numbers taken from the spec, and they are
printed in the summary so a reader can see what was applied.  The share of the
bank that also clears the strict Lichess 0.7 bar is reported as well.
"""

import argparse
import json
import math
import os
import sys
from collections import defaultdict

try:
    import chess
    import chess.engine
except ImportError:  # pragma: no cover
    sys.exit("verify_items.py needs python-chess: pip install chess")

# teacher.md §0.5 -- the Lichess winning-chances regression constant.  The same
# number sits in src/core/WinProb.h; do not "improve" it in one place only.
WIN_PROB_K = -0.00368208

# The two bars, in winning chances and in centipawns.  See the module docstring.
GAP_FORCING_WC = 0.36
GAP_FORCING_CP = 150
GAP_QUIET_WC = 0.20
GAP_QUIET_CP = 80
# The Lichess puzzle generator's own bar, reported for information only.
GAP_LICHESS_WC = 0.70

# Two moves this close to each other are the same answer as far as the learner
# is concerned, so the second one is accepted instead of punished.
ACCEPT_WC = 0.04
ACCEPT_CP = 25

# A quiet move must not threaten to win a piece outright either.
THREAT_CP = 200

DIMENSIONS = ("TAK", "SRG", "REC", "END", "STL", "ERD")

# Difficulty bands, ~150 Elo wide, covering the range the app addresses.
# teacher.md §4.2 stratifies the bank over 600 to 2200; the hand-written items
# only ever reached 720..2030, and the bounds were written from them. The
# imported Lichess items use the whole range, so the range is the spec's now.
BAND_LO, BAND_HI, BAND_WIDTH = 600, 2200, 200

PIECE_VALUE = {chess.PAWN: 1, chess.KNIGHT: 3, chess.BISHOP: 3,
               chess.ROOK: 5, chess.QUEEN: 9}

# Centipawn values for the static exchange evaluation below.  python-chess has
# no SEE of its own, so this is the classic swap-off algorithm, written out.
SEE_VALUE = {chess.PAWN: 100, chess.KNIGHT: 320, chess.BISHOP: 330,
             chess.ROOK: 500, chess.QUEEN: 900, chess.KING: 20000}


def static_exchange(board, move):
    """What a capture on `move.to_square` wins, after both sides have taken
    everything they can, in centipawns.  Legal-move generation is used at every
    step, so pins and discovered checks are respected; promotions inside the
    exchange are ignored, which is good enough for a threat test."""
    target = move.to_square
    if board.is_en_passant(move):
        gain = [SEE_VALUE[chess.PAWN]]
    else:
        victim = board.piece_at(target)
        gain = [SEE_VALUE[victim.piece_type] if victim else 0]
    probe = board.copy(stack=False)
    probe.push(move)
    depth = 0
    while True:
        standing = probe.piece_at(target)
        if standing is None:
            break
        recaptures = [m for m in probe.legal_moves if m.to_square == target]
        if not recaptures:
            break
        cheapest = min(recaptures,
                       key=lambda m: SEE_VALUE[probe.piece_at(m.from_square).piece_type])
        depth += 1
        gain.append(SEE_VALUE[standing.piece_type] - gain[depth - 1])
        probe.push(cheapest)
    for index in range(len(gain) - 1, 0, -1):
        gain[index - 1] = -max(-gain[index - 1], gain[index])
    return gain[0]


def see_ge(board, move, threshold):
    return static_exchange(board, move) >= threshold


def band_of(difficulty):
    index = int((difficulty - BAND_LO) // BAND_WIDTH)
    index = max(0, min(index, (BAND_HI - BAND_LO) // BAND_WIDTH - 1))
    lo = BAND_LO + index * BAND_WIDTH
    return index, "%d-%d" % (lo, lo + BAND_WIDTH)


def winning_chances(centipawns):
    """wc(cp) on [-1, +1] -- teacher.md §0.5."""
    return 2.0 / (1.0 + math.exp(WIN_PROB_K * centipawns)) - 1.0


def score_pair(pov_score):
    """(centipawns, winning chances) of an engine score, mate saturated."""
    if pov_score.is_mate():
        mate = pov_score.mate()
        return (100000 - abs(mate)) * (1 if mate > 0 else -1), (1.0 if mate > 0 else -1.0)
    centipawns = pov_score.score()
    return centipawns, winning_chances(centipawns)


# --------------------------------------------------------------------------
# position properties, all engine-free
# --------------------------------------------------------------------------

def non_king_pieces(board):
    return sum(1 for square in chess.SQUARES
               if board.piece_at(square) and board.piece_at(square).piece_type != chess.KING)


def heavy_material(board):
    """Material of both sides without kings and pawns, in pawns."""
    total = 0
    for square in chess.SQUARES:
        piece = board.piece_at(square)
        if piece and piece.piece_type not in (chess.KING, chess.PAWN):
            total += PIECE_VALUE[piece.piece_type]
    return total


def is_endgame(board):
    """An endgame by material count, the way teacher.md §8.3 uses the word."""
    return non_king_pieces(board) <= 10 and heavy_material(board) <= 16


def threatens_material(board, threshold=THREAT_CP):
    """Does the side to move threaten to win >= threshold by a capture?"""
    if board.is_check():
        return True
    for move in board.legal_moves:
        if board.is_capture(move) and see_ge(board, move, threshold):
            return True
        after = board.copy(stack=False)
        after.push(move)
        if after.is_checkmate():
            return True
    return False


def standing_threat(board):
    """Would the opponent win material (or mate) if we simply passed?"""
    if board.is_check():
        return True
    passed = board.copy(stack=False)
    passed.push(chess.Move.null())
    return threatens_material(passed)


def is_quiet_move(board, move):
    """teacher.md §6.3 / Lichess `quietMove`: no check, no capture, no
    promotion, and no immediate threat to capture something either."""
    if board.is_capture(move) or move.promotion:
        return False
    after = board.copy(stack=False)
    after.push(move)
    if after.is_check():
        return False
    passed = after.copy(stack=False)
    passed.push(chess.Move.null())      # legal: we just showed it is not check
    for reply in passed.legal_moves:
        if passed.is_capture(reply) and see_ge(passed, reply, THREAT_CP):
            return False
        probe = passed.copy(stack=False)
        probe.push(reply)
        if probe.is_checkmate():
            return False
    return True


def pv_is_forcing_sequence(board, pv, plies=4, needed=2):
    """Do the first `plies` half-moves of the PV contain `needed` checks or
    captures?  That is what makes a REC item a calculation item."""
    probe = board.copy(stack=False)
    forcing = 0
    for move in pv[:plies]:
        if move not in probe.legal_moves:
            break
        capture = probe.is_capture(move)
        probe.push(move)
        if capture or probe.is_check():
            forcing += 1
    return forcing >= needed


# --------------------------------------------------------------------------
# per-dimension plausibility (check 5)
# --------------------------------------------------------------------------

def check_dimension(dimension, board, best_move, pv, quiet, forcing_gap):
    """Return a complaint string, or None when the dimension is plausible."""
    if dimension == "END":
        if not is_endgame(board):
            return ("claims END but is not an endgame by material "
                    "(%d pieces, %d heavy material)"
                    % (non_king_pieces(board), heavy_material(board)))
    elif dimension == "TAK":
        if is_endgame(board):
            return "claims TAK but the position is an endgame -- use END"
        if quiet:
            return "claims TAK but the best move is quiet -- use STL"
    elif dimension == "STL":
        if not quiet:
            return "claims STL but the best move is a capture, a check or an immediate threat"
    elif dimension == "REC":
        if quiet:
            return "claims REC but the best move is quiet -- nothing to calculate"
        if len(pv) < 5:
            return "claims REC but the engine's line is only %d plies long" % len(pv)
        if not pv_is_forcing_sequence(board, pv):
            return "claims REC but the line is not forcing in its first moves"
    elif dimension == "SRG":
        if not standing_threat(board):
            return "claims SRG but nothing is hanging or threatened -- there is nothing to overlook"
    elif dimension == "ERD":
        if board.fullmove_number > 14:
            return "claims ERD but the game is already at move %d" % board.fullmove_number
        if non_king_pieces(board) < 22:
            return ("claims ERD but only %d pieces are left -- that is no longer the opening"
                    % non_king_pieces(board))
    return None


# --------------------------------------------------------------------------
# the engine pass (checks 1-3)
# --------------------------------------------------------------------------

def analyse(engine, board, movetime, depth, multipv=3):
    limit = chess.engine.Limit(time=movetime / 1000.0) if depth is None \
        else chess.engine.Limit(depth=depth)
    infos = engine.analyse(board, limit, multipv=multipv)
    lines = []
    for info in infos:
        pv = info.get("pv") or []
        if not pv:
            continue
        centipawns, wc = score_pair(info["score"].pov(board.turn))
        lines.append({"move": pv[0], "cp": centipawns, "wc": wc, "pv": pv})
    return lines


def verify_item(engine, item, args):
    """Returns (errors, info).  `info` carries what the summary needs."""
    errors = []
    info = {"quiet": False, "gap_wc": 0.0, "gap_cp": 0, "accepted": [],
            "lichess_bar": False}

    fen = item.get("fen", "")
    try:
        board = chess.Board(fen)
    except ValueError as exc:
        return ["illegal FEN: %s" % exc], info
    if not board.is_valid():
        return ["illegal position: %s" % board.status()], info
    if board.is_game_over():
        return ["the position is already over -- nothing to ask"], info
    if len(list(board.legal_moves)) < 2:
        return ["only one legal move -- the side to move has no task"], info

    try:
        solution = chess.Move.from_uci(item.get("solution", ""))
    except ValueError:
        return ["solution %r is not a UCI move" % item.get("solution")], info
    if solution not in board.legal_moves:
        return ["solution %s is not legal here" % solution.uci()], info

    lines = analyse(engine, board, args.movetime, args.depth)
    if len(lines) < 2:
        return ["engine returned fewer than two lines"], info

    if lines[0]["move"] != solution:
        errors.append("engine prefers %s (%+d cp) over the claimed solution %s (%s)"
                      % (lines[0]["move"].uci(), lines[0]["cp"], solution.uci(),
                         next(("%+d cp" % l["cp"] for l in lines if l["move"] == solution),
                              "not in the top 3")))
        return errors, info

    # Moves within a hair of the best one are the same answer.
    accepted = [lines[0]]
    for line in lines[1:]:
        if (abs(lines[0]["wc"] - line["wc"]) <= ACCEPT_WC
                and abs(lines[0]["cp"] - line["cp"]) <= ACCEPT_CP):
            accepted.append(line)
        else:
            break
    if len(accepted) >= 3:
        errors.append("three moves within %.2f wc of each other -- the item has no single answer"
                      % ACCEPT_WC)
        return errors, info

    rest = lines[len(accepted):]
    if not rest:
        errors.append("engine found no third line -- cannot measure the gap")
        return errors, info
    gap_wc = accepted[-1]["wc"] - rest[0]["wc"]
    gap_cp = accepted[-1]["cp"] - rest[0]["cp"]

    quiet = is_quiet_move(board, solution)
    bar_wc, bar_cp = (GAP_QUIET_WC, GAP_QUIET_CP) if quiet else (GAP_FORCING_WC, GAP_FORCING_CP)
    if gap_wc < bar_wc or gap_cp < bar_cp:
        errors.append("gap to %s is only %.2f wc / %d cp, the %s bar is %.2f wc / %d cp"
                      % (rest[0]["move"].uci(), gap_wc, gap_cp,
                         "quiet" if quiet else "forcing", bar_wc, bar_cp))

    info["quiet"] = quiet
    info["gap_wc"] = gap_wc
    info["gap_cp"] = gap_cp
    info["lichess_bar"] = gap_wc >= GAP_LICHESS_WC
    info["accepted"] = [line["move"].uci() for line in accepted[1:]]
    info["best_cp"] = lines[0]["cp"]

    declared = list(item.get("alsoAccepted", []))
    if args.write_accepted:
        item["alsoAccepted"] = info["accepted"]
    elif sorted(declared) != sorted(info["accepted"]):
        errors.append("alsoAccepted is %s but the engine says %s"
                      % (declared or "[]", info["accepted"] or "[]"))

    complaint = check_dimension(item.get("dimension", ""), board, solution,
                                lines[0]["pv"], quiet, gap_wc)
    if complaint:
        errors.append(complaint)
    return errors, info


# --------------------------------------------------------------------------

def structural_checks(items):
    """Checks 4 and the plain field hygiene, before the engine is started."""
    problems = []
    seen_ids, seen_positions = {}, {}
    for index, item in enumerate(items):
        where = item.get("id") or "#%d" % index
        for field in ("id", "fen", "solution", "dimension", "explanation"):
            if not item.get(field):
                problems.append("%s: field %r is missing or empty" % (where, field))
        if item.get("dimension") not in DIMENSIONS:
            problems.append("%s: dimension %r is not one of %s"
                            % (where, item.get("dimension"), ", ".join(DIMENSIONS)))
        difficulty = item.get("difficulty")
        if not isinstance(difficulty, (int, float)):
            problems.append("%s: difficulty is not a number" % where)
        elif not BAND_LO <= difficulty <= BAND_HI:
            problems.append("%s: difficulty %s is outside %d..%d"
                            % (where, difficulty, BAND_LO, BAND_HI))
        explanation = item.get("explanation", "")
        if explanation and not explanation.rstrip().endswith((".", "!", "?")):
            problems.append("%s: explanation is not a sentence" % where)
        item_id = item.get("id")
        if item_id in seen_ids:
            problems.append("%s: duplicate id, first seen at index %d" % (where, seen_ids[item_id]))
        seen_ids[item_id] = index
        try:
            board = chess.Board(item.get("fen", ""))
        except ValueError:
            continue
        key = board.board_fen() + " " + ("w" if board.turn else "b") + " " \
            + board.castling_xfen() + " " + (chess.square_name(board.ep_square)
                                             if board.ep_square else "-")
        if key in seen_positions:
            problems.append("%s: duplicate position, same as %s" % (where, seen_positions[key]))
        seen_positions[key] = where

        # The line (teacher.md §6.5). A one-move item has one entry; anything
        # longer has to alternate learner, opponent, learner and end on the
        # learner, or the last thing they do is watch. And it has to play
        # through: a bank entry that does not is worse than no entry.
        line = item.get("line") or [item.get("solution")]
        if line[0] != item.get("solution"):
            problems.append("%s: line starts with %s but solution says %s"
                            % (where, line[0], item.get("solution")))
        if len(line) % 2 == 0:
            problems.append("%s: line has %d plies and must end on the learner's move"
                            % (where, len(line)))
        probe = board.copy()
        for ply, move in enumerate(line):
            try:
                probe.push_uci(move)
            except (ValueError, AssertionError):
                problems.append("%s: move %d of the line (%s) is not legal there"
                                % (where, ply + 1, move))
                break
    return problems


def print_summary(items, infos, args):
    band_count = (BAND_HI - BAND_LO) // BAND_WIDTH
    names = [band_of(BAND_LO + i * BAND_WIDTH + 1)[1] for i in range(band_count)]
    grid = defaultdict(int)
    for item in items:
        grid[(item["dimension"], band_of(item["difficulty"])[0])] += 1

    width = max(len(n) for n in names) + 1
    print()
    print("items per dimension and difficulty band")
    print("-" * (6 + band_count * width + 7))
    print("%-5s" % "dim" + "".join("%*s" % (width, n) for n in names) + "%7s" % "total")
    for dimension in DIMENSIONS:
        row = [grid[(dimension, i)] for i in range(band_count)]
        print("%-5s" % dimension
              + "".join("%*s" % (width, value or ".") for value in row)
              + "%7d" % sum(row))
    totals = [sum(grid[(d, i)] for d in DIMENSIONS) for i in range(band_count)]
    print("%-5s" % "all" + "".join("%*d" % (width, value) for value in totals)
          + "%7d" % sum(totals))
    print()

    quiet = sum(1 for i in infos if i["quiet"])
    strict = sum(1 for i in infos if i["lichess_bar"])
    total = len(infos)
    print("quiet items          %3d of %d  (%.1f %%, required >= 25 %%)"
          % (quiet, total, 100.0 * quiet / max(total, 1)))
    print("clear the Lichess 0.70 wc bar as well  %3d of %d  (%.1f %%)"
          % (strict, total, 100.0 * strict / max(total, 1)))
    if infos:
        gaps = sorted(i["gap_wc"] for i in infos)
        print("gap to the next move  min %.2f wc   median %.2f wc"
              % (gaps[0], gaps[len(gaps) // 2]))
    print("bars applied          forcing >= %.2f wc / %d cp,  quiet >= %.2f wc / %d cp"
          % (GAP_FORCING_WC, GAP_FORCING_CP, GAP_QUIET_WC, GAP_QUIET_CP))
    print("engine                %s, MultiPV 3, %s, Threads %d, Hash %d MB"
          % (os.path.basename(args.engine),
             "depth %d" % args.depth if args.depth else "movetime %d ms" % args.movetime,
             args.threads, args.hash))
    return quiet, total


def main():
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--items", default=os.path.join(here, "assets", "items", "placement.json"))
    parser.add_argument("--engine", default=os.environ.get("STOCKFISH", "stockfish"))
    parser.add_argument("--engine-dir", default=None,
                        help="working directory for the engine, so it finds its own NNUE file")
    parser.add_argument("--eval-file", default=None, help="value for setoption name EvalFile")
    parser.add_argument("--movetime", type=int, default=2000)
    parser.add_argument("--depth", type=int, default=None,
                        help="use a fixed depth instead of a movetime")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--hash", type=int, default=256)
    parser.add_argument("--min-items", type=int, default=90)
    parser.add_argument("--min-quiet", type=float, default=0.25)
    parser.add_argument("--write-accepted", action="store_true",
                        help="rewrite alsoAccepted from what the engine found, then save")
    parser.add_argument("--only", default=None, help="verify one item id (for a quick re-check)")
    args = parser.parse_args()

    with open(args.items, encoding="utf-8") as handle:
        document = json.load(handle)
    items = document["items"] if isinstance(document, dict) else document
    if args.only:
        items = [i for i in items if i.get("id") == args.only]

    failures = []
    for problem in structural_checks(items):
        failures.append(problem)
        print("FAIL %s" % problem)

    # MultiPV is passed per analysis call; python-chess manages the option itself.
    options = {"Threads": args.threads, "Hash": args.hash}
    if args.eval_file:
        options["EvalFile"] = args.eval_file
    engine = chess.engine.SimpleEngine.popen_uci(
        args.engine, cwd=args.engine_dir or os.path.dirname(os.path.abspath(args.engine)))
    infos = []
    try:
        engine.configure(options)
        for index, item in enumerate(items, 1):
            errors, info = verify_item(engine, item, args)
            infos.append(info)
            mark = "ok  " if not errors else "FAIL"
            print("%s %3d/%d %-22s %-3s %4d  gap %5.2f wc %6d cp %s"
                  % (mark, index, len(items), item.get("id", "?"),
                     item.get("dimension", "?"), item.get("difficulty", 0),
                     info["gap_wc"], info["gap_cp"], "quiet" if info["quiet"] else ""),
                  flush=True)
            for error in errors:
                print("       %s" % error, flush=True)
                failures.append("%s: %s" % (item.get("id", "?"), error))
    finally:
        engine.quit()

    if args.write_accepted:
        for item, info in zip(items, infos):
            item["alsoAccepted"] = info["accepted"]
        with open(args.items, "w", encoding="utf-8") as handle:
            json.dump(items, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        print("\nalsoAccepted rewritten into %s" % args.items)

    quiet, total = print_summary(items, infos, args)
    if not args.only:
        if total < args.min_items:
            failures.append("only %d items, the bank needs at least %d" % (total, args.min_items))
        if total and quiet / total < args.min_quiet:
            failures.append("only %.1f %% quiet items, teacher.md §6.3 asks for at least %.0f %%"
                            % (100.0 * quiet / total, 100.0 * args.min_quiet))
    print()
    if failures:
        print("%d problem(s):" % len(failures))
        for failure in failures[:40]:
            print("  - %s" % failure)
        return 1
    print("all %d items verified." % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
