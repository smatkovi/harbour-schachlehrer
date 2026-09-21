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
// A practice session must go on after a correct answer. The user reported that
// it stops after the first task, so this walks a whole session the way the
// board does: read the task, play its solution, expect the next one.
#include "TeacherEngine.h"
#include "Database.h"
#include "core/Position.h"
#include "core/Srs.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstdio>

using namespace schach;

namespace {
int failures = 0;
void check(bool ok, const char* what, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FEHLER Zeile %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

// Exactly what Board.qml does: pick the piece up, look at the offered targets,
// put it down. Calling play() straight would skip the selection, which is the
// half the board actually uses.
bool playSolution(TeacherEngine& teacher, const QString& uci)
{
    if (uci.size() < 4)
        return false;
    const int from = core::squareFromName(uci.left(2).toStdString());
    const int to = core::squareFromName(uci.mid(2, 2).toStdString());
    const QString promotion = uci.size() > 4 ? uci.mid(4, 1) : QString();

    teacher.setSelectedSquare(from);
    if (teacher.selectedSquare() != from) {
        std::fprintf(stderr, "das Feld %s liess sich nicht auswaehlen\n",
                     qPrintable(uci.left(2)));
        return false;
    }
    bool offered = false;
    const QVariantList targets = teacher.legalTargets();
    for (int i = 0; i < targets.size(); ++i) {
        const QVariant entry = targets.at(i);
        const int square = entry.type() == QVariant::Map
                ? entry.toMap().value(QStringLiteral("to")).toInt()
                : entry.toInt();
        offered = offered || square == to;
    }
    if (!offered) {
        std::fprintf(stderr, "%s wurde nicht als Ziel angeboten (%d Ziele)\n",
                     qPrintable(uci), targets.size());
        return false;
    }
    return teacher.play(from, to, promotion);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());

    const QString root = QString::fromLatin1(qgetenv("SCHACH_SOURCE_DIR"));
    const QString bank = (root.isEmpty() ? QStringLiteral(".") : root)
            + QStringLiteral("/assets/items/placement.json");

    TeacherEngine teacher;
    teacher.setPaths(QString(), QString(), QString(),
                     tmp.path() + QStringLiteral("/test.sqlite"));
    teacher.setItemBankPath(bank);

    teacher.startSession();
    CHECK(teacher.mode() == TeacherEngine::Drill);
    CHECK(!teacher.prompt().isEmpty());

    // --- die Lösung ansehen (teacher.md §6.5, §6.6 Hilfestufe 4) ----------
    //
    // Every answered task has to stay reachable. Until this existed, a drill
    // task was gone the moment the next one loaded and one sentence of
    // feedback was all the learner ever saw of it.
    CHECK(teacher.reviewCount() == 0);

    {
        CHECK(teacher.mode() == TeacherEngine::Drill);
        const QString openTask = teacher.task().value(QStringLiteral("cardId")).toString();
        const int answeredBefore = teacher.reviewCount();

        // Looking at the solution of a task that is still open ends it,
        // unsolved: there is nothing left to produce once it has been seen.
        teacher.showSolution();
        CHECK(teacher.reviewCount() == answeredBefore + 1);
        CHECK(teacher.reviewing());
        QVariantMap view = teacher.solutionView();
        CHECK(view.value(QStringLiteral("active")).toBool());
        CHECK(view.value(QStringLiteral("atStart")).toBool());
        CHECK(view.value(QStringLiteral("total")).toInt() >= 1);
        // It starts on the position as it was asked, nothing played.
        const QString asked = teacher.fen();

        // Forward walks the line; the board moves with it.
        const int total = view.value(QStringLiteral("total")).toInt();
        for (int step = 0; step < total; ++step)
            teacher.solutionForward();
        view = teacher.solutionView();
        CHECK(view.value(QStringLiteral("atEnd")).toBool());
        CHECK(teacher.fen() != asked);

        // And back again, to exactly where it started.
        for (int step = 0; step < total; ++step)
            teacher.solutionBack();
        CHECK(teacher.fen() == asked);
        CHECK(teacher.solutionView().value(QStringLiteral("atStart")).toBool());

        // Leaving it returns to the task the session is on — not to the one
        // that was just given up, and not to a freshly drawn one.
        teacher.hideSolution();
        CHECK(!teacher.reviewing());
        CHECK(!teacher.solutionView().value(QStringLiteral("active")).toBool());
        CHECK(teacher.task().value(QStringLiteral("cardId")).toString() != openTask);
        CHECK(teacher.mode() == TeacherEngine::Drill);
    }


    QStringList seen;
    int solved = 0;
    for (int step = 0; step < 10 && teacher.mode() == TeacherEngine::Drill; ++step) {
        const QVariantMap task = teacher.task();
        const QString id = task.value(QStringLiteral("cardId")).toString();
        CHECK(!id.isEmpty());
        CHECK(!seen.contains(id));       // the same task twice is the reported bug
        seen << id;
        const QString fenBefore = teacher.fen();
        // The board is always seen from the side that has to move.
        CHECK(teacher.flipped() == !teacher.whiteToMove());
        // The whole line, not just its first move: a task may be longer than
        // one move now (teacher.md §6.5), and in that case the first move only
        // gets it started.
        const QStringList line = teacher.solutionLineForTest()
                .split(QLatin1Char(' '));
        CHECK(!line.isEmpty());
        bool played = true;
        for (int move = 0; move < line.size() && played; ++move) {
            if (teacher.task().value(QStringLiteral("guided")).toBool()
                    && (move % 2) == 1) {
                continue;   // the app answers for the opponent in that one case
            }
            played = playSolution(teacher, line.at(move));
        }
        if (!played)
            break;
        ++solved;
        if (teacher.mode() != TeacherEngine::Drill)
            break;
        CHECK(teacher.fen() != fenBefore);   // a new task, not the old board
    }

    std::printf("test_session: %d Aufgaben gelöst, %d verschiedene, %d in der Durchsicht\n",
                solved, seen.size(), teacher.reviewCount());
    CHECK(solved >= 2);
    // One given up plus the solved ones: every answered task is kept.
    CHECK(teacher.reviewCount() == solved + 1);

    // A solved task has to leave a trace, or nothing is scheduled and the hint
    // the learner needed is forgotten with it.
    Database db;
    CHECK(db.open(tmp.path() + QStringLiteral("/test.sqlite")));
    core::Card stored;
    CHECK(db.loadCard(seen.value(0), stored));
    CHECK(stored.srs.reps >= 1);

    // And a hint must cost something: the same task answered with a hint is
    // graded Hard, so it comes back sooner than one answered unaided.
    const core::SrsState clean = core::applyReview(core::SrsState(), core::Rating::Good, 0);
    const core::SrsState hinted = core::applyReview(core::SrsState(), core::Rating::Hard, 0);
    CHECK(hinted.dueDay <= clean.dueDay);
    CHECK(core::ratingFor(true, true, 1000) == core::Rating::Hard);
    CHECK(core::ratingFor(true, false, 1000) == core::Rating::Easy);
    if (failures) {
        std::printf("%d Fehler\n", failures);
        return 1;
    }
    std::printf("test_session: alles grün\n");
    return 0;
}
