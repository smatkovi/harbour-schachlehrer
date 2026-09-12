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
#include "core/Position.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

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
        const QString solution = teacher.solutionForTest();
        CHECK(!solution.isEmpty());
        if (!playSolution(teacher, solution))
            break;
        ++solved;
        if (teacher.mode() != TeacherEngine::Drill)
            break;
        CHECK(teacher.fen() != fenBefore);   // a new task, not the old board
    }

    std::printf("test_session: %d Aufgaben gelöst, %d verschiedene\n", solved, seen.size());
    CHECK(solved >= 2);
    if (failures) {
        std::printf("%d Fehler\n", failures);
        return 1;
    }
    std::printf("test_session: alles grün\n");
    return 0;
}
