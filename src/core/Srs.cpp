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
#include "Srs.h"

#include <algorithm>
#include <cmath>

namespace schach {
namespace core {

namespace {

// FSRS forgetting curve constants.
constexpr double kDecay = -0.5;
const double kFactor = 19.0 / 81.0;

double clampDifficulty(double difficulty)
{
    return std::min(10.0, std::max(1.0, difficulty));
}

double clampStability(double stability)
{
    return std::max(0.01, stability);
}

int grade(Rating rating) { return static_cast<int>(rating); }

double initialStability(const FsrsParams& params, Rating rating)
{
    return clampStability(params.w[grade(rating) - 1]);
}

double initialDifficulty(const FsrsParams& params, Rating rating)
{
    return clampDifficulty(params.w[4] - std::exp(params.w[5] * (grade(rating) - 1)) + 1.0);
}

double nextDifficulty(const FsrsParams& params, double difficulty, Rating rating)
{
    const double delta = -params.w[6] * (grade(rating) - 3);
    // Linear damping: the closer D already is to 10, the less a bad answer
    // moves it.
    const double moved = difficulty + delta * (10.0 - difficulty) / 9.0;
    // Mean reversion towards the difficulty an "easy" first answer would give.
    const double reverted = params.w[7] * initialDifficulty(params, Rating::Easy)
            + (1.0 - params.w[7]) * moved;
    return clampDifficulty(reverted);
}

// FSRS-5 short-term term: a second look on the same day moves the stability
// only a little, and never through the long-term formula.
double sameDayStability(const FsrsParams& params, double stability, Rating rating)
{
    return clampStability(stability * std::exp(params.w[17] * (grade(rating) - 3 + params.w[18])));
}

double stabilityAfterRecall(const FsrsParams& params, double difficulty, double stability,
                            double r, Rating rating)
{
    const double hardPenalty = rating == Rating::Hard ? params.w[15] : 1.0;
    const double easyBonus = rating == Rating::Easy ? params.w[16] : 1.0;
    const double growth = std::exp(params.w[8]) * (11.0 - difficulty)
            * std::pow(stability, -params.w[9])
            * (std::exp(params.w[10] * (1.0 - r)) - 1.0)
            * hardPenalty * easyBonus;
    return clampStability(stability * (1.0 + growth));
}

double stabilityAfterForget(const FsrsParams& params, double difficulty, double stability, double r)
{
    const double value = params.w[11] * std::pow(difficulty, -params.w[12])
            * (std::pow(stability + 1.0, params.w[13]) - 1.0)
            * std::exp(params.w[14] * (1.0 - r));
    // FSRS 4.5 and later: a lapse never *raises* the stability.
    return clampStability(std::min(value, stability));
}

} // namespace

const FsrsParams& defaultParams()
{
    static const FsrsParams params;
    return params;
}

const char* ratingKey(Rating rating)
{
    switch (rating) {
    case Rating::Again: return "nochmal";
    case Rating::Hard: return "schwer";
    case Rating::Good: return "gut";
    case Rating::Easy: return "leicht";
    }
    return "gut";
}

const char* cardStateKey(CardState state)
{
    switch (state) {
    case CardState::New: return "neu";
    case CardState::Learning: return "lernen";
    case CardState::Review: return "wiederholung";
    case CardState::Retired: return "ruhestand";
    }
    return "neu";
}

double retrievability(double elapsedDays, double stability)
{
    if (stability <= 0.0)
        return 0.0;
    if (elapsedDays <= 0.0)
        return 1.0;
    return std::pow(1.0 + kFactor * elapsedDays / stability, kDecay);
}

double intervalFor(double stability, double desiredRetention)
{
    const double retention = std::min(0.99, std::max(0.70, desiredRetention));
    return stability / kFactor * (std::pow(retention, 1.0 / kDecay) - 1.0);
}

Rating ratingFor(bool correct, bool usedHint, int milliseconds)
{
    if (!correct)
        return Rating::Again;
    if (usedHint || milliseconds > 45000)
        return Rating::Hard;
    if (milliseconds < 10000)
        return Rating::Easy;
    return Rating::Good;
}

SrsState applyReview(const SrsState& state, Rating rating, long long today,
                     const FsrsParams& params)
{
    SrsState next = state;
    next.reps = state.reps + 1;
    next.lastReviewDay = today;

    if (state.state == CardState::New || state.lastReviewDay < 0) {
        next.stability = initialStability(params, rating);
        next.difficulty = initialDifficulty(params, rating);
    } else {
        const double elapsed = static_cast<double>(today - state.lastReviewDay);
        const double r = retrievability(elapsed, state.stability);
        next.difficulty = nextDifficulty(params, state.difficulty, rating);
        if (elapsed < 1.0)
            next.stability = sameDayStability(params, state.stability, rating);
        else
            next.stability = rating == Rating::Again
                    ? stabilityAfterForget(params, next.difficulty, state.stability, r)
                    : stabilityAfterRecall(params, next.difficulty, state.stability, r, rating);
    }

    if (rating == Rating::Again) {
        next.lapses = state.lapses + 1;
        next.consecutiveCorrect = 0;
        // A failure always drops back into learning (§5.5) and is repeated the
        // same day; the interval is not allowed to go below one day, so the
        // next session picks it up.
        next.state = CardState::Learning;
        next.intervalDays = 1;
    } else {
        next.consecutiveCorrect = state.consecutiveCorrect + 1;
        next.state = (state.state == CardState::New || state.state == CardState::Learning)
                ? (next.consecutiveCorrect >= 1 ? CardState::Review : CardState::Learning)
                : CardState::Review;
        const double interval = intervalFor(next.stability, params.desiredRetention);
        next.intervalDays = std::max(1, std::min(params.maximumInterval,
                                                 static_cast<int>(std::lround(interval))));
    }
    next.dueDay = today + next.intervalDays;
    return next;
}

SrsState applyGameError(const SrsState& state, long long today, const FsrsParams& params)
{
    // teacher.md §5.4: the game is the retrieval test. A retired card comes
    // back through reactivate(); everything else is a plain lapse due today.
    if (state.state == CardState::Retired)
        return reactivate(state, today);
    SrsState next = applyReview(state, Rating::Again, today, params);
    next.dueDay = today;      // due now, not tomorrow
    next.intervalDays = 0;
    return next;
}

SrsState reactivate(const SrsState& state, long long today)
{
    SrsState next = state;
    next.state = CardState::Learning;
    next.stability = std::min(state.stability / 3.0, 21.0);
    if (next.stability <= 0.0)
        next.stability = 1.0;
    next.lapses = state.lapses + 1;
    next.consecutiveCorrect = 0;
    next.dueDay = today;
    next.intervalDays = 0;
    next.lastReviewDay = today;
    return next;
}

bool retirementReached(const SrsState& state, int transferSightings, long long daysSinceCreated,
                       long long daysSinceLastOccurrence)
{
    if (state.state != CardState::Review)
        return false;
    if (state.stability < 180.0)
        return false;
    if (state.consecutiveCorrect < 4)
        return false;
    if (transferSightings >= 2)
        return true;
    // teacher.md §5.5 [RISIKO]: a rare pattern would never satisfy the
    // transfer condition. After twelve months without an occurrence it counts
    // as met, and the card carries the note "rare, not confirmed in play".
    const long long quiet = std::max(daysSinceCreated, daysSinceLastOccurrence);
    return quiet >= 365;
}

bool isDue(const SrsState& state, long long today)
{
    return state.state != CardState::Retired && state.dueDay <= today;
}

} // namespace core
} // namespace schach
