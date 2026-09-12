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
#ifndef SCHACH_CORE_SRS_H
#define SCHACH_CORE_SRS_H

// teacher.md §5 — FSRS scheduling and the card life cycle.
//
// FSRS rather than SM-2 because it models memory as DSR with a power
// forgetting curve, which fits real review data better, and because it leads
// the open benchmark over ~10 000 Anki collections.
//
// Three decisions of §5.1 are baked in here:
//   1. planner only, no optimiser — the published default parameters are
//      compiled in; a per-user optimisation is worth doing after hundreds of
//      reviews, which is months away, and belongs in v2.
//   2. desired retention 0.85 instead of 0.90 — every repetition shows a
//      *different* position, so FSRS underestimates stability systematically
//      (§5.2 [RISIKO]); a lower target lengthens the intervals and compensates.
//   3. four grades, assigned automatically — the app knows whether the answer
//      was right, so asking the user would only add a source of error.
//
// No clock in here: days are passed in as day numbers.

#include <cstdint>

namespace schach {
namespace core {

enum class Rating : std::uint8_t { Again = 1, Hard = 2, Good = 3, Easy = 4 };

// teacher.md §5.5. The life cycle, with the retirement condition that no
// flashcard system has: the pattern must have survived in real games.
enum class CardState : std::uint8_t { New = 0, Learning, Review, Retired };

const char* ratingKey(Rating rating);
const char* cardStateKey(CardState state);

struct FsrsParams {
    // The published FSRS-5 default parameters. They are used unchanged: a
    // per-user optimisation needs hundreds of reviews and belongs in v2 (§5.1).
    double w[19] = { 0.40255, 1.18385, 3.17300, 15.69105, 7.19490,
                     0.53450, 1.46040, 0.00460, 1.54575, 0.11920,
                     1.01925, 1.93950, 0.11000, 0.29605, 2.26980,
                     0.23150, 2.98980, 0.51655, 0.66210 };
    double desiredRetention = 0.85;
    int maximumInterval = 36500;   // 100 years, i.e. effectively none
};

const FsrsParams& defaultParams();

struct SrsState {
    CardState state = CardState::New;
    double stability = 0.0;     // days until retrievability falls to 0.9
    double difficulty = 0.0;    // 1 … 10
    int reps = 0;
    int lapses = 0;
    int consecutiveCorrect = 0;
    long long dueDay = 0;       // day number, see above
    long long lastReviewDay = -1;
    int intervalDays = 0;
};

// The power forgetting curve of FSRS: R(t) = (1 + F·t/S)^D with D = -0.5.
double retrievability(double elapsedDays, double stability);
// The interval that lands exactly on the desired retention.
double intervalFor(double stability, double desiredRetention);

// teacher.md §5.1: the grade is derived, never asked.
//   Again  wrong answer
//   Hard   right after a hint, or slower than 45 s
//   Good   right in 10–45 s
//   Easy   right under 10 s
Rating ratingFor(bool correct, bool usedHint, int milliseconds);

// One review. `today` is a day number; the state comes back scheduled.
SrsState applyReview(const SrsState& state, Rating rating, long long today,
                     const FsrsParams& params = defaultParams());

// teacher.md §5.4 — an error of this class in a real game. The game *is* the
// retrieval test, and a failure there is a heavier lapse than one in the
// drill, so it counts as `Again` and the card becomes due today.
SrsState applyGameError(const SrsState& state, long long today,
                        const FsrsParams& params = defaultParams());

// A retired card that failed in a game comes back, but not from zero: it is
// no total loss, so S is divided by three and capped at 21 days (§5.4).
SrsState reactivate(const SrsState& state, long long today);

// teacher.md §5.5, all three conditions. `transferSightings` counts games in
// which the pattern occurred without the error class firing; `daysSinceSeen`
// lets the 12-month escape hatch work for rare patterns.
bool retirementReached(const SrsState& state, int transferSightings, long long daysSinceCreated,
                       long long daysSinceLastOccurrence);

bool isDue(const SrsState& state, long long today);

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_SRS_H
