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
// core::SolutionLine — the multi-move answer of teacher.md §6.5.
//
// The two modes are the point of the class, so both are checked here, and so
// is the rule that looks like a bug until one reads why it is there: a move
// that mates solves the task even when the line says something else.
#include "core/SolutionLine.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

Position at(const std::string& fen)
{
    Position position;
    if (!position.setFen(fen))
        std::fprintf(stderr, "bad FEN in the test itself: %s\n", fen.c_str());
    return position;
}

// A three-ply line: learner, opponent, learner. Legal-mover position taken
// from the shipped item bank so the test needs no generator.
// 1... Nxe2+ 2.Kf1 Nxc1 — the knight takes with check and collects the rook.
const char* kFen = "4r3/1bQ5/1p1p1kp1/1PpPp3/2P1n3/6nP/4B1P1/2R3K1 b - - 0 32";
const std::vector<std::string> kLine = { "g3e2", "g1f1", "e2c1" };

void oneMoveLineStillWorks()
{
    SolutionLine line;
    line.start(at(kFen), { "g3e2" });
    CHECK(line.learnerMovesTotal() == 1);
    CHECK(line.remaining() == 1);
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Solved);
    CHECK(line.finished());
    CHECK(!line.failed());
}

void wholeLineIsEnteredByTheLearner()
{
    SolutionLine line;
    line.start(at(kFen), kLine, SolutionLine::Mode::WholeLine);
    CHECK(line.remaining() == 3);
    // The learner's move is accepted and the board does *not* answer for him:
    // the opponent's reply is his to enter as well (§6.5).
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Accepted);
    CHECK(line.entered().size() == 1u);
    CHECK(line.lastReply().empty());
    CHECK(line.play("g1f1") == SolutionLine::Verdict::Accepted);
    CHECK(line.play("e2c1") == SolutionLine::Verdict::Solved);
    CHECK(line.finished());
    CHECK(!line.failed());
}

void guidedModeAnswersForTheOpponent()
{
    SolutionLine line;
    line.start(at(kFen), kLine, SolutionLine::Mode::Guided);
    // One move from the learner, and the reply is already on the board — the
    // introductory instance of a new pattern, §6.1(3).
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Accepted);
    CHECK(line.lastReply() == "g1f1");
    CHECK(line.entered().size() == 2u);
    CHECK(line.play("e2c1") == SolutionLine::Verdict::Solved);
}

void aWrongMoveIsWrongAndStaysRemembered()
{
    SolutionLine line;
    line.start(at(kFen), kLine, SolutionLine::Mode::WholeLine);
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Accepted);
    // The opponent has a legal alternative, but the line is the line.
    CHECK(line.play("g1h2") == SolutionLine::Verdict::Wrong);
    CHECK(line.failed());
    CHECK(line.divergedAt() == 1);
    // Taking the entry back does not make the attempt a clean one (§5.1).
    CHECK(line.undo());
    CHECK(line.failed());
}

void takingBackUndoesWhatWasEntered()
{
    SolutionLine line;
    line.start(at(kFen), kLine, SolutionLine::Mode::WholeLine);
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Accepted);
    CHECK(line.play("g1f1") == SolutionLine::Verdict::Accepted);
    CHECK(line.entered().size() == 2u);
    CHECK(line.undo());
    CHECK(line.entered().size() == 1u);
    CHECK(line.step() == 1);
    // And the position is back where it was, so the next entry is judged in
    // the right place.
    CHECK(line.current().fen() == at(kFen).after("g3e2").fen());
    CHECK(line.undo());
    CHECK(line.entered().empty());
    CHECK(line.current().fen() == at(kFen).fen());
    CHECK(!line.undo());
}

void guidedTakeBackReturnsBothHalves()
{
    SolutionLine line;
    line.start(at(kFen), kLine, SolutionLine::Mode::Guided);
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Accepted);
    CHECK(line.entered().size() == 2u);   // ours and the answer
    CHECK(line.undo());
    // Half a take-back would leave the learner on a board he never played into.
    CHECK(line.entered().empty());
    CHECK(line.step() == 0);
}

// Two rooks, two back-rank mates: Ra8# and Rb8#. The line names one of them.
const char* kMateFen = "6k1/5ppp/8/8/8/8/8/RR5K w - - 0 1";

void mateSolvesItHoweverItIsReached()
{
    SolutionLine line;
    line.start(at(kMateFen), { "a1a8" });
    CHECK(line.play("a1a8") == SolutionLine::Verdict::Solved);

    SolutionLine other;
    other.start(at(kMateFen), { "a1a8" });
    // Not the move in the line — but it mates, so there is nothing left to be
    // right about. This is the Lichess rule and salichess' PuzzleLogic does
    // the same (CREDITS/CODE.md).
    CHECK(other.play("b1b8") == SolutionLine::Verdict::Solved);
    CHECK(other.finished());
    CHECK(!other.failed());
}

void aMoveThatDoesNotMateIsSimplyWrong()
{
    SolutionLine line;
    line.start(at(kMateFen), { "a1a8" });
    CHECK(line.play("h1g1") == SolutionLine::Verdict::Wrong);
    CHECK(line.failed());
}

void anIllegalMoveChangesNothing()
{
    SolutionLine line;
    line.start(at(kFen), kLine, SolutionLine::Mode::WholeLine);
    CHECK(line.play("a1a8") == SolutionLine::Verdict::Wrong);
    CHECK(line.entered().empty());
    CHECK(line.step() == 0);
}

void aTrailingOpponentMoveIsDropped()
{
    // A generator that trails an opponent move is a data bug we survive: the
    // last thing the learner does must be a move, not watching one.
    SolutionLine line;
    line.start(at(kFen), { "g3e2", "g1f1" }, SolutionLine::Mode::WholeLine);
    CHECK(line.line().size() == 1u);
    CHECK(line.play("g3e2") == SolutionLine::Verdict::Solved);
}

} // namespace

int main()
{
    oneMoveLineStillWorks();
    wholeLineIsEnteredByTheLearner();
    guidedModeAnswersForTheOpponent();
    aWrongMoveIsWrongAndStaysRemembered();
    takingBackUndoesWhatWasEntered();
    guidedTakeBackReturnsBothHalves();
    mateSolvesItHoweverItIsReached();
    aMoveThatDoesNotMateIsSimplyWrong();
    anIllegalMoveChangesNothing();
    aTrailingOpponentMoveIsDropped();

    if (failures) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("test_line: all checks passed\n");
    return 0;
}
