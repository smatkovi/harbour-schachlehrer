/*
    Copyright (C) 2026 smatkovi

    This file is part of harbour-schachlehrer.

    harbour-schachlehrer is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    harbour-schachlehrer is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with harbour-schachlehrer. If not, see <https://www.gnu.org/licenses/>.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
// The UCI protocol and the win-probability conversion. Both are pure
// functions, which is the point: the phone may have no engine binary at all
// (missing file, Sailjail), and these still have to be right.
#include "core/Uci.h"
#include "core/WinProb.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace schach::core;
using namespace schach::core::uciproto;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        if (++failures > 20)
            std::exit(1);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

} // namespace

int main()
{
    // --- commands -----------------------------------------------------------
    CHECK(cmdUci() == "uci");
    CHECK(cmdIsReady() == "isready");
    CHECK(cmdSetOption("Hash", 16) == "setoption name Hash value 16");
    CHECK(cmdSetOption("SyzygyPath", "/usr/share/harbour-schachlehrer/syzygy")
          == "setoption name SyzygyPath value /usr/share/harbour-schachlehrer/syzygy");
    CHECK(cmdSetOption("Ponder", std::string()) == "setoption name Ponder");
    CHECK(cmdPosition(std::string(), std::vector<std::string>()) == "position startpos");
    {
        std::vector<std::string> moves;
        moves.push_back("e2e4");
        moves.push_back("e7e5");
        CHECK(cmdPosition(std::string(), moves) == "position startpos moves e2e4 e7e5");
        CHECK(cmdPosition("8/8/8/8/8/8/8/K6k w - - 0 1", moves)
              == "position fen 8/8/8/8/8/8/8/K6k w - - 0 1 moves e2e4 e7e5");
    }
    // platform.md §1: never `go infinite`; a fixed node count so that the
    // diagnosis gives the same answer on every device (teacher.md §2.2).
    CHECK(cmdGoNodes(1500000) == "go nodes 1500000");
    CHECK(cmdGoMovetime(250) == "go movetime 250");
    CHECK(cmdGoMovetime(0) == "go movetime 1");
    CHECK(cmdGoDepth(18) == "go depth 18");

    // --- line classification ------------------------------------------------
    CHECK(classify("uciok") == LineKind::UciOk);
    CHECK(classify("readyok") == LineKind::ReadyOk);
    CHECK(classify("id name Stockfish 17.1") == LineKind::Id);
    CHECK(classify("option name Hash type spin default 16") == LineKind::Option);
    CHECK(classify("info depth 1 score cp 13") == LineKind::Info);
    CHECK(classify("bestmove e2e4") == LineKind::BestMove);
    CHECK(classify("") == LineKind::Unknown);
    CHECK(classify("Stockfish 17.1 by the Stockfish developers") == LineKind::Unknown);

    {
        std::string key, value;
        CHECK(parseId("id name Stockfish 17.1", key, value));
        CHECK(key == "name");
        CHECK(value == "Stockfish 17.1");
        CHECK(parseId("id author the Stockfish developers (see AUTHORS file)", key, value));
        CHECK(key == "author");
    }

    // --- info lines ---------------------------------------------------------
    {
        Info info;
        CHECK(parseInfo("info depth 20 seldepth 29 multipv 1 score cp 34 nodes 1500023 nps 1420000 "
                        "hashfull 210 tbhits 0 time 1056 pv e2e4 e7e5 g1f3 b8c6",
                        info));
        CHECK(info.hasDepth && info.depth == 20);
        CHECK(info.seldepth == 29);
        CHECK(info.multipv == 1);
        CHECK(info.score.valid && !info.score.isMate && info.score.cp == 34);
        CHECK(info.nodes == 1500023);
        CHECK(info.nps == 1420000);
        CHECK(info.timeMs == 1056);
        CHECK(info.hashfull == 210);
        CHECK(info.pv.size() == 4);
        CHECK(info.bestMove() == "e2e4");
        CHECK(info.pv.back() == "b8c6");
    }
    {
        Info info;
        CHECK(parseInfo("info depth 14 multipv 2 score mate -3 pv h7h8q g8h8", info));
        CHECK(info.multipv == 2);
        CHECK(info.score.isMate && info.score.mateIn == -3);
        CHECK(info.pv.size() == 2);
    }
    {
        Info info;
        CHECK(parseInfo("info depth 9 score cp -120 upperbound nodes 5000 pv d2d4", info));
        CHECK(info.upperbound && !info.lowerbound);
        CHECK(info.score.cp == -120);
    }
    {
        // `info string` is engine chatter and carries no score: it must not be
        // half-parsed into one.
        Info info;
        CHECK(!parseInfo("info string NNUE evaluation using nn-37f18f62d772.nnue", info));
        CHECK(!parseInfo("bestmove e2e4", info));
        CHECK(!parseInfo("", info));
    }
    {
        // A truncated line must not read past its end.
        Info info;
        CHECK(!parseInfo("info score", info));
        CHECK(parseInfo("info depth 3 score cp", info));   // depth survives, score does not
        CHECK(!info.score.valid);
    }
    {
        Info info;
        CHECK(parseInfo("info currmove b1c3 currmovenumber 2 depth 12", info));
        CHECK(info.currmove == "b1c3");
        CHECK(info.currmovenumber == 2);
        CHECK(info.pv.empty());
    }

    // --- bestmove -----------------------------------------------------------
    {
        BestMove best;
        CHECK(parseBestMove("bestmove e2e4 ponder e7e5", best));
        CHECK(best.move == "e2e4");
        CHECK(best.ponder == "e7e5");
        CHECK(parseBestMove("bestmove a7a8q", best));
        CHECK(best.move == "a7a8q");
        CHECK(best.ponder.empty());
        CHECK(parseBestMove("bestmove (none)", best));
        CHECK(best.move == "(none)");
        CHECK(!parseBestMove("bestmove", best));
        CHECK(!parseBestMove("info depth 1", best));
    }

    // --- teacher.md §0.5, the numbers of appendix C -------------------------
    CHECK(near(winProbability(0), 50.0, 1e-9));
    CHECK(near(winningChances(0), 0.0, 1e-9));
    // +9.00 -> +6.00 is 6.4 pp: an inaccuracy, and deliberately not a blunder.
    {
        const double drop = winProbability(900) - winProbability(600);
        CHECK(near(drop, 6.4, 0.15));
        CHECK(severityOf(drop) == Severity::Inaccuracy);
    }
    // +10.00 -> +7.00 is 4.6 pp: not even an inaccuracy.
    {
        const double drop = winProbability(1000) - winProbability(700);
        CHECK(near(drop, 4.6, 0.15));
        CHECK(severityOf(drop) == Severity::Clean);
    }
    // +6.00 -> +3.00 is exactly 15.0 pp.
    {
        const double drop = winProbability(600) - winProbability(300);
        CHECK(near(drop, 15.0, 0.15));
    }
    // +0.30 -> -2.70, a minor piece dropped in a level position: 25.8 pp, four
    // times as heavy as three pawns in a won position. That weighting falls out
    // of the formula without a special rule.
    {
        const double drop = winProbability(30) - winProbability(-270);
        CHECK(near(drop, 25.8, 0.15));
        CHECK(severityOf(drop) == Severity::Blunder);
    }
    // The corrected thresholds of §2.3.1: the blunder line is 18 pp, not 15.
    CHECK(severityOf(4.9) == Severity::Clean);
    CHECK(severityOf(5.0) == Severity::Inaccuracy);
    CHECK(severityOf(9.99) == Severity::Inaccuracy);
    CHECK(severityOf(10.0) == Severity::Mistake);
    CHECK(severityOf(17.9) == Severity::Mistake);
    CHECK(severityOf(18.0) == Severity::Blunder);

    // A mate saturates the scale; a slower mate instead of a faster one is
    // never an error (§0.5).
    {
        const Score mateIn2 = Score::mate(2);
        const Score mateIn8 = Score::mate(8);
        CHECK(near(winProbability(mateIn2), 100.0, 1e-9));
        CHECK(near(deltaW(mateIn2, mateIn8), 0.0, 1e-9));
        CHECK(deltaW(Score::centipawns(0), Score::mate(-1)) > 49.0);
    }

    // The learning threshold rises as the player gets weaker (§2.3.2) — the
    // counter-intuitive table that the whole workload management rests on.
    CHECK(learningThreshold(900) == 28.0);
    CHECK(learningThreshold(1100) == 24.0);
    CHECK(learningThreshold(1450) == 20.0);
    CHECK(learningThreshold(1700) == 15.0);
    CHECK(learningThreshold(2100) == 10.0);
    CHECK(maxCardsPerGame(900) == 3);
    CHECK(maxCardsPerGame(2100) == 6);
    CHECK(learningThreshold(900) > learningThreshold(2100));
    // Dynamic readjustment, bounds included.
    CHECK(near(adjustThreshold(24.0, 6.0, 4), 26.0, 1e-9));
    CHECK(near(adjustThreshold(24.0, 0.5, 4), 22.0, 1e-9));
    CHECK(near(adjustThreshold(24.0, 2.0, 4), 24.0, 1e-9));
    CHECK(near(adjustThreshold(34.0, 9.0, 3), 35.0, 1e-9));
    CHECK(near(adjustThreshold(9.0, 0.0, 6), 8.0, 1e-9));

    std::printf("OK: UCI protocol and win probability\n");
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    return 0;
}
