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
// The thinking routine of src/core/Routine.{h,cpp} — teacher.md §6.6 (the
// transferable hints that *are* the questions), §7.5 (the drill and its
// fade-out) and §3.3 (which dimension each class belongs to).
//
// Four things are worth a test here, and they are the four ways this can go
// wrong silently:
//
//   1. The catalogue stays short and readable, and says nowhere a number
//      (§6.6: never an evaluation, anywhere).
//   2. The class-to-question mapping is **total**: no error class can be
//      diagnosed that the routine has no question for — otherwise the link
//      back in the feedback would sometimes be empty and the learner would be
//      told about a mistake his routine cannot catch.
//   3. The personalised order really follows the learner's record, and the
//      fade-out really makes the scaffolding disappear.
//   4. The drill schedule advances 3 -> 5 -> 8 -> off after three correct
//      answers in a row and comes back on an A1, B1 or C2 event — exactly as
//      §7.5 writes it, not approximately.
#include "core/Routine.h"
#include "core/Skill.h"
#include "core/Taxonomy.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace schach::core;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        if (++failures > 40)
            std::exit(1);
    }
}
#define CHECK(x) check((x), #x, __LINE__)

// Every class the taxonomy can ever hand out: the 50 rules plus the two
// sub-variants that are counted separately and the Z0 bucket.
std::vector<ErrorClass> everyClass()
{
    std::vector<ErrorClass> out;
    for (int i = 1; i < static_cast<int>(ErrorClass::Count); ++i)
        out.push_back(static_cast<ErrorClass>(i));
    return out;
}

const QuestionStatus* statusOf(const RoutineView& view, const char* id)
{
    for (std::size_t i = 0; i < view.questions.size(); ++i) {
        if (std::string(questionAt(view.questions[i].index).id) == id)
            return &view.questions[i];
    }
    return nullptr;
}

int indexOf(const char* id)
{
    const Question* question = questionById(id);
    return question ? static_cast<int>(question - &questionAt(0)) : -1;
}

// --- 1. the catalogue -------------------------------------------------------

void testCatalogue()
{
    // "Sechs bis zehn, geordnet, ist eine Routine" — a list of twenty is a
    // poster. This is the one number in the whole file that is a judgement,
    // so it is pinned.
    CHECK(questionCount() >= 6);
    CHECK(questionCount() <= 10);

    std::set<std::string> ids;
    std::set<std::string> texts;
    bool sawOpening = false, sawEndgame = false, sawRelease = false, sawOpponent = false;
    std::set<int> dimensions;

    for (int i = 0; i < questionCount(); ++i) {
        const Question& question = questionAt(i);
        CHECK(question.id != nullptr && *question.id != '\0');
        CHECK(question.text != nullptr && *question.text != '\0');
        CHECK(question.source != nullptr && *question.source != '\0');
        CHECK(question.catchCount > 0);
        CHECK(question.catches != nullptr);
        CHECK(ids.insert(question.id).second);
        CHECK(texts.insert(question.text).second);

        // §6.6, without exception and for the routine too: no number, anywhere.
        for (const char* c = question.text; *c; ++c)
            CHECK(!(*c >= '0' && *c <= '9'));

        // German, second person. Every question addresses the learner.
        const std::string text = question.text;
        CHECK(text.find("du") != std::string::npos || text.find("dein") != std::string::npos
              || text.find("dir") != std::string::npos || text.find("dich") != std::string::npos
              || text.find("Zähl") != std::string::npos
              || text.find("frag") != std::string::npos || text.find("Frag") != std::string::npos);

        CHECK(questionById(question.id) == &question);
        dimensions.insert(static_cast<int>(question.dimension));

        sawOpening = sawOpening || question.when == Trigger::InOpening;
        sawEndgame = sawEndgame || question.when == Trigger::InEndgame;
        sawRelease = sawRelease || question.when == Trigger::BeforeRelease;
        sawOpponent = sawOpponent || question.when == Trigger::AfterOpponentMove;

        CHECK(*triggerKey(question.when) != '\0');
        CHECK(*triggerName(question.when) != '\0');
    }

    // The moments the routine is built around all exist.
    CHECK(sawOpening);
    CHECK(sawEndgame);
    CHECK(sawRelease);
    CHECK(sawOpponent);
    // All six dimensions of §3.1 are served by at least one question.
    CHECK(dimensions.size() == static_cast<std::size_t>(kDimensionCount));

    CHECK(questionById("gibt-es-nicht") == nullptr);
}

// --- 2. the mapping is total ------------------------------------------------

void testMappingIsTotal()
{
    const std::vector<ErrorClass> all = everyClass();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const ErrorClass cls = all[i];
        const int index = questionIndexFor(cls);
        if (index < 0)
            std::fprintf(stderr, "no question for class %s\n", errorKey(cls));
        CHECK(index >= 0);
        CHECK(index < questionCount());
        const Question* question = questionFor(cls);
        CHECK(question != nullptr);
        CHECK(question == &questionAt(index));
        CHECK(questionCatches(index, cls));
    }
    // The 50 detection rules of §2.6 in particular, by their stable keys.
    for (int i = 0; i < kClassCount; ++i)
        CHECK(questionFor(kAllClasses[i]) != nullptr);
    // The two separately counted sub-variants and the fallback bucket.
    CHECK(questionFor(ErrorClass::A3k) != nullptr);
    CHECK(questionFor(ErrorClass::B2n) != nullptr);
    CHECK(questionFor(ErrorClass::Z0) != nullptr);
    // …and nothing for the two non-classes.
    CHECK(questionIndexFor(ErrorClass::None) == -1);
    CHECK(questionIndexFor(ErrorClass::Count) == -1);

    // Every class named in a question's list is a class the taxonomy knows.
    for (int i = 0; i < questionCount(); ++i) {
        const Question& question = questionAt(i);
        for (int c = 0; c < question.catchCount; ++c) {
            const ErrorClass cls = question.catches[c];
            CHECK(cls != ErrorClass::None && cls != ErrorClass::Count);
            CHECK(*errorKey(cls) != '\0');
        }
    }

    // The ones §6.6 writes out by hand land where the hint stands (this is the
    // whole point: the questions are not a parallel system).
    CHECK(std::string(questionFor(ErrorClass::A1)->id) == "frage-ungedeckt");
    CHECK(std::string(questionFor(ErrorClass::A2)->id) == "frage-abtausch");
    CHECK(std::string(questionFor(ErrorClass::A9)->id) == "frage-abtausch");
    CHECK(std::string(questionFor(ErrorClass::B1)->id) == "frage-drohung");
    CHECK(std::string(questionFor(ErrorClass::B3)->id) == "frage-koenig");
    CHECK(std::string(questionFor(ErrorClass::C2)->id) == "frage-freie-figur");
    CHECK(std::string(questionFor(ErrorClass::C4)->id) == "frage-stiller-zug");
    CHECK(std::string(questionFor(ErrorClass::F6)->id) == "frage-endspiel");
    CHECK(std::string(questionFor(ErrorClass::A3)->id) == "frage-schach-schlag");

    // Where §6.6 has no hint, the question says so instead of pretending.
    CHECK(questionFor(ErrorClass::H1)->fromSpec == false);
    CHECK(questionFor(ErrorClass::I1)->fromSpec == false);
    CHECK(questionFor(ErrorClass::A1)->fromSpec == true);
}

// --- 3. the personalised order ---------------------------------------------

void testEmptyRecord()
{
    // No record: the whole routine, in the order of the catalogue. Nothing is
    // faded, because an empty record is not evidence of anything.
    const ErrorHistory history;
    const RoutineView view = personalise(history, Phase::Middlegame);
    CHECK(!view.questions.empty());
    CHECK(view.visibleCount == static_cast<int>(view.questions.size()));
    for (std::size_t i = 0; i < view.questions.size(); ++i) {
        CHECK(view.questions[i].standing == Standing::Active);
        CHECK(view.questions[i].hits == 0);
        CHECK(view.questions[i].weight == 0.0);
        CHECK(!view.questions[i].caughtLatest);
        if (i > 0)
            CHECK(view.questions[i - 1].index < view.questions[i].index);
    }
}

void testPhaseFilter()
{
    const ErrorHistory history;
    const RoutineView middlegame = personalise(history, Phase::Middlegame);
    const RoutineView opening = personalise(history, Phase::Opening);
    const RoutineView endgame = personalise(history, Phase::Endgame);

    CHECK(statusOf(middlegame, "frage-eroeffnung") == nullptr);
    CHECK(statusOf(middlegame, "frage-endspiel") == nullptr);
    CHECK(statusOf(opening, "frage-eroeffnung") != nullptr);
    CHECK(statusOf(opening, "frage-endspiel") == nullptr);
    CHECK(statusOf(endgame, "frage-endspiel") != nullptr);
    CHECK(statusOf(endgame, "frage-eroeffnung") == nullptr);
    // The phase-bound pair excludes itself, so the list is never all ten.
    CHECK(static_cast<int>(middlegame.questions.size()) == questionCount() - 2);
    CHECK(static_cast<int>(opening.questions.size()) == questionCount() - 1);
    CHECK(static_cast<int>(endgame.questions.size()) == questionCount() - 1);
}

void testOrderFollowsTheRecord()
{
    // A learner who hangs pieces: twelve fresh A1 events and nothing else.
    ErrorHistory history;
    for (int i = 0; i < 12; ++i)
        history.add(ErrorClass::A1, i % 5);

    RoutineView view = personalise(history, Phase::Middlegame);
    CHECK(!view.questions.empty());
    CHECK(std::string(questionAt(view.questions.front().index).id) == "frage-ungedeckt");
    CHECK(view.questions.front().hits == 12);
    CHECK(view.questions.front().standing == Standing::Active);
    CHECK(view.questions.front().caughtLatest);
    CHECK(rankOf(view, indexOf("frage-ungedeckt")) == 1);

    // Everything he has never committed has retired by now (he is past the
    // settling window), so the poster has shrunk to the one question he needs.
    CHECK(view.visibleCount == 1);
    const QuestionStatus* quiet = statusOf(view, "frage-stiller-zug");
    CHECK(quiet != nullptr && quiet->standing == Standing::Retired);

    // He starts missing threats, and more often than he hangs pieces. The
    // threat question overtakes; the old one stays, it does not vanish.
    for (int i = 0; i < 20; ++i)
        history.add(ErrorClass::B1, 0);
    view = personalise(history, Phase::Middlegame);
    CHECK(std::string(questionAt(view.questions.front().index).id) == "frage-drohung");
    CHECK(std::string(questionAt(view.questions[1].index).id) == "frage-ungedeckt");
    CHECK(view.questions[0].weight > view.questions[1].weight);
    CHECK(view.visibleCount == 2);
    CHECK(rankOf(view, indexOf("frage-drohung")) == 1);
    CHECK(rankOf(view, indexOf("frage-ungedeckt")) == 2);
    CHECK(rankOf(view, indexOf("frage-endspiel")) == 0);   // not in this view at all
}

void testFadeOut()
{
    // Enough events that the personalisation is switched on at all, all of
    // them old: one class from six weeks ago, one from half a year ago.
    ErrorHistory history;
    for (int i = 0; i < 12; ++i)
        history.add(ErrorClass::C4, kFadeDays + 10);     // fading
    for (int i = 0; i < 12; ++i)
        history.add(ErrorClass::A2, kRetireDays + 10);   // retired
    for (int i = 0; i < 3; ++i)
        history.add(ErrorClass::B1, 1);                  // active

    const RoutineView view = personalise(history, Phase::Middlegame);
    const QuestionStatus* active = statusOf(view, "frage-drohung");
    const QuestionStatus* fading = statusOf(view, "frage-stiller-zug");
    const QuestionStatus* retired = statusOf(view, "frage-abtausch");
    CHECK(active != nullptr && active->standing == Standing::Active);
    CHECK(fading != nullptr && fading->standing == Standing::Fading);
    CHECK(retired != nullptr && retired->standing == Standing::Retired);
    CHECK(fading->hits == 12);
    CHECK(retired->hits == 12);   // it is remembered, it is just not shown

    // Order: active, then fading, then retired.
    CHECK(rankOf(view, active->index) < rankOf(view, fading->index));
    CHECK(rankOf(view, fading->index) < rankOf(view, retired->index));
    CHECK(view.visibleCount == 2);

    // The scaffolding really disappears: a learner with a long record and no
    // recent mistakes at all sees nothing.
    ErrorHistory old;
    for (int i = 0; i < 30; ++i)
        old.add(ErrorClass::A1, kRetireDays + 1);
    const RoutineView gone = personalise(old, Phase::Middlegame);
    CHECK(gone.visibleCount == 0);

    // And the recency weighting is the reason: a month-old event counts about
    // a quarter of a fresh one.
    CHECK(recencyWeight(0) == 1.0);
    CHECK(recencyWeight(7) == 1.0);
    CHECK(recencyWeight(28) > 0.24 && recencyWeight(28) < 0.26);
    CHECK(recencyWeight(-1) == 0.0);
    CHECK(recencyWeight(14) > recencyWeight(30));
}

void testSettlingWindow()
{
    // Below kSettleEvents nothing fades, however old the few events are.
    ErrorHistory history;
    for (int i = 0; i < kSettleEvents - 1; ++i)
        history.add(ErrorClass::A1, kRetireDays + 50);
    const RoutineView view = personalise(history, Phase::Middlegame);
    CHECK(view.visibleCount == static_cast<int>(view.questions.size()));
    for (std::size_t i = 0; i < view.questions.size(); ++i)
        CHECK(view.questions[i].standing == Standing::Active);
    // One more event and the record is taken seriously.
    history.add(ErrorClass::A1, kRetireDays + 50);
    CHECK(personalise(history, Phase::Middlegame).visibleCount == 0);
}

void testLatestMarker()
{
    ErrorHistory history;
    history.add(ErrorClass::F6, 9);
    history.add(ErrorClass::A1, 2);    // the most recent one
    CHECK(history.latest == ErrorClass::A1);
    const RoutineView view = personalise(history, Phase::Endgame);
    const QuestionStatus* hanging = statusOf(view, "frage-ungedeckt");
    const QuestionStatus* endgame = statusOf(view, "frage-endspiel");
    CHECK(hanging != nullptr && hanging->caughtLatest);
    CHECK(endgame != nullptr && !endgame->caughtLatest);
    CHECK(endgame->hits == 1);
}

// --- the link back ----------------------------------------------------------

void testCaughtSentence()
{
    ErrorHistory history;
    for (int i = 0; i < 20; ++i)
        history.add(ErrorClass::B1, 0);
    for (int i = 0; i < 5; ++i)
        history.add(ErrorClass::A1, 0);
    const RoutineView view = personalise(history, Phase::Middlegame);

    const int index = questionIndexFor(ErrorClass::A1);
    const int rank = rankOf(view, index);
    CHECK(rank == 2);
    const std::string sentence = caughtSentence(rank, questionAt(index));
    CHECK(sentence.find("Frage 2 hätte das gefunden:") == 0);
    CHECK(sentence.find(questionAt(index).text) != std::string::npos);
    // No evaluation ever creeps into it either.
    CHECK(sentence.find("cp") == std::string::npos);
    CHECK(sentence.find("%") == std::string::npos);

    // A question that is not in the current view still gets a sentence, just
    // without a number in front of it.
    const std::string unranked = caughtSentence(0, questionAt(index));
    CHECK(unranked.find("Diese Frage hätte das gefunden:") == 0);
}

// --- 4. the blunder-check drill, teacher.md §7.5 ----------------------------

void testDrillSchedule()
{
    BlunderCheckSchedule schedule;
    CHECK(schedule.every == 3);
    CHECK(schedule.mode == DrillMode::Auto);
    CHECK(!schedule.due());

    // "bei jedem 3. Zug"
    schedule.noteMove();
    CHECK(!schedule.due());
    schedule.noteMove();
    CHECK(!schedule.due());
    schedule.noteMove();
    CHECK(schedule.due());

    // Three correct answers in a row, and only then, move it one step out.
    schedule.noteAnswer(true);
    CHECK(!schedule.due());
    CHECK(schedule.every == 3);
    schedule.noteAnswer(true);
    CHECK(schedule.every == 3);
    schedule.noteAnswer(true);
    CHECK(schedule.every == 5);
    CHECK(schedule.streak == 0);

    // A wrong answer resets the streak — two right, one wrong, two right must
    // not advance anything.
    schedule.noteAnswer(true);
    schedule.noteAnswer(true);
    schedule.noteAnswer(false);
    schedule.noteAnswer(true);
    schedule.noteAnswer(true);
    CHECK(schedule.every == 5);
    schedule.noteAnswer(true);
    CHECK(schedule.every == 8);

    // "jeder 3. -> jeder 5. -> jeder 8. -> aus"
    for (int i = 0; i < 7; ++i)
        schedule.noteMove();
    CHECK(!schedule.due());
    schedule.noteMove();
    CHECK(schedule.due());
    schedule.noteAnswer(true);
    schedule.noteAnswer(true);
    schedule.noteAnswer(true);
    CHECK(schedule.every == 0);
    for (int i = 0; i < 20; ++i)
        schedule.noteMove();
    CHECK(!schedule.due());

    // "…und kehrt zurück, sobald ein A1-, B1- oder C2-Ereignis auftritt."
    schedule.bringBack();
    CHECK(schedule.every == 3);
    CHECK(schedule.streak == 0);
    CHECK(schedule.due());

    CHECK(BlunderCheckSchedule::bringsBack(ErrorClass::A1));
    CHECK(BlunderCheckSchedule::bringsBack(ErrorClass::B1));
    CHECK(BlunderCheckSchedule::bringsBack(ErrorClass::C2));
    CHECK(!BlunderCheckSchedule::bringsBack(ErrorClass::A2));
    CHECK(!BlunderCheckSchedule::bringsBack(ErrorClass::C4));
    CHECK(!BlunderCheckSchedule::bringsBack(ErrorClass::F6));
    // All three are SRG — the drill trains that one dimension (§3.3).
    CHECK(dimensionOf(ErrorClass::A1) == Dimension::SRG);
    CHECK(dimensionOf(ErrorClass::B1) == Dimension::SRG);
    CHECK(dimensionOf(ErrorClass::C2) == Dimension::SRG);

    // The drill's own question is in the catalogue, and it is the §1.2.2 one.
    CHECK(questionById("frage-schach-schlag") != nullptr);
    CHECK(questionById("frage-schach-schlag")->when == Trigger::BeforeRelease);

    // After the switch back to every third move the schedule advances again
    // from the beginning, not from where it was.
    schedule.noteAnswer(true);
    schedule.noteAnswer(true);
    schedule.noteAnswer(true);
    CHECK(schedule.every == 5);

    schedule.reset();
    CHECK(schedule.every == 3);
    CHECK(schedule.since == 0);
    CHECK(!schedule.due());
}

void testDrillModes()
{
    // The learner may force it. Auto is the schedule above.
    BlunderCheckSchedule schedule;
    schedule.mode = DrillMode::Always;
    CHECK(schedule.due());
    schedule.every = 0;               // even when the schedule has switched off
    CHECK(schedule.due());

    schedule.mode = DrillMode::Never;
    schedule.every = 3;
    schedule.since = 99;
    CHECK(!schedule.due());

    schedule.mode = DrillMode::Auto;
    CHECK(schedule.due());

    CHECK(std::string(drillModeKey(DrillMode::Auto)) == "auto");
    CHECK(std::string(drillModeKey(DrillMode::Always)) == "immer");
    CHECK(std::string(drillModeKey(DrillMode::Never)) == "nie");
}

} // namespace

int main()
{
    testCatalogue();
    testMappingIsTotal();
    testEmptyRecord();
    testPhaseFilter();
    testOrderFollowsTheRecord();
    testFadeOut();
    testSettlingWindow();
    testLatestMarker();
    testCaughtSentence();
    testDrillSchedule();
    testDrillModes();

    if (failures == 0)
        std::printf("test_routine: %d Fragen, alle Prüfungen bestanden\n", questionCount());
    else
        std::fprintf(stderr, "test_routine: %d Prüfung(en) fehlgeschlagen\n", failures);
    return failures == 0 ? 0 : 1;
}
