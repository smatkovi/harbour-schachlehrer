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
#include "SolutionLine.h"

namespace schach {
namespace core {

namespace {

// Two spellings of the same move must not count as two moves: a promotion
// without its piece letter is a queen promotion (that is what every generator
// means), and castling written as king-onto-rook is the Chess960 form of
// e1g1. Neither is the learner's mistake, so neither may be judged as one.
std::string normalise(const Position& position, const std::string& uci)
{
    if (uci.size() < 4)
        return std::string();
    if (position.isLegal(uci))
        return uci;

    const std::string promoted = uci.substr(0, 4) + "q";
    if (uci.size() == 4 && position.isLegal(promoted))
        return promoted;

    const int from = squareFromName(uci.substr(0, 2));
    const int to = squareFromName(uci.substr(2, 2));
    if (from < 0 || to < 0)
        return std::string();
    const char mover = position.pieceAt(from);
    if (mover == 'K' || mover == 'k') {
        const bool whiteKing = mover == 'K';
        if (position.pieceAt(to) == (whiteKing ? 'R' : 'r')) {
            const int rank = whiteKing ? 0 : 7;
            const bool kingSide = fileOf(to) > fileOf(from);
            const std::string standard =
                    squareName(from) + squareName(rank * 8 + (kingSide ? 6 : 2));
            if (position.isLegal(standard))
                return standard;
        }
    }
    return std::string();
}

} // namespace

void SolutionLine::start(const Position& startPosition, const std::vector<std::string>& line,
                         Mode mode)
{
    m_start = startPosition;
    m_current = startPosition;
    m_line = line;
    // The line has to end on a learner move, or the last thing the learner
    // does is watch. A trailing opponent move is dropped rather than shown.
    if (!m_line.empty() && (m_line.size() % 2) == 0)
        m_line.pop_back();
    m_entered.clear();
    m_lastReply.clear();
    m_mode = mode;
    m_step = 0;
    m_diverged = -1;
    m_failed = false;
}

int SolutionLine::remaining() const
{
    const int left = int(m_line.size()) - m_step;
    return left > 0 ? left : 0;
}

int SolutionLine::learnerMovesTotal() const
{
    return (int(m_line.size()) + 1) / 2;
}

std::string SolutionLine::expected() const
{
    if (finished())
        return std::string();
    return m_line.at(std::size_t(m_step));
}

SolutionLine::Verdict SolutionLine::play(const std::string& uciMove)
{
    if (finished())
        return Verdict::Wrong;

    const std::string played = normalise(m_current, uciMove);
    if (played.empty())
        return Verdict::Wrong;   // not even legal; nothing happened

    const std::string wanted = normalise(m_current, m_line.at(std::size_t(m_step)));

    if (played == wanted) {
        m_current.play(played);
        m_entered.push_back(played);
        ++m_step;

        if (m_mode == Mode::Guided && !finished() && !isLearnerMove(m_step)) {
            // The one place the app answers for the opponent: the introductory
            // instance of a new pattern (§5.6 block B step 1, §6.1(3)).
            m_lastReply = m_line.at(std::size_t(m_step));
            m_current.play(m_lastReply);
            m_entered.push_back(m_lastReply);
            ++m_step;
        }
        return finished() ? Verdict::Solved : Verdict::Accepted;
    }

    // Mate ends it, whoever found it and however. Lichess scores its puzzles
    // this way and it is plainly right: there is nothing left to be right
    // about. Only for the learner's own move — the opponent does not get to
    // mate himself out of the line.
    if (isLearnerMove(m_step)) {
        const Position after = m_current.after(played);
        if (after.endReason() == EndReason::Checkmate) {
            m_current = after;
            m_entered.push_back(played);
            m_step = int(m_line.size());
            return Verdict::Solved;
        }
    }

    if (m_diverged < 0)
        m_diverged = m_step;
    m_failed = true;
    return Verdict::Wrong;
}

bool SolutionLine::undo()
{
    if (m_entered.empty())
        return false;
    // In Guided mode the opponent's reply and the move that drew it are one
    // entry as far as the learner is concerned; taking back half of that would
    // leave them on a board they never played into.
    int back = 1;
    if (m_mode == Mode::Guided && m_entered.size() >= 2 && isLearnerMove(m_step))
        back = 2;
    for (int i = 0; i < back && !m_entered.empty(); ++i) {
        m_current.undo();
        m_entered.pop_back();
        --m_step;
    }
    if (m_step < 0)
        m_step = 0;
    m_lastReply.clear();
    // `m_failed` deliberately stays: taking a wrong move back does not make
    // the attempt a clean one (§5.1).
    return true;
}

} // namespace core
} // namespace schach
