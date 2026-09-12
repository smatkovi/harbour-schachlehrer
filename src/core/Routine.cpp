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
#include "Routine.h"

#include <algorithm>

namespace schach {
namespace core {

namespace {

// The class lists. Each class appears in exactly one list as its *primary*
// question — the one the feedback quotes — with one deliberate repetition
// (B5 is a threat as much as it is endgame technique), which is why the
// lookup below takes the first match in catalogue order.

const ErrorClass kDrohung[] = {
    ErrorClass::B1, ErrorClass::B2, ErrorClass::B2n, ErrorClass::B4, ErrorClass::B5
};

const ErrorClass kFreieFigur[] = {
    ErrorClass::C1, ErrorClass::C2, ErrorClass::C3, ErrorClass::A10
};

const ErrorClass kStillerZug[] = {
    ErrorClass::C4, ErrorClass::C5,
    ErrorClass::E2, ErrorClass::E3, ErrorClass::E4,
    ErrorClass::G2, ErrorClass::G5
};

const ErrorClass kAbtausch[] = {
    ErrorClass::A2, ErrorClass::A9, ErrorClass::E1, ErrorClass::E5, ErrorClass::E6,
    ErrorClass::G3
};

const ErrorClass kUngedeckt[] = {
    ErrorClass::A1, ErrorClass::A7
};

const ErrorClass kSchachUndSchlag[] = {
    ErrorClass::A3, ErrorClass::A3k, ErrorClass::A4, ErrorClass::A5, ErrorClass::A6,
    ErrorClass::A8
};

const ErrorClass kKoenig[] = {
    ErrorClass::B3, ErrorClass::D1, ErrorClass::D2, ErrorClass::G1, ErrorClass::G4
};

const ErrorClass kEndspiel[] = {
    ErrorClass::C6,
    ErrorClass::F1, ErrorClass::F2, ErrorClass::F3, ErrorClass::F4,
    ErrorClass::F5, ErrorClass::F6, ErrorClass::F7, ErrorClass::F8,
    ErrorClass::B5
};

const ErrorClass kEroeffnung[] = {
    ErrorClass::D3, ErrorClass::H1, ErrorClass::H2, ErrorClass::H3, ErrorClass::H4
};

const ErrorClass kFreigabe[] = {
    ErrorClass::I1, ErrorClass::I2, ErrorClass::I3, ErrorClass::Z0
};

// The catalogue. The order is the cadence of the routine as it is taught, and
// it is also the tie-breaker of the personalised order: first look at what he
// wants (§6.6 B1: "Erst hinschauen, was er will, dann den eigenen Plan"), then
// at what is going for you, then check your own move, then release it.
//
// It is not the order the learner sees once he has a record — that is
// personalise() below.
const Question kQuestions[] = {
    { "frage-drohung",
      "Was will er? Was hat sein letzter Zug verändert — was droht dir jetzt?",
      Trigger::AfterOpponentMove, Dimension::SRG, true,
      "teacher.md §6.6 B1",
      kDrohung, static_cast<int>(sizeof(kDrohung) / sizeof(kDrohung[0])) },

    { "frage-freie-figur",
      "Steht bei ihm etwas frei — und ist es wirklich frei oder eine Falle für dich?",
      // TAK, not SRG, although the §6.6 hint it comes from belongs to C2:
      // three of its four classes (C1, C3, A10) are pattern recognition, and
      // that is what the second half of the question asks for (§3.1: TAK is
      // recognising the pattern, SRG is looking whether there is one at all).
      Trigger::BeforeOwnPlan, Dimension::TAK, true,
      "teacher.md §6.6 C2 (die Fallenhälfte für A10 ergänzt)",
      kFreieFigur, static_cast<int>(sizeof(kFreieFigur) / sizeof(kFreieFigur[0])) },

    { "frage-stiller-zug",
      "Nicht jeder Gewinnzug ist laut: Hast du einen ruhigen Zug, der mehr bringt?",
      Trigger::BeforeOwnPlan, Dimension::REC, true,
      "teacher.md §6.6 C4",
      kStillerZug, static_cast<int>(sizeof(kStillerZug) / sizeof(kStillerZug[0])) },

    { "frage-abtausch",
      "Zähl den Abtausch bis zum Ende — und frag: Hat er ein Schach dazwischen?",
      Trigger::BeforeExchange, Dimension::REC, true,
      "teacher.md §6.6 A2 und A9",
      kAbtausch, static_cast<int>(sizeof(kAbtausch) / sizeof(kAbtausch[0])) },

    { "frage-ungedeckt",
      "Was steht nach deinem Zug ungedeckt — und welche Figur hat kein Feld mehr?",
      Trigger::BeforeRelease, Dimension::SRG, true,
      "teacher.md §6.6 A1",
      kUngedeckt, static_cast<int>(sizeof(kUngedeckt) / sizeof(kUngedeckt[0])) },

    { "frage-schach-schlag",
      "Welche Schachs und Schläge hat er nach deinem Zug — und trifft einer "
      "davon zwei Sachen auf einmal?",
      Trigger::BeforeRelease, Dimension::SRG, true,
      "teacher.md §7.5 und §1.2.2, Gabelhälfte aus §6.6 A3",
      kSchachUndSchlag, static_cast<int>(sizeof(kSchachUndSchlag) / sizeof(kSchachUndSchlag[0])) },

    { "frage-koenig",
      "Wie steht dein König: Hat er ein Luftloch, steht der Bauernschild noch? "
      "Einen Bauernzug davor nimmst du nicht zurück.",
      Trigger::BeforeRelease, Dimension::STL, true,
      "teacher.md §6.6 B3 (Struktur- und Rochadehälfte ergänzt)",
      kKoenig, static_cast<int>(sizeof(kKoenig) / sizeof(kKoenig[0])) },

    { "frage-endspiel",
      "Im Endspiel: Gehört dein König nach vorn — und was wird nach dem letzten "
      "Abtausch aus dem Bauernendspiel?",
      Trigger::InEndgame, Dimension::END, true,
      "teacher.md §6.6 F6 (Königshälfte ergänzt)",
      kEndspiel, static_cast<int>(sizeof(kEndspiel) / sizeof(kEndspiel[0])) },

    { "frage-eroeffnung",
      "In der Eröffnung: Bringst du eine neue Figur ins Spiel, und wann kommt "
      "dein König in Sicherheit?",
      Trigger::InOpening, Dimension::ERD, false,
      "ergänzt — §6.6 hat für Gruppe H keinen Hinweis (§2.6 H1–H3, D1)",
      kEroeffnung, static_cast<int>(sizeof(kEroeffnung) / sizeof(kEroeffnung[0])) },

    { "frage-freigabe",
      "Ist das hier ein wichtiger Moment? Dann nimm dir die Zeit dafür — und "
      "gib den Zug erst danach frei.",
      Trigger::BeforeRelease, Dimension::SRG, false,
      "ergänzt — §6.6 hat für Gruppe I keinen Hinweis (§2.6 I1, §1.2.5)",
      kFreigabe, static_cast<int>(sizeof(kFreigabe) / sizeof(kFreigabe[0])) },
};

constexpr int kCount = static_cast<int>(sizeof(kQuestions) / sizeof(kQuestions[0]));

bool appliesIn(Trigger trigger, Phase phase)
{
    // The two phase-bound questions exclude each other, which is why ten
    // questions are at most nine on the screen and usually eight.
    if (trigger == Trigger::InOpening)
        return phase == Phase::Opening;
    if (trigger == Trigger::InEndgame)
        return phase == Phase::Endgame;
    return true;
}

} // namespace

const char* triggerKey(Trigger trigger)
{
    switch (trigger) {
    case Trigger::AfterOpponentMove: return "nach-seinem-zug";
    case Trigger::BeforeOwnPlan:     return "vor-dem-plan";
    case Trigger::BeforeExchange:    return "vor-dem-abtausch";
    case Trigger::BeforeRelease:     return "vor-der-freigabe";
    case Trigger::InEndgame:         return "im-endspiel";
    case Trigger::InOpening:         return "in-der-eroeffnung";
    }
    return "";
}

const char* triggerName(Trigger trigger)
{
    switch (trigger) {
    case Trigger::AfterOpponentMove: return "Nach seinem Zug";
    case Trigger::BeforeOwnPlan:     return "Bevor du deinen Plan machst";
    case Trigger::BeforeExchange:    return "Vor einem Abtausch";
    case Trigger::BeforeRelease:     return "Bevor du den Zug freigibst";
    case Trigger::InEndgame:         return "Im Endspiel";
    case Trigger::InOpening:         return "In der Eröffnung";
    }
    return "";
}

const char* standingKey(Standing standing)
{
    switch (standing) {
    case Standing::Active:  return "aktiv";
    case Standing::Fading:  return "verblassend";
    case Standing::Retired: return "abgelegt";
    }
    return "";
}

const char* drillModeKey(DrillMode mode)
{
    switch (mode) {
    case DrillMode::Auto:   return "auto";
    case DrillMode::Always: return "immer";
    case DrillMode::Never:  return "nie";
    }
    return "";
}

int questionCount() { return kCount; }

const Question& questionAt(int index)
{
    if (index < 0 || index >= kCount)
        return kQuestions[0];
    return kQuestions[index];
}

bool questionCatches(int index, ErrorClass cls)
{
    if (index < 0 || index >= kCount)
        return false;
    const Question& question = kQuestions[index];
    for (int i = 0; i < question.catchCount; ++i) {
        if (question.catches[i] == cls)
            return true;
    }
    return false;
}

int questionIndexFor(ErrorClass cls)
{
    if (cls == ErrorClass::None || cls == ErrorClass::Count)
        return -1;
    for (int i = 0; i < kCount; ++i) {
        if (questionCatches(i, cls))
            return i;
    }
    return -1;
}

const Question* questionFor(ErrorClass cls)
{
    const int index = questionIndexFor(cls);
    return index < 0 ? nullptr : &kQuestions[index];
}

const Question* questionById(const std::string& id)
{
    for (int i = 0; i < kCount; ++i) {
        if (id == kQuestions[i].id)
            return &kQuestions[i];
    }
    return nullptr;
}

// --- the learner's own record -----------------------------------------------

ErrorHistory::ErrorHistory() { clear(); }

void ErrorHistory::clear()
{
    count.fill(0);
    daysSinceLast.fill(-1);
    totalEvents = 0;
    latest = ErrorClass::None;
    latestDays = -1;
}

void ErrorHistory::add(ErrorClass cls, int daysAgo, int times)
{
    const int index = static_cast<int>(cls);
    if (index <= 0 || index >= kErrorClassCount || times <= 0)
        return;
    if (daysAgo < 0)
        daysAgo = 0;
    count[index] += times;
    if (daysSinceLast[index] < 0 || daysAgo < daysSinceLast[index])
        daysSinceLast[index] = daysAgo;
    totalEvents += times;
    if (latestDays < 0 || daysAgo < latestDays) {
        latestDays = daysAgo;
        latest = cls;
    }
}

double recencyWeight(int daysSinceLast)
{
    // Full weight for a week, then 7/d: after a month a mistake still counts,
    // but a quarter as much as a fresh one. No exponential, because the shape
    // is a design choice and a hyperbola is the one that can be read off the
    // number ("a month old counts a quarter").
    if (daysSinceLast < 0)
        return 0.0;
    if (daysSinceLast <= 7)
        return 1.0;
    return 7.0 / static_cast<double>(daysSinceLast);
}

RoutineView personalise(const ErrorHistory& history, Phase phase)
{
    RoutineView view;
    view.questions.reserve(kCount);

    const int latestIndex = questionIndexFor(history.latest);

    for (int i = 0; i < kCount; ++i) {
        if (!appliesIn(kQuestions[i].when, phase))
            continue;

        QuestionStatus status;
        status.index = i;
        for (int c = 0; c < kQuestions[i].catchCount; ++c) {
            const int cls = static_cast<int>(kQuestions[i].catches[c]);
            if (cls <= 0 || cls >= kErrorClassCount)
                continue;
            const int n = history.count[cls];
            if (n <= 0)
                continue;
            const int days = history.daysSinceLast[cls];
            status.hits += n;
            status.weight += n * recencyWeight(days);
            if (days >= 0 && (status.daysSinceLastHit < 0 || days < status.daysSinceLastHit))
                status.daysSinceLastHit = days;
        }
        status.caughtLatest = (i == latestIndex);

        // The fade-out of §7.5, applied to the whole list. Below kSettleEvents
        // nothing fades: an empty record is not evidence that the learner has
        // internalised anything, only that the app has not seen him play.
        if (history.totalEvents < kSettleEvents)
            status.standing = Standing::Active;
        else if (status.hits > 0 && status.daysSinceLastHit >= 0
                 && status.daysSinceLastHit <= kFadeDays)
            status.standing = Standing::Active;
        else if (status.hits > 0 && status.daysSinceLastHit >= 0
                 && status.daysSinceLastHit <= kRetireDays)
            status.standing = Standing::Fading;
        else
            status.standing = Standing::Retired;

        view.questions.push_back(status);
    }

    // Active ones by weight, the heaviest first; everything else stays in the
    // order of the catalogue, so the list the learner half knows by heart does
    // not reshuffle under him for no reason.
    std::stable_sort(view.questions.begin(), view.questions.end(),
                     [](const QuestionStatus& a, const QuestionStatus& b) {
                         if (a.standing != b.standing)
                             return a.standing < b.standing;
                         if (a.standing == Standing::Active && a.weight != b.weight)
                             return a.weight > b.weight;
                         return a.index < b.index;
                     });

    view.visibleCount = 0;
    for (std::size_t i = 0; i < view.questions.size(); ++i) {
        if (view.questions[i].standing != Standing::Retired)
            ++view.visibleCount;
    }
    return view;
}

int rankOf(const RoutineView& view, int questionIndex)
{
    for (std::size_t i = 0; i < view.questions.size(); ++i) {
        if (view.questions[i].index == questionIndex)
            return static_cast<int>(i) + 1;
    }
    return 0;
}

std::string caughtSentence(int rank, const Question& question)
{
    // teacher.md §6.6: this is *added* to the sentence of the error class, and
    // it carries no number of any kind except the position in the learner's
    // own list — which is a label, not an evaluation.
    std::string out;
    if (rank > 0) {
        out += "Frage ";
        out += std::to_string(rank);
        out += " hätte das gefunden: ";
    } else {
        out += "Diese Frage hätte das gefunden: ";
    }
    out += question.text;
    return out;
}

// --- the blunder-check drill, teacher.md §7.5 -------------------------------

void BlunderCheckSchedule::reset()
{
    every = 3;
    streak = 0;
    since = 0;
}

bool BlunderCheckSchedule::due() const
{
    if (mode == DrillMode::Never)
        return false;
    if (mode == DrillMode::Always)
        return true;
    if (every <= 0)
        return false;
    return since >= every;
}

void BlunderCheckSchedule::noteMove() { ++since; }

void BlunderCheckSchedule::noteAnswer(bool correct)
{
    since = 0;
    if (!correct) {
        streak = 0;
        return;
    }
    if (++streak < 3)
        return;
    // "Die Abfrage wird seltener, wenn sie dreimal hintereinander richtig
    // beantwortet wurde (jeder 3. → jeder 5. → jeder 8. → aus)."
    streak = 0;
    if (every == 3)
        every = 5;
    else if (every == 5)
        every = 8;
    else
        every = 0;
}

void BlunderCheckSchedule::bringBack()
{
    // "… und kehrt zurück, sobald ein A1-, B1- oder C2-Ereignis auftritt."
    // Back to every third move, and due at once: the next move is the one that
    // needs the check, not the third one after it.
    every = 3;
    streak = 0;
    since = every;
}

bool BlunderCheckSchedule::bringsBack(ErrorClass cls)
{
    return cls == ErrorClass::A1 || cls == ErrorClass::B1 || cls == ErrorClass::C2;
}

} // namespace core
} // namespace schach
