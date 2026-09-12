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
#ifndef SCHACH_CORE_PLACEMENT_H
#define SCHACH_CORE_PLACEMENT_H

// teacher.md §4 — the adaptive placement test, implemented along the pseudo
// code of §4.8.
//
// Elo-like updating (equivalent to Rasch) rather than IRT-2PL: it needs no
// pre-calibration of the item bank, it estimates item difficulty and person
// ability at the same time, and it runs online in constant memory. That
// matters because four of the six item sorts are self-generated and therefore
// uncalibrated until enough user data exists (§4.2, §4.3).
//
// What it must not do: print six numbers. §4.6 shows that four items per
// dimension give +-189 Elo, which is wider than the whole range the app
// addresses. The result per dimension is therefore ordinal.

#include "Skill.h"

#include <array>
#include <string>
#include <vector>

namespace schach {
namespace core {

// One logit is 400/ln(10) Elo points.
constexpr double kLogitElo = 173.7;
// Target solve probability during the test. §4.5: 0.70 rather than the
// maximum-information 0.50, at a cost of exactly 16 % information per item —
// four items out of 25 — and almost nobody walks out.
constexpr double kTargetOffset = 147.0;

struct PlacementItem {
    std::string id;
    Dimension dimension = Dimension::TAK;
    double difficulty = 1000.0;   // Elo scale
    bool selfGenerated = false;   // then the item learns along (§4.4)
    int attempts = 0;
};

struct PlacementPlan {
    double startDifficulty = 850.0;
    double theta = 1000.0;
    double seElo = 0.0;
    int items = 0;
    std::vector<Dimension> firstAreas;                   // what to train first
    std::array<Ordinal, kDimensionCount> ordinal{};      // weak / unremarkable / strong
    std::array<double, kDimensionCount> thetaD{};        // for difficulty selection only
    bool aborted = false;
};

class Placement
{
public:
    // `selfReportedElo` is a club rating the user typed in; the anchor items
    // then start at that value minus 100 (§4.7). Pass 0 for "no idea".
    explicit Placement(int selfReportedElo = 0);

    // The dimension the next item should come from. During the anchor phase
    // this walks the six in a fixed order; afterwards it is the dimension with
    // the largest remaining uncertainty, never the same twice in a row (§4.8).
    Dimension nextDimension() const;
    // theta_d - 147, the difficulty to look for.
    double nextDifficulty() const;

    // Record one answer. `item` may be null for library items whose difficulty
    // is fixed; a self-generated item is updated in place.
    void record(Dimension dimension, double itemDifficulty, bool correct,
                PlacementItem* item = nullptr);

    // §4.7 stopping rule. `elapsedSeconds` is passed in because the core has
    // no clock.
    bool finished(double elapsedSeconds) const;

    int answered() const { return m_answers; }
    double theta() const { return m_state.theta; }
    double thetaOf(Dimension dimension) const;
    double seLogit() const;
    double seElo() const;

    // §4.8 plan_aus(): the plan, with the honest ordinal verdict.
    PlacementPlan plan(bool aborted = false) const;

    const SkillState& state() const { return m_state; }

private:
    SkillState m_state;
    std::array<double, kDimensionCount> m_information{};
    double m_totalInformation = 0.0;
    int m_answers = 0;
    int m_lastDimension = -1;
};

// §4.4, exported for the tests: the Rasch expectation on the Elo scale.
double raschExpectation(double theta, double difficulty);

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_PLACEMENT_H
