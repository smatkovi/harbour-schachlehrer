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
// What the placement test hands out. Three defects were found by simulating
// the picker against the shipped bank, and this is the test that keeps them
// from coming back:
//
//   1. Every placement test opened with the same six positions, because the
//      picker took the nearest item and theta always started at 1000.
//   2. Nobody could be placed above about 1400, for the same reason: twenty
//      five items move theta a bounded distance, so the starting point was
//      the ceiling.
//   3. A learner who answered everything wrong was served *harder* items —
//      1223, 1342, 1479, 1684 — because "nearest" reached upwards once the
//      easy items of a dimension were used up. teacher.md §6.1 prices that
//      exactly: retrieval practice below 50 % correct has g = 0.03.
#include "ItemBank.h"
#include "TeacherEngine.h"
#include "core/Skill.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QVector>

#include <cstdio>

using namespace schach;

namespace {

int failures = 0;

void check(bool ok, const char* what, int line)
{
    if (!ok) {
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)

QString sourceRoot()
{
    const QString root = QString::fromLatin1(qgetenv("SCHACH_SOURCE_DIR"));
    return root.isEmpty() ? QStringLiteral(".") : root;
}

QString bankPath()
{
    return sourceRoot() + QStringLiteral("/assets/items/placement.json");
}

// The imported bank (tools/import_lichess_puzzles.py). It may be absent — the
// app runs without it — so every use of it is guarded.
QString extraBankPath()
{
    return sourceRoot() + QStringLiteral("/assets/items/puzzles.json");
}

void loadBanks(TeacherEngine& teacher)
{
    teacher.setItemBankPath(bankPath());
    teacher.addItemBankPath(extraBankPath());
}

// One item as the placement test handed it out.
struct Asked {
    QString id;
    core::Dimension dimension = core::Dimension::TAK;
    double difficulty = 0.0;
};

// The first `count` items a fresh placement test hands out, with a given draw
// seed. Every item is skipped, which counts as a wrong answer.
QVector<Asked> openingItems(TeacherEngine& teacher, unsigned int seed, int count)
{
    teacher.seedItemBankForTest(seed);
    teacher.startPlacement();
    QVector<Asked> out;
    for (int i = 0; i < count && teacher.mode() == TeacherEngine::Placement; ++i) {
        const QVariantMap task = teacher.task();
        Asked asked;
        asked.id = task.value(QStringLiteral("itemId")).toString();
        if (asked.id.isEmpty())
            break;
        asked.dimension = core::dimensionFromKey(
                task.value(QStringLiteral("dimension")).toString().toStdString());
        asked.difficulty = task.value(QStringLiteral("difficulty")).toDouble();
        out << asked;
        teacher.skipTask();
    }
    return out;
}

QStringList idsOf(const QVector<Asked>& asked)
{
    QStringList out;
    for (int i = 0; i < asked.size(); ++i)
        out << asked.at(i).id;
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- 1. two tests do not open the same way ----------------------------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        TeacherEngine teacher;
        teacher.setPaths(QString(), QString(), QString(),
                         tmp.path() + QStringLiteral("/a.sqlite"));
        loadBanks(teacher);

        // The whole test, not just its opening: a learner who answers
        // everything wrong walks below the bank's floor, and that is where the
        // picker used to fall back to the same easiest item every time.
        const QVector<Asked> first = openingItems(teacher, 1, 25);
        const QVector<Asked> second = openingItems(teacher, 2, 25);
        CHECK(first.size() >= 20);
        CHECK(second.size() >= 20);

        const QStringList firstIds = idsOf(first);
        int repeated = 0;
        for (int i = 0; i < second.size(); ++i)
            repeated += firstIds.contains(second.at(i).id) ? 1 : 0;

        // What the picker can promise is exactly this: it never repeats while
        // an unseen item of that dimension fits the window. Where the bank
        // holds fewer than two items near the difficulty that was asked for
        // there is no choice to make, and no amount of shuffling invents one.
        // So the bound is counted from the bank, per item, and the thin spots
        // are printed — that is a gap in the *data*, and it should be visible.
        ItemBank bank;
        CHECK(bank.load(bankPath()));
        bank.merge(extraBankPath());
        int forced = 0;
        for (int i = 0; i < second.size(); ++i) {
            const Asked& asked = second.at(i);
            const int near = bank.countNear(asked.dimension, asked.difficulty);
            if (near < 2) {
                ++forced;
                std::printf("  %s bei %4.0f: nur %d Aufgabe(n) im Fenster — hier bekommt\n"
                            "  jeder Lernende dieselbe Stellung\n",
                            core::dimensionKey(asked.dimension), asked.difficulty, near);
            }
        }
        std::printf("  %d von %d Aufgaben eines zweiten Tests wiederholt, "
                    "%d davon unvermeidlich\n",
                    repeated, second.size(), forced);
        CHECK(repeated <= forced);
    }

    // --- 2. a failing learner is not handed harder and harder items -------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        TeacherEngine teacher;
        teacher.setPaths(QString(), QString(), QString(),
                         tmp.path() + QStringLiteral("/b.sqlite"));
        loadBanks(teacher);
        teacher.seedItemBankForTest(7);
        teacher.startPlacement();

        QVector<double> difficulties;
        for (int i = 0; i < 25 && teacher.mode() == TeacherEngine::Placement; ++i) {
            difficulties << teacher.task().value(QStringLiteral("difficulty")).toDouble();
            teacher.skipTask();
        }
        CHECK(difficulties.size() >= 20);
        // The *asked-for* difficulty must fall, not rise: everything was wrong.
        CHECK(difficulties.last() < difficulties.first());
        // And no item may sit far above what was asked. Before the fix the
        // gap reached +400 and more.
        for (int i = 0; i < difficulties.size(); ++i)
            CHECK(difficulties.at(i) < 1400.0);
    }

    // --- 3. a retest starts from what was measured, not from 1000 ---------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString path = tmp.path() + QStringLiteral("/c.sqlite");
        {
            Database db;
            CHECK(db.open(path));
            QVector<double> perDimension;
            for (int i = 0; i < core::kDimensionCount; ++i)
                perDimension << 1600.0;
            CHECK(db.recordSkill(1000, 1600.0, perDimension, 0.05));
        }
        TeacherEngine teacher;
        teacher.setPaths(QString(), QString(), QString(), path);
        loadBanks(teacher);
        teacher.seedItemBankForTest(11);
        teacher.startPlacement();
        // §4.7: the anchors sit 100 below what is already known. Measured at
        // 1600, the test must not open at 900 again.
        const double asked = teacher.task().value(QStringLiteral("difficulty")).toDouble();
        CHECK(asked > 1400.0);
    }

    // --- 4. the estimator is fed the item, not the wish -------------------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        TeacherEngine teacher;
        teacher.setPaths(QString(), QString(), QString(),
                         tmp.path() + QStringLiteral("/d.sqlite"));
        loadBanks(teacher);
        teacher.seedItemBankForTest(13);
        teacher.startPlacement();

        ItemBank bank;
        CHECK(bank.load(bankPath()));
        bank.merge(extraBankPath());

        // §4.4 updates theta with the difficulty of the item that was actually
        // answered. The items are drawn from a window around the target now,
        // so the two differ — and carrying the target instead would put an
        // error of up to the whole window into every single update.
        int differed = 0;
        for (int i = 0; i < 10 && teacher.mode() == TeacherEngine::Placement; ++i) {
            const QVariantMap task = teacher.task();
            const QString id = task.value(QStringLiteral("itemId")).toString();
            CHECK(!id.isEmpty());
            const double carried = task.value(QStringLiteral("difficulty")).toDouble();
            CHECK(carried == bank.difficultyOf(id));
            CHECK(task.contains(QStringLiteral("requested")));
            if (carried != task.value(QStringLiteral("requested")).toDouble())
                ++differed;
            teacher.skipTask();
        }
        // If they never differed the check above would be vacuous.
        CHECK(differed > 0);
    }

    // --- 5. the solution of an answered item can be stepped through -------
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        TeacherEngine teacher;
        teacher.setPaths(QString(), QString(), QString(),
                         tmp.path() + QStringLiteral("/e.sqlite"));
        loadBanks(teacher);
        teacher.seedItemBankForTest(17);
        teacher.startPlacement();

        // A fresh test starts with an empty review — the solutions of the last
        // one are not the solutions of this one.
        CHECK(teacher.reviewCount() == 0);

        // Skip a few, then look at what they were. §4.1 allows exactly this:
        // the solution may be looked at, only the running tally is withheld.
        for (int i = 0; i < 3 && teacher.mode() == TeacherEngine::Placement; ++i)
            teacher.skipTask();
        CHECK(teacher.reviewCount() == 3);

        teacher.reviewPrevious();
        CHECK(teacher.reviewing());
        QVariantMap view = teacher.solutionView();
        CHECK(view.value(QStringLiteral("active")).toBool());
        CHECK(view.value(QStringLiteral("atStart")).toBool());
        const int total = view.value(QStringLiteral("total")).toInt();
        CHECK(total >= 1);
        CHECK(!view.value(QStringLiteral("san")).toString().isEmpty());

        const QString asked = teacher.fen();
        for (int step = 0; step < total; ++step)
            teacher.solutionForward();
        CHECK(teacher.solutionView().value(QStringLiteral("atEnd")).toBool());
        CHECK(teacher.fen() != asked);
        // One more forward changes nothing: the line ends where it ends.
        const QString end = teacher.fen();
        teacher.solutionForward();
        CHECK(teacher.fen() == end);

        teacher.hideSolution();
        CHECK(!teacher.reviewing());
        // And the test goes on where it stood, on an item, not on a result.
        CHECK(teacher.mode() == TeacherEngine::Placement);
        CHECK(!teacher.task().value(QStringLiteral("itemId")).toString().isEmpty());
    }

    // --- 6. the picker itself ---------------------------------------------
    {
        ItemBank bank;
        CHECK(bank.load(bankPath()));
        bank.merge(extraBankPath());
        bank.setSeed(3);
        // Asked for something far below the bank's floor (720): it must hand
        // out the easiest item it has and never the hardest.
        const PlacementItem* item = bank.pick(core::Dimension::TAK, 300.0, QStringList());
        CHECK(item != 0);
        if (item)
            CHECK(item->difficulty < 1100.0);

        // Two draws at the same target differ over a handful of tries — that
        // is what stops the test from repeating itself.
        QStringList drawn;
        for (int i = 0; i < 12; ++i) {
            const PlacementItem* one = bank.pick(core::Dimension::TAK, 1300.0, QStringList());
            if (one && !drawn.contains(one->id))
                drawn << one->id;
        }
        CHECK(drawn.size() > 1);

        // Every item now carries a line, even a one-move one.
        const PlacementItem* any = bank.pick(core::Dimension::END, 1200.0, QStringList());
        CHECK(any != 0);
        if (any) {
            CHECK(!any->line.isEmpty());
            CHECK(any->line.first() == any->solution);
        }
    }

    if (failures) {
        std::fprintf(stderr, "\n%d Prüfung(en) fehlgeschlagen\n", failures);
        return 1;
    }
    std::printf("test_bank: alles grün\n");
    return 0;
}
