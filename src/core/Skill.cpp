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
#include "Skill.h"

#include <algorithm>
#include <cmath>

namespace schach {
namespace core {

namespace {

struct DimensionRow {
    Dimension dimension;
    const char* key;
    const char* name;
    const char* question;
};

const DimensionRow kDimensions[kDimensionCount] = {
    { Dimension::TAK, "TAK", "Taktik", "Erkennst du ein Motiv, wenn es da ist?" },
    { Dimension::SRG, "SRG", "Sorgfalt", "Prüfst du vor dem Zug, ob etwas hängt oder droht?" },
    { Dimension::REC, "REC", "Rechnen", "Kannst du eine Folge korrekt bis zum Ende rechnen?" },
    { Dimension::END, "END", "Endspiel", "Verwertest du, was du erreicht hast?" },
    { Dimension::STL, "STL", "Stellungsurteil", "Weißt du, was in einer ruhigen Stellung zu tun ist?" },
    { Dimension::ERD, "ERD", "Eröffnung", "Kommst du ohne Schaden aus den ersten zwölf Zügen?" },
};

std::size_t indexOf(Dimension dimension)
{
    const std::size_t index = static_cast<std::size_t>(dimension);
    return index < kDimensionCount ? index : 0;
}

} // namespace

const char* dimensionKey(Dimension dimension) { return kDimensions[indexOf(dimension)].key; }
const char* dimensionName(Dimension dimension) { return kDimensions[indexOf(dimension)].name; }
const char* dimensionQuestion(Dimension dimension) { return kDimensions[indexOf(dimension)].question; }

Dimension dimensionFromKey(const std::string& key)
{
    for (const DimensionRow& row : kDimensions) {
        if (key == row.key)
            return row.dimension;
    }
    return Dimension::SRG;
}

const char* ordinalName(Ordinal ordinal)
{
    switch (ordinal) {
    case Ordinal::Weak: return "auffällig schwach";
    case Ordinal::Strong: return "auffällig stark";
    case Ordinal::Unremarkable: break;
    }
    return "unauffällig";
}

SkillState::SkillState()
{
    delta.fill(0.0);
    answers.fill(0);
    mass.fill(0.0);
    events.fill(0);
}

double shrink(int answers)
{
    if (answers <= 0)
        return 0.0;
    return static_cast<double>(answers) / (static_cast<double>(answers) + 8.0);
}

double thetaOf(const SkillState& state, Dimension dimension)
{
    const std::size_t index = indexOf(dimension);
    return state.theta + shrink(state.answers[index]) * state.delta[index];
}

void addEvent(SkillState& state, const Finding& finding)
{
    const std::size_t index = indexOf(finding.dimension);
    // Unfiltered by L on purpose (§3.2 b): a_d is a ratio and the filter would
    // bend it. Only the suppression rules U1-U6 have already applied.
    state.mass[index] += std::max(0.0, finding.dW);
    state.events[index] += 1;
    if (finding.dW >= 18.0)
        state.blunders += 1;
}

std::array<double, kDimensionCount> shares(const SkillState& state)
{
    std::array<double, kDimensionCount> out{};
    out.fill(0.0);
    double total = 0.0;
    for (std::size_t i = 0; i < kDimensionCount; ++i)
        total += state.mass[i];
    if (total <= 0.0)
        return out;
    for (std::size_t i = 0; i < kDimensionCount; ++i)
        out[i] = state.mass[i] / total;
    return out;
}

double blunderRate(const SkillState& state)
{
    if (state.ownMoves <= 0)
        return 0.0;
    return 100.0 * static_cast<double>(state.blunders) / static_cast<double>(state.ownMoves);
}

std::vector<Dimension> byShare(const SkillState& state)
{
    const std::array<double, kDimensionCount> a = shares(state);
    std::vector<Dimension> out;
    out.reserve(kDimensionCount);
    for (const DimensionRow& row : kDimensions)
        out.push_back(row.dimension);
    std::stable_sort(out.begin(), out.end(), [&a](Dimension lhs, Dimension rhs) {
        return a[indexOf(lhs)] > a[indexOf(rhs)];
    });
    return out;
}

ClusterResult clusterTest(int k, int n, double p, double z)
{
    ClusterResult result;
    result.k = k;
    result.n = n;
    result.p = p;
    if (n < 30 || p <= 0.0 || p >= 1.0)
        return result;   // teacher.md §3.4: below 30 events a single case says nothing
    const double observed = static_cast<double>(k) / static_cast<double>(n);
    const double sd = std::sqrt(p * (1.0 - p) / static_cast<double>(n));
    result.excess = observed - p;
    result.margin = observed - (p + z * sd);
    result.significant = result.margin > 0.0;
    return result;
}

ClusterCandidate strongestCluster(const std::vector<ClusterCandidate>& candidates)
{
    ClusterCandidate best;
    for (const ClusterCandidate& candidate : candidates) {
        if (!candidate.result.significant)
            continue;
        if (best.cls == ErrorClass::None || candidate.result.margin > best.result.margin)
            best = candidate;
    }
    return best;
}

bool motifSuspect(int motifCount, int totalEvents)
{
    if (totalEvents <= 0)
        return false;
    const double share = static_cast<double>(motifCount) / static_cast<double>(totalEvents);
    return share < 0.005 || share > 0.25;
}

} // namespace core
} // namespace schach
