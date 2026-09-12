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
#ifndef SCHACH_CORE_ROUTINE_H
#define SCHACH_CORE_ROUTINE_H

// "Welche Fragen soll ich mir bei einer Stellung stellen?" — the thinking
// routine, and the one part of the app the learner is supposed to take away
// from it and keep using without the app.
//
// It is not a new system. Every "warum nicht"-sentence of teacher.md §6.6 ends
// with a transferable hint, and those hints *are* the questions; this file
// collects them, gives each a stable id, and records which of the 50 error
// classes of §2.6 each one would have caught. Four of the ten had no hint in
// §6.6 (groups H and I have none, and A10 and the mate-net half of C1 only in
// passing); those are written in the same voice and marked `fromSpec = false`,
// so a reader can tell the spec from the addition.
//
// Length is a design decision, not an accident: a routine of twenty questions
// is a poster nobody reads. Ten, of which at most nine ever apply at once
// (the opening and the endgame question exclude each other), is a routine.
//
// Two things follow the learner rather than a fixed list:
//
//   * the **order** — a question whose classes he actually commits rises and
//     stays up (§3.2 b: the diagnosis normalises against the learner himself),
//   * the **visibility** — a question that has not been triggered in a long
//     time fades out and finally retires, the same idea as the fade-out of the
//     blunder-check drill in §7.5: the scaffolding has to disappear as it is
//     internalised, otherwise it is furniture.
//
// Qt-free like the rest of src/core/: the personalisation is a pure function
// of a counted history, which is what makes it checkable with fixed inputs.

#include "Position.h"
#include "Taxonomy.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace schach {
namespace core {

// When a question applies. The five moments the routine is built around, plus
// "before your own plan", which is the wording §6.6 C2 itself uses ("Vor dem
// eigenen Plan: Steht bei ihm etwas frei?").
enum class Trigger : std::uint8_t {
    AfterOpponentMove = 0,   // nach seinem Zug
    BeforeOwnPlan,           // bevor du deinen Plan machst
    BeforeExchange,          // vor einem Abtausch
    BeforeRelease,           // bevor du den Zug freigibst
    InEndgame,               // im Endspiel
    InOpening                // in der Eröffnung
};

const char* triggerKey(Trigger trigger);    // stable, e.g. "vor-freigabe"
const char* triggerName(Trigger trigger);   // German, shown to the learner

// One question of the routine. Everything here is static data; the learner's
// history never touches it, only the order and the visibility (see below).
struct Question {
    const char* id;             // stable key, never the index: "frage-ungedeckt"
    const char* text;           // German, second person — what the learner reads
    Trigger when;
    Dimension dimension;        // the skill it serves (§3.3)
    bool fromSpec;              // true: the hint stands in §6.6; false: added here
    const char* source;         // where it comes from, for the About/rules page
    const ErrorClass* catches;  // the classes it would have caught
    int catchCount;
};

int questionCount();
const Question& questionAt(int index);
// -1 when there is none (only ErrorClass::None and ErrorClass::Count).
int questionIndexFor(ErrorClass cls);
const Question* questionFor(ErrorClass cls);
const Question* questionById(const std::string& id);
bool questionCatches(int index, ErrorClass cls);

// --- the learner's own record ----------------------------------------------

constexpr int kErrorClassCount = static_cast<int>(ErrorClass::Count);

// What the personalisation reads: how often each class was committed inside
// the window the caller chose (Database: the last ten games, teacher.md §3.2 b)
// and how long ago the most recent one was. Days, because that is what the
// fade-out is expressed in and what the database can answer.
struct ErrorHistory {
    std::array<int, kErrorClassCount> count;
    std::array<int, kErrorClassCount> daysSinceLast;   // -1 = never
    int totalEvents = 0;
    ErrorClass latest = ErrorClass::None;   // the most recent mistake, for the marker
    int latestDays = -1;

    ErrorHistory();
    void add(ErrorClass cls, int daysAgo, int times = 1);
    void clear();
};

// Below this many recorded events nothing is faded: a learner who has just
// started gets the whole routine, because an empty record is not evidence that
// he has internalised anything.
constexpr int kSettleEvents = 10;
// Not triggered in this many days: the question drops behind the active ones.
constexpr int kFadeDays = 30;
// Not triggered in this many days: it retires and is not shown at all.
constexpr int kRetireDays = 90;

enum class Standing : std::uint8_t { Active = 0, Fading, Retired };
const char* standingKey(Standing standing);

struct QuestionStatus {
    int index = -1;                 // into questionAt()
    double weight = 0.0;            // what the order is built from
    int hits = 0;                   // events of this learner the question covers
    int daysSinceLastHit = -1;      // -1 = never
    Standing standing = Standing::Active;
    bool caughtLatest = false;      // it is the one that caught the last mistake
};

// The questions that apply in `phase`, ordered: active ones by weight (the
// heaviest first), then the fading ones, then the retired ones, each group in
// the canonical order of the catalogue. The retired ones are carried so the
// UI can offer "alle Fragen zeigen" without a second call; `visibleCount` is
// how many come before them.
struct RoutineView {
    std::vector<QuestionStatus> questions;
    int visibleCount = 0;
};

RoutineView personalise(const ErrorHistory& history, Phase phase);

// How much one event still counts: full for a week, then 7/d. Exposed because
// the test pins it and because the order is otherwise a black box.
double recencyWeight(int daysSinceLast);

// 1-based position of a question in a view, 0 when it is not in it — this is
// the number the feedback sentence quotes ("Frage 2 hätte das gefunden").
int rankOf(const RoutineView& view, int questionIndex);

// teacher.md §6.6: the link back. The sentence of the error class stays as it
// is and this one is *added* after it, never instead of it.
std::string caughtSentence(int rank, const Question& question);

// --- the blunder-check drill, teacher.md §7.5 -------------------------------

// The learner may force the scaffolding on or off; Auto is the schedule below,
// which is the spec's rule.
enum class DrillMode : std::uint8_t { Auto = 0, Always, Never };
const char* drillModeKey(DrillMode mode);

// "Vor der Zugfreigabe, bei jedem 3. Zug (später bei jedem 5., dann aus)".
// Three correct answers in a row move it one step out; an A1, B1 or C2 event
// puts it straight back to every third move. Qt-free and separate from the
// sparring opponent so that the schedule can be checked on its own.
struct BlunderCheckSchedule {
    int every = 3;        // 3 -> 5 -> 8 -> 0 (off)
    int streak = 0;       // correct answers in a row
    int since = 0;        // own moves since the last check
    DrillMode mode = DrillMode::Auto;

    void reset();                 // back to every third move, streak cleared
    bool due() const;
    void noteMove();              // one of the learner's own moves went by
    void noteAnswer(bool correct);
    void bringBack();             // an A1/B1/C2 event happened (§7.5)

    // The three classes that bring the drill back. They are exactly the three
    // §7.5 names, and all three are SRG (§3.3) — the drill trains that one
    // dimension and nothing else.
    static bool bringsBack(ErrorClass cls);
};

} // namespace core
} // namespace schach

#endif // SCHACH_CORE_ROUTINE_H
