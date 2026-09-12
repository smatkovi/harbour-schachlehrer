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
#ifndef SCHACH_CORE_SKILL_H
#define SCHACH_CORE_SKILL_H

// teacher.md §3 — the six dimensions and the two numbers each of them carries.
//
// theta_d is absolute, from calibrated puzzles, and imprecise (+-170 Elo after
// 25 items). a_d is relative, from the learner's own games, and precise,
// because it normalises against the learner himself and therefore needs no
// population norms — which is lucky, because none exist (§3.2).
//
// Selection of *difficulty* follows theta_d; selection of *training* follows
// a_d. That split runs through the whole app.

#include "Taxonomy.h"

#include <array>
#include <string>
#include <vector>

namespace schach {
namespace core {

constexpr int kDimensionCount = 6;

const char* dimensionKey(Dimension dimension);   // "TAK", …
const char* dimensionName(Dimension dimension);  // German
const char* dimensionQuestion(Dimension dimension);  // the one question it answers
Dimension dimensionFromKey(const std::string& key);

// teacher.md §4.6: 25 items give one overall number to about +-80 Elo and no
// dimension values at all, so a dimension is reported ordinally.
enum class Ordinal : std::uint8_t { Weak = 0, Unremarkable = 1, Strong = 2 };
const char* ordinalName(Ordinal ordinal);   // German

struct SkillState {
    double theta = 1000.0;                         // global estimate, Elo scale
    std::array<double, kDimensionCount> delta{};   // dimension deviations
    std::array<int, kDimensionCount> answers{};    // n_d
    std::array<double, kDimensionCount> mass{};    // sum of dW per dimension
    std::array<int, kDimensionCount> events{};     // event counts per dimension
    int ownMoves = 0;                              // denominator of the blunder rate
    int blunders = 0;                              // events with dW >= 18

    SkillState();
};

// teacher.md §4.4: shrink(n) = n / (n + 8). With four items a dimension keeps
// only a third of its raw deviation — the statistically correct treatment of a
// tiny sample, and what stops one failed item from branding a dimension weak.
double shrink(int answers);
double thetaOf(const SkillState& state, Dimension dimension);

// teacher.md §3.2 (b): a_d, the share of the dimension in the learner's own
// error mass. Computed unfiltered by L, because it is a ratio.
std::array<double, kDimensionCount> shares(const SkillState& state);
void addEvent(SkillState& state, const Finding& finding);
// The one absolute number the app shows (§3.2 c): blunders per 100 own moves.
double blunderRate(const SkillState& state);

// The dimensions ordered by a_d, largest first — this is what picks the next
// new pattern (§5.7 step 2).
std::vector<Dimension> byShare(const SkillState& state);

// teacher.md §3.4 — when is a pattern a pattern? A one-sided binomial test.
// The default z is 2.58 (1 % level), the counter-measure the spec asks for
// against multiple testing; only the strongest blind spot may be reported.
struct ClusterResult {
    bool significant = false;
    double excess = 0.0;    // k/n - p, how far past the expectation
    double margin = 0.0;    // k/n - (p + z*sd), distance to the threshold
    int k = 0, n = 0;
    double p = 0.0;
};

ClusterResult clusterTest(int k, int n, double p, double z = 2.58);

// Of several candidate blind spots, at most one is reported: the one with the
// largest distance to its threshold (§3.4 counter-measure).
struct ClusterCandidate {
    ErrorClass cls = ErrorClass::None;
    ClusterResult result;
};
ClusterCandidate strongestCluster(const std::vector<ClusterCandidate>& candidates);

// teacher.md §2.5 [RISIKO]: every motif carries a counter, and a motif that
// marks less than 0.5 % or more than 25 % of the events is suspect — Lichess'
// own `overloading` predicate is a plain `return False`, and that is exactly
// what this guard is for.
bool motifSuspect(int motifCount, int totalEvents);

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_SKILL_H
