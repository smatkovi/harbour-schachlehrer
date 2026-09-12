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
#include "Placement.h"

#include <algorithm>
#include <cmath>

namespace schach {
namespace core {

namespace {

// The anchor order of §4.7: one item per dimension, fixed sequence.
const Dimension kAnchorOrder[kDimensionCount] = {
    Dimension::TAK, Dimension::SRG, Dimension::REC,
    Dimension::END, Dimension::STL, Dimension::ERD
};

std::size_t indexOf(Dimension dimension)
{
    const std::size_t index = static_cast<std::size_t>(dimension);
    return index < kDimensionCount ? index : 0;
}

} // namespace

double raschExpectation(double theta, double difficulty)
{
    return 1.0 / (1.0 + std::pow(10.0, (difficulty - theta) / 400.0));
}

Placement::Placement(int selfReportedElo)
{
    m_information.fill(0.0);
    // §4.8: theta starts at the self-reported rating if there is one, else at
    // 1000. The anchor items then sit 100 below it.
    m_state.theta = selfReportedElo > 0 ? static_cast<double>(selfReportedElo) : 1000.0;
}

double Placement::thetaOf(Dimension dimension) const
{
    return core::thetaOf(m_state, dimension);
}

Dimension Placement::nextDimension() const
{
    if (m_answers < kDimensionCount)
        return kAnchorOrder[m_answers];

    // The dimension with the largest remaining uncertainty, but never the same
    // one twice in a row — the interleaving requirement of §6.2.
    Dimension best = kAnchorOrder[0];
    double bestScore = -1.0;
    for (const Dimension dimension : kAnchorOrder) {
        if (static_cast<int>(dimension) == m_lastDimension)
            continue;
        const double score = 1.0 / (m_information[indexOf(dimension)] + 1.0);
        if (score > bestScore) {
            bestScore = score;
            best = dimension;
        }
    }
    return best;
}

double Placement::nextDifficulty() const
{
    if (m_answers < kDimensionCount)
        return m_state.theta - 100.0;   // anchors sit slightly below theta
    return thetaOf(nextDimension()) - kTargetOffset;
}

void Placement::record(Dimension dimension, double itemDifficulty, bool correct,
                       PlacementItem* item)
{
    const std::size_t index = indexOf(dimension);
    const double thetaDim = thetaOf(dimension);
    const double p = raschExpectation(thetaDim, itemDifficulty);
    const double y = correct ? 1.0 : 0.0;

    // §4.4: an uncertainty function instead of a fixed K. b = 0.10 rather than
    // the literature's 0.05, because that value is meant for systems with
    // hundreds of answers and converges far too slowly in a 25-item session.
    const double kTheta = kLogitElo * 0.90 / (1.0 + 0.10 * static_cast<double>(m_answers));

    // §4.8: 60 % of the update works globally, 40 % on the dimension.
    m_state.theta += 0.6 * kTheta * (y - p);
    m_state.delta[index] += 0.4 * kTheta * (y - p);

    if (item && item->selfGenerated) {
        // The item learns along; items move more slowly than people.
        const double kDelta = kLogitElo * 0.35 / (1.0 + 0.04 * static_cast<double>(item->attempts));
        item->difficulty -= kDelta * (y - p);
        item->attempts += 1;
    }

    // Fisher information of the Rasch model, in logit squared.
    const double info = p * (1.0 - p);
    m_totalInformation += info;
    m_information[index] += info;

    m_state.answers[index] += 1;
    m_answers += 1;
    m_lastDimension = static_cast<int>(dimension);
}

double Placement::seLogit() const
{
    if (m_totalInformation <= 0.0)
        return 99.0;
    return 1.0 / std::sqrt(m_totalInformation);
}

double Placement::seElo() const { return kLogitElo * seLogit(); }

bool Placement::finished(double elapsedSeconds) const
{
    if (m_answers >= 25)
        return true;
    if (elapsedSeconds >= 14.0 * 60.0)
        return true;
    if (m_answers >= 20 && seLogit() < 0.45)
        return true;
    return false;
}

PlacementPlan Placement::plan(bool aborted) const
{
    PlacementPlan result;
    result.theta = m_state.theta;
    result.seElo = seElo();
    result.items = m_answers;
    result.aborted = aborted;
    result.startDifficulty = m_state.theta - kTargetOffset;
    result.ordinal.fill(Ordinal::Unremarkable);
    result.thetaD.fill(m_state.theta);

    // §4.6: a dimension counts as remarkable only when its *shrunken*
    // deviation is larger than one standard error. In practice that means
    // none or one dimension stands out, and that is the honest answer.
    struct Scored {
        Dimension dimension;
        double deviation;
    };
    std::vector<Scored> remarkable;
    for (std::size_t i = 0; i < kDimensionCount; ++i) {
        const Dimension dimension = static_cast<Dimension>(i);
        const double deviation = shrink(m_state.answers[i]) * m_state.delta[i];
        result.thetaD[i] = m_state.theta + deviation;
        if (std::fabs(deviation) > result.seElo) {
            result.ordinal[i] = deviation < 0.0 ? Ordinal::Weak : Ordinal::Strong;
            remarkable.push_back({ dimension, deviation });
        }
    }

    // At most two are kept, the ones with the largest absolute deviation.
    std::sort(remarkable.begin(), remarkable.end(), [](const Scored& a, const Scored& b) {
        return std::fabs(a.deviation) > std::fabs(b.deviation);
    });
    if (remarkable.size() > 2)
        remarkable.resize(2);

    for (const Scored& entry : remarkable) {
        if (entry.deviation >= 0.0)
            continue;   // only the weak ones set the training order
        // §4.9: a weak STL below 1300 is ignored — positional judgement is not
        // the bottleneck at that strength.
        if (entry.dimension == Dimension::STL && m_state.theta < 1300.0)
            continue;
        result.firstAreas.push_back(entry.dimension);
    }
    return result;
}

} // namespace core
} // namespace schach
