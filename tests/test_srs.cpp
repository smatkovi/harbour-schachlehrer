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
// The card life cycle of teacher.md §5: FSRS scheduling, the automatic grade,
// the lapse that comes out of a real game, retirement with its transfer proof,
// reactivation, and the session builder of §5.6.
#include "core/Card.h"
#include "core/Srs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

SrsState freshCard(long long today)
{
    SrsState state;
    state.state = CardState::New;
    state.dueDay = today;
    return state;
}

Card libraryCard(const std::string& id, Dimension dimension, long long today)
{
    Card card;
    card.id = id;
    card.dimension = dimension;
    card.origin = CardOrigin::Library;
    card.srs = freshCard(today);
    card.srs.state = CardState::Review;
    card.srs.dueDay = today;
    return card;
}

} // namespace

int main()
{
    const FsrsParams& params = defaultParams();

    // --- the forgetting curve ----------------------------------------------
    // FSRS models forgetting as a power function; at t = S the retrievability
    // is 0.9, which is what "stability" means.
    CHECK(near(retrievability(0.0, 10.0), 1.0, 1e-9));
    CHECK(near(retrievability(10.0, 10.0), 0.9, 0.005));
    CHECK(retrievability(100.0, 10.0) < retrievability(10.0, 10.0));
    // Desired retention 0.85 instead of 0.90 stretches the interval past the
    // stability — the compensation for the changing-instance problem of §5.2.
    CHECK(intervalFor(10.0, 0.85) > 10.0);
    CHECK(near(intervalFor(10.0, 0.90), 10.0, 0.2));
    CHECK(intervalFor(10.0, 0.85) > intervalFor(10.0, 0.90));
    CHECK(near(params.desiredRetention, 0.85, 1e-9));

    // --- the automatic grade, §5.1 -----------------------------------------
    CHECK(ratingFor(false, false, 5000) == Rating::Again);
    CHECK(ratingFor(false, true, 60000) == Rating::Again);
    CHECK(ratingFor(true, true, 5000) == Rating::Hard);     // solved with a hint
    CHECK(ratingFor(true, false, 46000) == Rating::Hard);
    CHECK(ratingFor(true, false, 20000) == Rating::Good);
    CHECK(ratingFor(true, false, 9000) == Rating::Easy);

    // --- Neu -> Lernen -> Wiederholung -------------------------------------
    long long day = 1000;
    SrsState card = freshCard(day);
    CHECK(card.state == CardState::New);
    CHECK(isDue(card, day));

    card = applyReview(card, Rating::Good, day);
    CHECK(card.state == CardState::Review);
    CHECK(card.reps == 1);
    CHECK(card.lapses == 0);
    CHECK(card.consecutiveCorrect == 1);
    CHECK(near(card.stability, params.w[2], 1e-9));     // S0(Good)
    CHECK(card.intervalDays >= 1);
    CHECK(card.dueDay == day + card.intervalDays);
    CHECK(!isDue(card, day));
    CHECK(isDue(card, card.dueDay));

    // Answering on time makes the interval grow, over and over.
    int previousInterval = card.intervalDays;
    for (int i = 0; i < 5; ++i) {
        day = card.dueDay;
        card = applyReview(card, Rating::Good, day);
        CHECK(card.intervalDays > previousInterval);
        CHECK(card.state == CardState::Review);
        previousInterval = card.intervalDays;
    }
    CHECK(card.consecutiveCorrect == 6);

    // An easy answer stretches it further than a good one, a hard one less.
    {
        const long long when = card.dueDay;
        const SrsState easy = applyReview(card, Rating::Easy, when);
        const SrsState good = applyReview(card, Rating::Good, when);
        const SrsState hard = applyReview(card, Rating::Hard, when);
        CHECK(easy.intervalDays > good.intervalDays);
        CHECK(hard.intervalDays < good.intervalDays);
        CHECK(easy.difficulty < good.difficulty);
        CHECK(hard.difficulty > good.difficulty);
    }

    // --- Fehlversuch: zurück nach Lernen -----------------------------------
    {
        const double stabilityBefore = card.stability;
        const long long when = card.dueDay;
        const SrsState failed = applyReview(card, Rating::Again, when);
        CHECK(failed.state == CardState::Learning);
        CHECK(failed.lapses == card.lapses + 1);
        CHECK(failed.consecutiveCorrect == 0);
        CHECK(failed.stability <= stabilityBefore);   // a lapse never raises S
        CHECK(failed.intervalDays == 1);
        CHECK(failed.dueDay == when + 1);
        // and back up again
        const SrsState recovered = applyReview(failed, Rating::Good, failed.dueDay);
        CHECK(recovered.state == CardState::Review);
        CHECK(recovered.consecutiveCorrect == 1);
    }

    // --- §5.4: the game is the retrieval test ------------------------------
    {
        const long long when = card.dueDay - 3;   // still in the future
        const SrsState hit = applyGameError(card, when);
        CHECK(hit.state == CardState::Learning);
        CHECK(hit.lapses == card.lapses + 1);
        CHECK(hit.dueDay == when);          // due now, not tomorrow
        CHECK(hit.intervalDays == 0);
        CHECK(isDue(hit, when));
    }

    // --- Ruhestand ---------------------------------------------------------
    {
        SrsState mature = freshCard(2000);
        long long when = 2000;
        mature = applyReview(mature, Rating::Easy, when);
        for (int i = 0; i < 60 && (mature.stability < 180.0 || mature.consecutiveCorrect < 4); ++i) {
            when = mature.dueDay;
            mature = applyReview(mature, Rating::Easy, when);
        }
        CHECK(mature.stability >= 180.0);
        CHECK(mature.consecutiveCorrect >= 4);
        CHECK(mature.state == CardState::Review);

        // Condition (3) is the point of the whole loop: the pattern must have
        // survived in real games, not only in the drill.
        CHECK(!retirementReached(mature, 0, 100, 100));
        CHECK(!retirementReached(mature, 1, 100, 100));
        CHECK(retirementReached(mature, 2, 100, 100));
        // The escape hatch for rare patterns (§5.5 [RISIKO]).
        CHECK(retirementReached(mature, 0, 400, 400));
        // The other two conditions still have to hold.
        {
            SrsState young = mature;
            young.stability = 90.0;
            CHECK(!retirementReached(young, 5, 400, 400));
            SrsState shaky = mature;
            shaky.consecutiveCorrect = 2;
            CHECK(!retirementReached(shaky, 5, 400, 400));
        }

        // Reactivation is not a fresh start: it is no total loss.
        SrsState retired = mature;
        retired.state = CardState::Retired;
        CHECK(!isDue(retired, 99999));
        const SrsState back = reactivate(retired, 3000);
        CHECK(back.state == CardState::Learning);
        CHECK(back.stability <= 21.0);
        CHECK(near(back.stability, std::min(mature.stability / 3.0, 21.0), 1e-9));
        CHECK(back.dueDay == 3000);
        CHECK(back.lapses == mature.lapses + 1);
        // An error of the class in a real game does exactly the same.
        const SrsState viaGame = applyGameError(retired, 3000);
        CHECK(viaGame.state == CardState::Learning);
        CHECK(near(viaGame.stability, back.stability, 1e-9));
    }

    // --- cards out of findings, §5.4 ---------------------------------------
    {
        Finding finding;
        finding.cls = ErrorClass::A3;
        finding.dimension = dimensionOf(ErrorClass::A3);
        finding.motif = Motif::Fork;
        finding.dW = 31.0;
        finding.fen = "4k3/8/8/1n6/4R3/8/R6P/4K3 w - - 0 1";
        finding.bestMove = "e4e5";

        Card made = cardFromFinding(finding, 5000);
        CHECK(made.id == "TAK/gabel/A3");
        CHECK(made.dimension == Dimension::TAK);
        CHECK(made.origin == CardOrigin::OwnError);
        CHECK(made.priority > 0);                  // ahead of library cards
        CHECK(made.srs.state == CardState::New);
        CHECK(made.srs.dueDay == 5000);
        CHECK(made.shownInstances.size() == 1);
        CHECK(!made.title.empty());

        // The same class showing up again in a later game.
        made.srs = applyReview(made.srs, Rating::Good, 5000);
        CHECK(made.srs.state == CardState::Review);
        Finding again = finding;
        again.fen = "4k3/8/8/1n6/4R3/8/R7/4K2P w - - 0 1";
        CHECK(applyFinding(made, again, 5100));
        CHECK(made.srs.dueDay == 5100);
        CHECK(made.srs.state == CardState::Learning);
        CHECK(made.shownInstances.size() == 2);
        CHECK(near(made.queuedMass, 62.0, 1e-9));

        // Ids are stable and do not depend on the enum numbering.
        CHECK(cardIdFor(ErrorClass::C4, Motif::None) == "REC/still/C4");
        CHECK(cardIdFor(ErrorClass::F6, Motif::None) == "END/letzter-abtausch/F6");
    }

    // --- the daily session, §5.6 -------------------------------------------
    {
        std::vector<Card> due;
        due.push_back(libraryCard("TAK/gabel/A3", Dimension::TAK, 7000));
        due.push_back(libraryCard("TAK/fesselung/A4", Dimension::TAK, 7000));
        due.push_back(libraryCard("SRG/haenger/A1", Dimension::SRG, 7000));
        due.push_back(libraryCard("REC/abtausch/A2", Dimension::REC, 7000));
        due.push_back(libraryCard("END/opposition/F1", Dimension::END, 7000));
        due.push_back(libraryCard("SRG/drohung/B1", Dimension::SRG, 7000));

        const SessionPlan plan = buildSession(due, "TAK/spiess/A5", Dimension::TAK, 0, 3);
        CHECK(!plan.blockA.empty());
        CHECK(plan.blockA.size() <= due.size());
        // Interleaved: never two in a row from the same dimension while the
        // pool still allows it (§6.2). Contrast needs temporal proximity, which
        // is why this is one block and not a stream over the day.
        for (std::size_t i = 1; i < plan.blockA.size(); ++i)
            CHECK(plan.blockA[i].dimension != plan.blockA[i - 1].dimension);
        // Block B is exactly one pattern, blocked, and it ends on the
        // counter-example — that step is not negotiable (§5.6).
        CHECK(plan.blockB.size() == 5);
        CHECK(plan.blockB.front().kind == SessionItem::Kind::Example);
        CHECK(plan.blockB.back().kind == SessionItem::Kind::CounterExample);
        for (const SessionItem& item : plan.blockB)
            CHECK(item.cardId == "TAK/spiess/A5");
        CHECK(!plan.newPatternSkipped);
        CHECK(plan.sparringSeconds >= 300);

        // The most important rule of the algorithm: with a backlog of more
        // than 25 cards, no new pattern at all.
        const SessionPlan buried = buildSession(due, "TAK/spiess/A5", Dimension::TAK, 0, 40);
        CHECK(buried.blockB.empty());
        CHECK(buried.newPatternSkipped);
        CHECK(buried.sparringSeconds > plan.sparringSeconds);
        // One new pattern a day, not two.
        const SessionPlan already = buildSession(due, "TAK/spiess/A5", Dimension::TAK, 1, 0);
        CHECK(already.blockB.empty());
        // A short session drops block B, not block C (§5.6).
        const SessionPlan shortDay = buildSession(due, std::string(), Dimension::TAK, 0, 0, 480);
        CHECK(shortDay.blockB.empty());
        CHECK(shortDay.sparringSeconds > 0);
    }

    std::printf("OK: FSRS scheduling, card life cycle and session builder\n");
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    return 0;
}
