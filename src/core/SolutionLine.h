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
#ifndef SCHACH_CORE_SOLUTIONLINE_H
#define SCHACH_CORE_SOLUTIONLINE_H

// teacher.md §6.5 — a task whose answer is a *line*, not a move.
//
// The line alternates: learner, opponent, learner, … starting with the
// learner, and it always ends on a learner move. That is the Lichess puzzle
// format, and the data model is taken from salichess' PuzzleLogic
// (CREDITS/CODE.md), including the one rule that is not obvious: **a move that
// mates solves the task even when it is not the move in the line.** There is
// nothing to be right about after mate.
//
// What is *not* taken from there is the interaction. salichess plays the
// opponent's reply onto the board automatically, which is right for a Lichess
// client and wrong here — §6.5 quotes the Stappenmethode on exactly that:
//
//     "The computer gives the opponent's answer automatically, so there is no
//      need to look at alternatives. Superficiality is trumps, because real
//      thinking is less necessary."
//
// Hence two modes, and the difference between them is the whole point:
//
//   * `Guided` — the opponent's reply appears as soon as the learner's move is
//     right. Used only for the introductory instance of a new pattern (§5.6
//     block B step 1), because §6.1(3) says desirable difficulties backfire
//     without prior knowledge (expertise reversal, Gates' 60 % optimum).
//   * `WholeLine` — everything else. The learner enters their moves *and* the
//     opponent's replies before anything is executed; the board does not move
//     until the line is complete. Then it is played back.
//
// Qt-free, like the rest of core/: it is checked against fixed inputs in
// tests/test_line.cpp.

#include "Position.h"

#include <string>
#include <vector>

namespace schach {
namespace core {

class SolutionLine
{
public:
    enum class Mode { WholeLine = 0, Guided };

    enum class Verdict {
        Wrong,      // this move is not the one, and it does not mate either
        Accepted,   // right so far, the line goes on
        Solved      // right, and there is nothing left to find
    };

    // `line` is UCI, learner first, alternating, odd length. `start` is the
    // position the learner sees. An even-length line is accepted and its last
    // (opponent) move ignored — a generator that trails one is a data bug we
    // survive rather than a reason to show no task.
    void start(const Position& startPosition, const std::vector<std::string>& line,
               Mode mode = Mode::WholeLine);

    // One move from the learner. In `Guided` mode a correct move consumes the
    // opponent's reply as well and `lastReply()` names it; in `WholeLine` mode
    // the learner is expected to enter that reply themselves as the next move,
    // and it is checked like any other.
    Verdict play(const std::string& uciMove);

    // Take the last entered move back. Free and unlimited, as in §7.6: the
    // learner is composing a line, and a slip of the finger is not an answer.
    bool undo();

    // The position the next move is entered from. In `WholeLine` mode this is
    // the line played out in the background — the board the learner *sees*
    // stays at the start until they are done, which is what makes them hold
    // the position in their head.
    const Position& current() const { return m_current; }
    const Position& startPosition() const { return m_start; }

    bool finished() const { return m_step >= int(m_line.size()); }
    // Set by the first wrong move and never cleared: §5.1 grades a task that
    // needed a second attempt as Hard, not as right.
    bool failed() const { return m_failed; }
    int step() const { return m_step; }
    // How many moves the learner still has to enter. Shown as dots, never as
    // "mate in three" — that would name the kind of task (§6.2).
    int remaining() const;
    // How many of the entries are the learner's own moves, for the UI.
    int learnerMovesTotal() const;

    // What should have been played now — hint level 3 and the solution view.
    std::string expected() const;
    // In Guided mode: the opponent move that was just played onto the board.
    const std::string& lastReply() const { return m_lastReply; }

    // Everything entered so far, UCI, in order. The playback uses it.
    const std::vector<std::string>& entered() const { return m_entered; }
    const std::vector<std::string>& line() const { return m_line; }
    // The first entry that left the line, -1 while nothing has. Feedback is
    // given *there* and not on the whole line, because "wrong" on four moves
    // at once is the bare "falsch" that §6.6 forbids.
    int divergedAt() const { return m_diverged; }

private:
    bool isLearnerMove(int step) const { return (step % 2) == 0; }

    Position m_start;
    Position m_current;
    std::vector<std::string> m_line;
    std::vector<std::string> m_entered;
    std::string m_lastReply;
    Mode m_mode = Mode::WholeLine;
    int m_step = 0;
    int m_diverged = -1;
    bool m_failed = false;
};

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_SOLUTIONLINE_H
