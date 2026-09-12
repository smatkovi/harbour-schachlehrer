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
// Move generation: the standard perft suite on the six classic positions to
// depth 5, plus the parts of Position that the rest of the core stands on —
// SAN, PGN, phase, material, SEE and the hanging-piece list.
//
// The reference numbers are the published ones (Chess Programming Wiki,
// "Perft Results"). They are not ours to choose: if a single one of them is
// off, everything above it — taxonomy, cards, sparring — is measuring noise.
#include "core/Position.h"

#include <chess.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace schach::core;

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

std::uint64_t perft(chess::Board& board, int depth)
{
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    if (depth == 1)
        return moves.size();

    std::uint64_t nodes = 0;
    for (const chess::Move& move : moves) {
        board.makeMove(move);
        nodes += perft(board, depth - 1);
        board.unmakeMove(move);
    }
    return nodes;
}

struct PerftCase {
    const char* name;
    const char* fen;
    std::uint64_t nodes[5];   // depth 1 … 5
};

const PerftCase kCases[] = {
    { "Grundstellung",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      { 20, 400, 8902, 197281, 4865609 } },
    { "Kiwipete",
      "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
      { 48, 2039, 97862, 4085603, 193690690 } },
    { "Position 3",
      "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
      { 14, 191, 2812, 43238, 674624 } },
    { "Position 4",
      "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
      { 6, 264, 9467, 422333, 15833292 } },
    { "Position 5",
      "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
      { 44, 1486, 62379, 2103487, 89941194 } },
    { "Position 6",
      "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
      { 46, 2079, 89890, 3894594, 164075551 } },
};

} // namespace

int main()
{
    std::uint64_t total = 0;

    for (const PerftCase& item : kCases) {
        chess::Board board(item.fen);
        for (int depth = 1; depth <= 5; ++depth) {
            const std::uint64_t nodes = perft(board, depth);
            const std::uint64_t want = item.nodes[depth - 1];
            total += nodes;
            if (nodes != want) {
                std::fprintf(stderr, "FAILED %s depth %d: %llu, expected %llu\n", item.name, depth,
                             static_cast<unsigned long long>(nodes),
                             static_cast<unsigned long long>(want));
                ++failures;
            } else {
                std::printf("  %-14s d%d %12llu ok\n", item.name, depth,
                            static_cast<unsigned long long>(nodes));
            }
        }
    }

    // --- the shell around the library ---------------------------------------
    {
        Position position;
        CHECK(position.whiteToMove());
        CHECK(position.phase() == Phase::Opening);
        CHECK(position.materialBalance() == 0);
        CHECK(position.officers() == 14);          // kings do not count (teacher.md §0.4)
        CHECK(position.legalMoves().size() == 20);
        CHECK(position.play("e2e4"));
        CHECK(position.lastMoveSan() == "e4");
        CHECK(!position.whiteToMove());
        CHECK(position.playSan("e5"));
        CHECK(position.playSan("Nf3"));
        CHECK(position.sanHistory().size() == 3);
        CHECK(position.undo());
        CHECK(position.sanHistory().size() == 2);
        CHECK(position.lastMoveSan() == "e5");
        CHECK(position.sanOf("g1f3") == "Nf3");
        CHECK(position.uciOf("Nf3") == "g1f3");
        CHECK(position.sanOf("g1g3").empty());     // illegal
    }

    // Castling has to reach the UI as "king to g1", not as "king takes rook".
    {
        Position position("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
        const std::vector<int> targets = position.legalTargets(squareFromName("e1"));
        bool sawG1 = false, sawC1 = false;
        for (int target : targets) {
            sawG1 = sawG1 || target == squareFromName("g1");
            sawC1 = sawC1 || target == squareFromName("c1");
        }
        CHECK(sawG1);
        CHECK(sawC1);
        CHECK(position.play("e1g1"));
        CHECK(position.pieceAt(squareFromName("g1")) == 'K');
        CHECK(position.pieceAt(squareFromName("f1")) == 'R');
    }

    // Phase boundaries of teacher.md §0.4.
    {
        Position endgame("8/8/4k3/8/8/3K4/4P3/8 w - - 0 1");
        CHECK(endgame.phase() == Phase::Endgame);
        CHECK(endgame.pieceCount() == 3);
        Position middlegame("r2q1rk1/pp2ppbp/2n3p1/8/3PP3/2N2N2/PP3PPP/R2Q1RK1 w - - 0 12");
        CHECK(middlegame.phase() == Phase::Middlegame);
    }

    // SEE and the hanging list, the two primitives of teacher.md §2.2.
    {
        // The knight on d4 is attacked by the c5 pawn and undefended.
        Position position("4k3/8/8/2p5/3N4/8/8/4K3 b - - 0 1");
        CHECK(position.see("c5d4") == 3);
        const std::vector<Hanging> hanging = position.hanging(true);
        CHECK(hanging.size() == 1);
        CHECK(hanging.front().square == squareFromName("d4"));
        CHECK(hanging.front().value == 3);
        // Defended, and the attacker is worth as much: the exchange no longer
        // wins anything, so the knight does not hang.
        Position defended("4k3/6b1/8/8/3N4/2P5/8/4K3 b - - 0 1");
        CHECK(defended.see("g7d4") == 0);
        CHECK(defended.hanging(true).empty());
        // Queen takes a defended pawn: losing.
        Position bad("4k3/1p6/2p5/8/8/8/8/3QK3 w - - 0 1");
        CHECK(bad.see("d1c1") == 0);
    }

    // checks and captures: the eleventh of all moves that refutes three
    // quarters of all blunders (teacher.md §1.2.2).
    {
        Position position("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
        const std::vector<std::string> loud = position.checksAndCaptures();
        CHECK(!loud.empty());
        CHECK(loud.size() < position.legalMoves().size());
    }

    // PGN output: seven-tag roster, correct numbering, FEN tag only when needed.
    {
        Position position;
        position.playSan("e4");
        position.playSan("e5");
        position.playSan("Nf3");
        PgnTags tags;
        tags.date = "2026.09.12";
        tags.white = "Lernender";
        tags.black = "Trainingsgegner";
        tags.result = "*";
        const std::string pgn = position.toPgn(tags);
        CHECK(pgn.find("[White \"Lernender\"]") != std::string::npos);
        CHECK(pgn.find("[Date \"2026.09.12\"]") != std::string::npos);
        CHECK(pgn.find("1. e4 e5 2. Nf3") != std::string::npos);
        CHECK(pgn.find("[FEN") == std::string::npos);

        Position fromFen("4k3/8/8/8/8/8/8/4K2R w K - 0 30");
        fromFen.playSan("Rh8+");
        const std::string pgn2 = fromFen.toPgn(tags);
        CHECK(pgn2.find("[SetUp \"1\"]") != std::string::npos);
        CHECK(pgn2.find("30. Rh8+") != std::string::npos);
    }

    // Outcomes, because the UI shows a sentence and not a bare "0-1".
    {
        Position mate("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
        CHECK(mate.outcome() == Outcome::BlackWins);
        CHECK(mate.endReason() == EndReason::Checkmate);
        Position stalemate("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        CHECK(stalemate.outcome() == Outcome::Draw);
        CHECK(stalemate.endReason() == EndReason::Stalemate);
    }

    std::printf("OK: %llu perft nodes over %d positions\n",
                static_cast<unsigned long long>(total),
                static_cast<int>(sizeof(kCases) / sizeof(kCases[0])));
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    return 0;
}
