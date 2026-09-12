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
// The adaptive placement test of teacher.md §4, checked against simulated
// learners of known strength: a learner whose true theta is known answers
// every item with the Rasch probability, and the estimator has to find him.
//
// The test also pins down what the estimate must *not* claim: §4.6 shows that
// 25 items give one overall value to about +-80 Elo and no dimension values at
// all, so a dimension may only come out ordinal, and usually as "unremarkable".
#include "core/Placement.h"
#include "core/Skill.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
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

// One simulated learner. `trueTheta` is his real strength; `bias` shifts a
// single dimension so that the "remarkable dimension" logic can be exercised.
struct Learner {
    double trueTheta = 1000.0;
    Dimension weakDimension = Dimension::Count;
    double weakBy = 0.0;
};

double trueThetaFor(const Learner& learner, Dimension dimension)
{
    if (dimension == learner.weakDimension)
        return learner.trueTheta - learner.weakBy;
    return learner.trueTheta;
}

// Run one whole placement session. Item difficulties are what the estimator
// asks for, which is the realistic case: the bank has 6.1 million calibrated
// puzzles, so a puzzle near the requested rating always exists.
PlacementPlan runSession(const Learner& learner, std::mt19937& rng, int selfReported = 0)
{
    Placement placement(selfReported);
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    double elapsed = 0.0;
    while (!placement.finished(elapsed)) {
        const Dimension dimension = placement.nextDimension();
        const double difficulty = placement.nextDifficulty();
        const double p = raschExpectation(trueThetaFor(learner, dimension), difficulty);
        placement.record(dimension, difficulty, coin(rng) < p);
        elapsed += 25.0;   // about 25 s per item, well inside the 14 minutes
    }
    return placement.plan();
}

double meanEstimate(double trueTheta, int runs, std::mt19937& rng)
{
    double sum = 0.0;
    for (int i = 0; i < runs; ++i) {
        Learner learner;
        learner.trueTheta = trueTheta;
        sum += runSession(learner, rng).theta;
    }
    return sum / runs;
}

} // namespace

int main()
{
    std::mt19937 rng(20260912);

    // --- the model itself ---------------------------------------------------
    CHECK(std::fabs(raschExpectation(1000.0, 1000.0) - 0.5) < 1e-9);
    CHECK(std::fabs(raschExpectation(1400.0, 1000.0) - 10.0 / 11.0) < 1e-9);
    // §4.5: an item 147 points below theta is solved with p = 0.70, which costs
    // exactly 16 % of the information of a p = 0.50 item and keeps people in.
    {
        const double p = raschExpectation(1000.0, 1000.0 - kTargetOffset);
        CHECK(std::fabs(p - 0.70) < 0.005);
        const double information = p * (1.0 - p);
        CHECK(std::fabs(information / 0.25 - 0.84) < 0.01);
    }
    // §4.4: shrinkage. With four answers a dimension keeps a third of its
    // deviation, with forty it keeps five sixths.
    CHECK(std::fabs(shrink(4) - 1.0 / 3.0) < 1e-9);
    CHECK(std::fabs(shrink(40) - 40.0 / 48.0) < 1e-9);
    CHECK(shrink(0) == 0.0);

    // --- the course of one session -----------------------------------------
    {
        Placement placement;
        // Phase 1 is the anchor: one item per dimension, in a fixed order.
        std::vector<Dimension> anchors;
        for (int i = 0; i < 6; ++i) {
            const Dimension dimension = placement.nextDimension();
            anchors.push_back(dimension);
            placement.record(dimension, placement.nextDifficulty(), true);
        }
        for (int i = 0; i < 6; ++i) {
            for (int j = i + 1; j < 6; ++j)
                CHECK(anchors[static_cast<std::size_t>(i)] != anchors[static_cast<std::size_t>(j)]);
        }
        // Phase 2 never asks the same dimension twice in a row — the
        // interleaving requirement of §6.2 applies to the test as well.
        Dimension previous = anchors.back();
        for (int i = 0; i < 12; ++i) {
            const Dimension dimension = placement.nextDimension();
            CHECK(dimension != previous);
            placement.record(dimension, placement.nextDifficulty(), i % 3 != 0);
            previous = dimension;
        }
        CHECK(placement.answered() == 18);
        // K falls as the session goes on: the estimate settles down.
        CHECK(placement.seElo() < 200.0);
    }

    // Stopping rule §4.7: never more than 25 items, never past 14 minutes.
    {
        Learner learner;
        Placement placement;
        int items = 0;
        while (!placement.finished(0.0) && items < 100) {
            placement.record(placement.nextDimension(), placement.nextDifficulty(), true);
            ++items;
        }
        CHECK(items <= 25);
        Placement timeBoxed;
        CHECK(timeBoxed.finished(14.0 * 60.0));
        CHECK(!timeBoxed.finished(60.0));
        (void)learner;
    }

    // --- convergence on simulated learners ---------------------------------
    // The estimator starts at 1000 and has 25 items; it must move decisively
    // towards the truth and end up close to it in the band the app addresses.
    {
        const double truths[] = { 700.0, 900.0, 1000.0, 1100.0, 1300.0, 1400.0 };
        double previous = -1e9;
        for (double truth : truths) {
            const double estimate = meanEstimate(truth, 200, rng);
            std::printf("  wahres theta %6.0f -> geschaetzt %7.1f (Abweichung %+6.1f)\n",
                        truth, estimate, estimate - truth);
            // Monotone in the truth: a stronger learner must not score lower.
            CHECK(estimate > previous);
            previous = estimate;
            // The estimator has to get within the honest accuracy of a 25-item
            // test; §4.6 puts that at about +-80 Elo. The walk from the 1000
            // default costs more the further away the truth is — 25 items with
            // a decaying K simply cannot cross 500 points, which is exactly why
            // §4.7 lets the learner type in a club rating instead.
            const double allowed = 80.0 + 0.35 * std::fabs(truth - 1000.0);
            CHECK(std::fabs(estimate - truth) < allowed);
        }
    }

    // A self-reported club rating is used as the starting point, which is
    // worth a lot when the learner is far from the default of 1000.
    {
        Learner strong;
        strong.trueTheta = 1700.0;
        double withHint = 0.0, without = 0.0;
        for (int i = 0; i < 200; ++i) {
            withHint += runSession(strong, rng, 1700).theta;
            without += runSession(strong, rng).theta;
        }
        withHint /= 200.0;
        without /= 200.0;
        std::printf("  Selbstangabe 1700: mit %7.1f, ohne %7.1f\n", withHint, without);
        CHECK(std::fabs(withHint - 1700.0) < std::fabs(without - 1700.0));
        CHECK(std::fabs(withHint - 1700.0) < 100.0);
    }

    // --- the honest verdict, §4.6 ------------------------------------------
    {
        // A learner with no weakness: normally *no* dimension stands out, and
        // saying so is the honest answer. Anything above a small share here
        // would mean the app sells noise as diagnosis.
        int remarkable = 0;
        const int runs = 300;
        for (int i = 0; i < runs; ++i) {
            Learner flat;
            flat.trueTheta = 1100.0;
            const PlacementPlan plan = runSession(flat, rng);
            for (int d = 0; d < kDimensionCount; ++d) {
                if (plan.ordinal[static_cast<std::size_t>(d)] != Ordinal::Unremarkable)
                    ++remarkable;
            }
            CHECK(plan.items <= 25);
            CHECK(plan.startDifficulty < plan.theta);
            CHECK(plan.firstAreas.size() <= 2);
        }
        const double perRun = static_cast<double>(remarkable) / runs;
        std::printf("  ohne echte Schwaeche: %.2f auffaellige Dimensionen je Test\n", perRun);
        CHECK(perRun < 1.5);
    }
    {
        // A learner who really is much weaker in one dimension. With four items
        // per dimension the shrunken deviation almost never crosses one
        // standard error, and that is the specification working as intended
        // (§4.6): the direction is there, the *verdict* is not, and the app
        // must not pretend otherwise.
        int spotted = 0;
        double weakSum = 0.0;
        double otherSum = 0.0;
        const int runs = 300;
        for (int i = 0; i < runs; ++i) {
            Learner uneven;
            uneven.trueTheta = 1200.0;
            uneven.weakDimension = Dimension::END;
            uneven.weakBy = 500.0;
            const PlacementPlan plan = runSession(uneven, rng);
            const std::size_t weak = static_cast<std::size_t>(Dimension::END);
            if (plan.ordinal[weak] == Ordinal::Weak)
                ++spotted;
            weakSum += plan.thetaD[weak] - plan.theta;
            for (int d = 0; d < kDimensionCount; ++d) {
                if (static_cast<std::size_t>(d) != weak)
                    otherSum += plan.thetaD[static_cast<std::size_t>(d)] - plan.theta;
            }
        }
        const double weakMean = weakSum / runs;
        const double otherMean = otherSum / (runs * (kDimensionCount - 1));
        std::printf("  500 Elo Schwaeche in END: Abweichung %+6.1f gegen %+5.1f sonst, "
                    "%d von %d Tests auffaellig\n", weakMean, otherMean, spotted, runs);
        // The signal is in the right direction and clearly separated …
        CHECK(weakMean < -10.0);
        CHECK(weakMean < otherMean - 10.0);
        // … but the shrinkage keeps it below the noise floor, so the test result
        // stays "unremarkable" in the overwhelming majority of runs.
        CHECK(spotted < runs / 5);
    }

    // §4.9: a weak STL below 1300 is deliberately ignored — positional
    // judgement is not the bottleneck at that strength.
    {
        int stlPlanned = 0;
        for (int i = 0; i < 200; ++i) {
            Learner uneven;
            uneven.trueTheta = 1000.0;
            uneven.weakDimension = Dimension::STL;
            uneven.weakBy = 600.0;
            const PlacementPlan plan = runSession(uneven, rng);
            for (Dimension dimension : plan.firstAreas) {
                if (dimension == Dimension::STL)
                    ++stlPlanned;
            }
        }
        CHECK(stlPlanned == 0);
    }

    std::printf("OK: placement estimator converges on simulated learners\n");
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    return 0;
}
