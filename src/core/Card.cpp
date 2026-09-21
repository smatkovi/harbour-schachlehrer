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
#include "Card.h"
#include "Skill.h"

#include <algorithm>

namespace schach {
namespace core {

namespace {

// The second path element: the motif when there is one, otherwise the class'
// own slug, so that every class lands in a readable place.
std::string patternSlug(ErrorClass cls, Motif motif)
{
    if (motif != Motif::None)
        return motifKey(motif);
    switch (cls) {
    case ErrorClass::A1: return "haenger";
    case ErrorClass::A2:
    case ErrorClass::E1: return "abtausch";
    case ErrorClass::A9: return "zwischenzug";
    case ErrorClass::B1: return "drohung";
    case ErrorClass::B2:
    case ErrorClass::B2n: return "matt";
    case ErrorClass::B3: return "grundreihe";
    case ErrorClass::B4: return "koenigsangriff";
    case ErrorClass::B5:
    case ErrorClass::F3: return "quadratregel";
    case ErrorClass::C1: return "matt";
    case ErrorClass::C2: return "haenger";
    case ErrorClass::C4: return "still";
    case ErrorClass::C5: return "verteidigung";
    case ErrorClass::C6: return "verwertung";
    case ErrorClass::D1: return "rochade";
    case ErrorClass::D2: return "koenigsschild";
    case ErrorClass::D3: return "damenausflug";
    case ErrorClass::E2: return "rueckwaerts";
    case ErrorClass::E3: return "lange-linie";
    case ErrorClass::E4: return "rand";
    case ErrorClass::E5: return "zugfolge";
    case ErrorClass::E6: return "rechentiefe";
    case ErrorClass::F1: return "opposition";
    case ErrorClass::F2: return "schluesselfeld";
    case ErrorClass::F4: return "koenigsaktivierung";
    case ErrorClass::F5: return "turm-hinter-freibauer";
    case ErrorClass::F6: return "letzter-abtausch";
    case ErrorClass::F7: return "remis-halten";
    case ErrorClass::F8: return "falscher-laeufer";
    case ErrorClass::G1: return "bauernstruktur";
    case ErrorClass::G2: return "zugzweck";
    case ErrorClass::G3: return "figurenqualitaet";
    case ErrorClass::G4: return "feldschwaeche";
    case ErrorClass::G5: return "offene-linie";
    case ErrorClass::H1: return "figur-mehrfach";
    case ErrorClass::H2: return "fluegelbauern";
    case ErrorClass::H3: return "entwicklung";
    case ErrorClass::H4: return "falle";
    case ErrorClass::I1: return "kritischer-moment";
    default: break;
    }
    return "sonstiges";
}

} // namespace

void Card::normaliseSolution()
{
    if (solutionLine.empty() && !solutionUci.empty())
        solutionLine.push_back(solutionUci);
    else if (solutionUci.empty() && !solutionLine.empty())
        solutionUci = solutionLine.front();
}

std::string cardIdFor(ErrorClass cls, Motif motif)
{
    std::string id = dimensionKey(dimensionOf(cls));
    id += '/';
    id += patternSlug(cls, motif);
    id += '/';
    id += errorKey(cls);
    return id;
}

std::string cardTitleFor(ErrorClass cls, Motif motif)
{
    std::string title = errorName(cls);
    if (motif != Motif::None) {
        title += " (";
        // motifName() reads "eine Gabel"; for a title the bare noun is better.
        const std::string full = motifName(motif);
        const std::size_t space = full.find(' ');
        title += space == std::string::npos ? full : full.substr(space + 1);
        title += ')';
    }
    return title;
}

Card cardFromFinding(const Finding& finding, long long today)
{
    Card card;
    card.errorClass = finding.cls;
    card.motif = finding.motif;
    card.dimension = finding.dimension;
    card.id = cardIdFor(finding.cls, finding.motif);
    card.pattern = patternSlug(finding.cls, finding.motif);
    card.title = cardTitleFor(finding.cls, finding.motif);
    card.seedFen = finding.fen;
    card.solutionUci = finding.bestMove;
    card.origin = finding.cls == ErrorClass::H4 ? CardOrigin::OpeningTrap
                : isClusterClass(finding.cls) ? CardOrigin::BlindSpot
                                              : CardOrigin::OwnError;
    // §5.4: a card born from one's own mistake outranks a library card.
    card.priority = 10;
    card.createdDay = today;
    card.lastOccurrenceDay = today;
    card.queuedMass = finding.dW;
    card.srs.state = CardState::New;
    card.srs.dueDay = today;
    card.shownInstances.push_back(finding.fen);
    return card;
}

bool applyFinding(Card& card, const Finding& finding, long long today)
{
    card.queuedMass += finding.dW;
    card.lastOccurrenceDay = today;
    // The learner's own position becomes the first instance of the card.
    if (std::find(card.shownInstances.begin(), card.shownInstances.end(), finding.fen)
        == card.shownInstances.end())
        card.shownInstances.push_back(finding.fen);

    switch (card.srs.state) {
    case CardState::Review:
        card.srs = applyGameError(card.srs, today);
        return true;
    case CardState::Retired:
        card.srs = reactivate(card.srs, today);
        return true;
    case CardState::New:
    case CardState::Learning:
        card.srs.dueDay = today;
        return true;
    }
    return false;
}

SessionPlan buildSession(const std::vector<Card>& due, const std::string& newPattern,
                         Dimension newPatternDimension, int newCardsToday, int backlog,
                         int budgetSeconds)
{
    SessionPlan plan;
    // §5.6: six minutes of review, four of new material, five of play. The
    // proportions follow the budget, not the other way round.
    const int blockASeconds = budgetSeconds * 360 / 900;
    const int blockBSeconds = budgetSeconds * 240 / 900;
    const int blockCSeconds = budgetSeconds - blockASeconds - blockBSeconds;

    std::vector<const Card*> pool;
    pool.reserve(due.size());
    for (const Card& card : due) {
        if (!card.retired())
            pool.push_back(&card);
    }

    // Block A is interleaved: never two consecutive tasks from the same
    // dimension, and no visible category (§6.2). Interleaving works through
    // contrast, and contrast needs temporal proximity — that is why this is one
    // contiguous block and not a stream spread over the day (§6.1 [BIRNBAUM13]).
    int spent = 0;
    int lastDimension = -1;
    int secondLastDimension = -1;
    while (spent < blockASeconds && !pool.empty()) {
        std::size_t pick = pool.size();
        for (std::size_t i = 0; i < pool.size(); ++i) {
            const int dimension = static_cast<int>(pool[i]->dimension);
            if (dimension != lastDimension && dimension != secondLastDimension) {
                pick = i;
                break;
            }
        }
        if (pick == pool.size())
            pick = 0;   // give the interleaving up rather than the review

        SessionItem item;
        item.kind = SessionItem::Kind::Review;
        item.cardId = pool[pick]->id;
        item.dimension = pool[pick]->dimension;
        item.estimatedSeconds = 30;
        plan.blockA.push_back(item);
        spent += item.estimatedSeconds;
        secondLastDimension = lastDimension;
        lastDimension = static_cast<int>(item.dimension);
        pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(pick));
    }

    int restA = blockASeconds - spent;

    // Block B: exactly one new pattern, deliberately *blocked* — five
    // positions of the same pattern in a row, because what the learner has not
    // yet grasped as a common structure belongs blocked (§6.1, rule after
    // [YAN24]). The counter-example in step 5 is not negotiable.
    if (!newPattern.empty() && newCardsToday < 1 && backlog < kMaxBacklogForNewPattern) {
        const SessionItem::Kind kinds[5] = {
            SessionItem::Kind::Example, SessionItem::Kind::Instance,
            SessionItem::Kind::Instance, SessionItem::Kind::Instance,
            SessionItem::Kind::CounterExample
        };
        for (SessionItem::Kind kind : kinds) {
            SessionItem item;
            item.kind = kind;
            item.cardId = newPattern;
            item.dimension = newPatternDimension;
            item.estimatedSeconds = blockBSeconds / 5;
            plan.blockB.push_back(item);
        }
    } else {
        // The single most important rule of the algorithm: with more than 25
        // cards of backlog, no new pattern. It prevents the state in which
        // every flashcard system dies.
        plan.newPatternSkipped = true;
        restA += blockBSeconds;
    }

    plan.sparringSeconds = blockCSeconds + restA;
    return plan;
}

} // namespace core
} // namespace schach
