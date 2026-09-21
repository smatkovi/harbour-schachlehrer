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
#ifndef SCHACH_CORE_CARD_H
#define SCHACH_CORE_CARD_H

// teacher.md §5.2 — a card is a pair (pattern, dimension). Not a position, not
// a move, not a puzzle. Every repetition draws a *fresh* position that fits
// the pattern, which is the whole difference to Woodpecker and to any puzzle
// book: there, the same puzzle comes back and one cannot tell whether the
// learner knows the pattern or remembers the puzzle.

#include "Srs.h"
#include "Taxonomy.h"

#include <string>
#include <vector>

namespace schach {
namespace core {

enum class CardOrigin : std::uint8_t {
    Library = 0,   // the ~120 shipped patterns (§5.3)
    OwnError,      // produced from the learner's own game (§5.4)
    OpeningTrap,   // H4
    BlindSpot      // a cluster class (E2–E4, G2) reported as a pattern (§3.4)
};

struct Card {
    // The stable id is the pattern path, e.g. "TAK/gabel/springer-koenig-turm".
    std::string id;
    Dimension dimension = Dimension::TAK;
    std::string pattern;       // slug, second path element
    std::string title;         // German, shown after solving, never before (§6.2)
    ErrorClass errorClass = ErrorClass::None;
    Motif motif = Motif::None;
    CardOrigin origin = CardOrigin::Library;

    // The first instance is the learner's own position; further repetitions
    // draw new ones.
    std::string seedFen;
    std::string solutionUci;                  // the first move, always line[0]
    // The whole answer (teacher.md §6.5), UCI, learner first and alternating.
    // One entry for a one-move card; the task does not say which it is, that
    // would be naming the kind of task (§6.2). The learner sees how many moves
    // are left, not what sort of thing they are looking for.
    std::vector<std::string> solutionLine;
    long long originGameId = -1;

    SrsState srs;
    int priority = 0;          // higher goes first; own errors beat library cards
    int transferSightings = 0; // §5.5 condition (3)
    long long createdDay = 0;
    long long lastOccurrenceDay = -1;
    double queuedMass = 0.0;   // sum of dW of its class, orders the waiting queue
    std::vector<std::string> shownInstances;

    bool retired() const { return srs.state == CardState::Retired; }

    // The two spellings kept in step: whichever was filled in fills the other.
    // Old rows in the database have only `solutionUci`, and a card built from
    // a finding has only that too — one move is a line of length one.
    void normaliseSolution();
};

// The stable card id for an error class plus motif (§5.2 table).
std::string cardIdFor(ErrorClass cls, Motif motif);
// A German pattern title for the same pair — shown *after* solving (§6.2).
std::string cardTitleFor(ErrorClass cls, Motif motif);

// §5.4: turn one diagnosed error into a card, or update the one that exists.
Card cardFromFinding(const Finding& finding, long long today);
// Apply one finding to an existing card; returns true when the card changed.
bool applyFinding(Card& card, const Finding& finding, long long today);

// teacher.md §5.4 [EMPFEHLUNG]: at most two freshly made error cards become
// active per day; the rest wait, sorted by the accumulated dW of their class,
// so that one bad game cannot flood the plan.
constexpr int kMaxNewErrorCardsPerDay = 2;
// §5.8: exactly one new pattern per day, and none at all with a backlog.
constexpr int kMaxBacklogForNewPattern = 25;

// One entry of the daily session (§5.6).
struct SessionItem {
    enum class Kind : std::uint8_t { Review = 0, Example, Instance, CounterExample, Sparring };
    Kind kind = Kind::Review;
    std::string cardId;
    Dimension dimension = Dimension::TAK;
    int estimatedSeconds = 30;
};

struct SessionPlan {
    std::vector<SessionItem> blockA;    // due cards, interleaved
    std::vector<SessionItem> blockB;    // exactly one new pattern, blocked
    int sparringSeconds = 300;          // block C
    bool newPatternSkipped = false;     // backlog too large
};

// §5.6 Füllalgorithmus. `due` must already be sorted by (priority desc,
// overdueness desc); `newPattern` is the id of the pattern block B would
// introduce, empty when there is none.
SessionPlan buildSession(const std::vector<Card>& due, const std::string& newPattern,
                         Dimension newPatternDimension, int newCardsToday, int backlog,
                         int budgetSeconds = 900);

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_CARD_H
