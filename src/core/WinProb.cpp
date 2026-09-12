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
#include "WinProb.h"

#include <cmath>

namespace schach {
namespace core {

double winningChances(int cp)
{
    return 2.0 / (1.0 + std::exp(kWinProbK * static_cast<double>(cp))) - 1.0;
}

double winProbability(int cp)
{
    return 50.0 + 50.0 * winningChances(cp);
}

double winningChances(const Score& score)
{
    if (!score.valid)
        return 0.0;
    // A forced mate is the end of the scale, not a very large centipawn
    // number: mapping it through the logistic would make "mate in 8" and
    // "mate in 2" different, and teacher.md §0.5 says a slower mate instead
    // of a faster one is never an error.
    if (score.isMate)
        return score.mateIn >= 0 ? 1.0 : -1.0;
    return winningChances(score.cp);
}

double winProbability(const Score& score)
{
    return 50.0 + 50.0 * winningChances(score);
}

double deltaW(const Score& before, const Score& after)
{
    if (!before.valid || !after.valid)
        return 0.0;
    return winProbability(before) - winProbability(after);
}

Severity severityOf(double deltaWpp)
{
    if (deltaWpp >= 18.0)
        return Severity::Blunder;
    if (deltaWpp >= 10.0)
        return Severity::Mistake;
    if (deltaWpp >= 5.0)
        return Severity::Inaccuracy;
    return Severity::Clean;
}

namespace {

// teacher.md §2.3.2, one table instead of an if-chain so the numbers can be
// read off against the specification.
struct ThresholdRow {
    int maxElo;      // upper bound of the band, exclusive
    double learn;    // L in dW percentage points
    int maxCards;    // cards per game
};

const ThresholdRow kThresholds[] = {
    { 1000, 28.0, 3 },
    { 1300, 24.0, 4 },
    { 1600, 20.0, 5 },
    { 1900, 15.0, 5 },
    { 1 << 30, 10.0, 6 },
};

const ThresholdRow& rowFor(int elo)
{
    for (const ThresholdRow& row : kThresholds) {
        if (elo < row.maxElo)
            return row;
    }
    return kThresholds[4];
}

} // namespace

double learningThreshold(int learnerElo)
{
    return rowFor(learnerElo).learn;
}

int maxCardsPerGame(int learnerElo)
{
    return rowFor(learnerElo).maxCards;
}

double adjustThreshold(double currentL, double eventsPerGameAverage, int maxCards)
{
    double l = currentL;
    if (eventsPerGameAverage > static_cast<double>(maxCards))
        l += 2.0;
    else if (eventsPerGameAverage < 1.0)
        l -= 2.0;
    if (l < 8.0)
        l = 8.0;
    if (l > 35.0)
        l = 35.0;
    return l;
}

} // namespace core
} // namespace schach
