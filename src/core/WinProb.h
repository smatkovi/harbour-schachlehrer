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
#ifndef SCHACH_CORE_WINPROB_H
#define SCHACH_CORE_WINPROB_H

// teacher.md §0.5: centipawns -> win probability.
//
// The app never compares centipawn differences. It compares win probability,
// because that is the only way "+9.00 down to +6.00 is not a blunder" falls
// out of the arithmetic instead of out of a special case. The constant is the
// Lichess regression constant; taking it verbatim keeps our error thresholds
// consistent with the largest freely available body of analysed games.
//
// Qt-free on purpose (docs/design.md §3).

namespace schach {
namespace core {

// The Lichess winning-chances constant (lila PR 11148). Do not "improve" it:
// its value is what makes our thresholds comparable to published numbers.
constexpr double kWinProbK = -0.00368208;

// Severity of a move, in the corrected reading of teacher.md §2.3.1. The
// blunder line sits at 18 pp, not at Lichess' 15, because we turn events into
// homework and 15 pp yields a dozen of them per game at 1100 Elo.
enum class Severity {
    Clean = 0,       // below the inaccuracy line
    Inaccuracy = 1,  //  5 <= dW < 10
    Mistake = 2,     // 10 <= dW < 18
    Blunder = 3      //      dW >= 18
};

// An engine verdict for one position, from the side to move's point of view.
struct Score {
    bool valid = false;
    bool isMate = false;
    int cp = 0;      // centipawns, valid when !isMate
    int mateIn = 0;  // plies-to-mate sign convention: > 0 = side to move mates

    static Score centipawns(int value)
    {
        Score s;
        s.valid = true;
        s.cp = value;
        return s;
    }
    static Score mate(int movesToMate)
    {
        Score s;
        s.valid = true;
        s.isMate = true;
        s.mateIn = movesToMate;
        return s;
    }
};

// wc(cp) on [-1, +1].
double winningChances(int cp);

// W(cp) = 50 + 50*wc on [0, 100] percentage points.
double winProbability(int cp);

// Same, but a mate verdict saturates the scale instead of being converted
// through a made-up centipawn value.
double winProbability(const Score& score);
double winningChances(const Score& score);

// dW = W(before) - W(after), both from the mover's point of view, in
// percentage points. Positive means the move lost something.
double deltaW(const Score& before, const Score& after);

// The one scale of the whole app (teacher.md §2.3.1).
Severity severityOf(double deltaWpp);

// The learning threshold L(theta) (teacher.md §2.3.2). It *rises* as the
// player gets weaker: a 1100 player produces so many events that a low
// threshold is noise, and his 8 pp inaccuracies are not learnable anyway.
double learningThreshold(int learnerElo);

// Cards per game allowed at that strength (same table).
int maxCardsPerGame(int learnerElo);

// The dynamic readjustment of L from teacher.md §2.3.2: keep the workload
// constant even when theta is estimated wrongly. Bounds 8 <= L <= 35.
double adjustThreshold(double currentL, double eventsPerGameAverage, int maxCards);

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_WINPROB_H
