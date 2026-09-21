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
// PuzzleFeed: turning what the Lichess API hands out into this app's items.
//
// Offline. The answer of `GET /api/puzzle/batch/mix` is a fixture in
// tests/data-puzzle-batch.json, captured from the live endpoint on 2026-09-18;
// no test may ever talk to the real server.
//
// The check that matters most is the one about the first move. The two Lichess
// sources disagree, and both mistakes look plausible:
//
//   * the CSV export gives a FEN *before* the opponent's move, so `Moves[0]`
//     belongs to the opponent and has to be applied, not shown;
//   * the API gives no FEN. The position is the PGN played out for
//     `initialPly + 1` half moves, and `solution[0]` is already the learner's.
//
// Off by one in either direction and every position is shown one move early —
// which still looks like a chess position, which is why it needs a test.
#include "PuzzleFeed.h"
#include "Themes.h"
#include "core/Position.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

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

QJsonArray fixture()
{
    QFile file(sourceRoot() + QStringLiteral("/tests/data-puzzle-batch.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "the fixture is missing: %s\n", qPrintable(file.fileName()));
        return QJsonArray();
    }
    return QJsonDocument::fromJson(file.readAll())
            .object().value(QStringLiteral("puzzles")).toArray();
}

QSet<QString> themesOf(const QStringList& list)
{
    QSet<QString> out;
    for (int i = 0; i < list.size(); ++i)
        out.insert(list.at(i));
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString themesPath = sourceRoot() + QStringLiteral("/assets/items/themes.json");

    // --- the table, shared with tools/import_lichess_puzzles.py -----------
    {
        Themes themes;
        CHECK(themes.load(themesPath));
        CHECK(!themes.isEmpty());

        CHECK(themes.dimensionOf(themesOf(QStringList() << "endgame" << "rookEndgame"))
              == core::Dimension::END);
        CHECK(themes.dimensionOf(themesOf(QStringList() << "opening")) == core::Dimension::ERD);
        CHECK(themes.dimensionOf(themesOf(QStringList() << "long")) == core::Dimension::REC);
        CHECK(themes.dimensionOf(themesOf(QStringList() << "hangingPiece")) == core::Dimension::SRG);
        CHECK(themes.dimensionOf(themesOf(QStringList() << "quietMove")) == core::Dimension::STL);
        // A quiet move that executes a motif is a tactic that happens to be
        // quiet — that separation is what lets §6.3's quota be filled across
        // all six dimensions instead of piling into STL.
        CHECK(themes.dimensionOf(themesOf(QStringList() << "quietMove" << "fork"))
              == core::Dimension::TAK);
        CHECK(themes.dimensionOf(themesOf(QStringList() << "fork")) == core::Dimension::TAK);

        CHECK(themes.sortOf(themesOf(QStringList() << "quietMove")) == QLatin1String("quiet"));
        CHECK(themes.sortOf(themesOf(QStringList() << "defensiveMove"))
              == QLatin1String("defensive"));
        // Both: quiet wins, because it is the rarer sort (§6.3).
        CHECK(themes.sortOf(themesOf(QStringList() << "quietMove" << "defensiveMove"))
              == QLatin1String("quiet"));
        CHECK(themes.sortOf(themesOf(QStringList() << "fork")) == QLatin1String("other"));

        CHECK(!themes.sentenceFor(themesOf(QStringList() << "fork")).isEmpty());
        CHECK(themes.sentenceFor(themesOf(QStringList() << "middlegame")).isEmpty());

        // A file that is not there is a stated condition, not a default.
        Themes missing;
        CHECK(!missing.load(sourceRoot() + QStringLiteral("/assets/items/nope.json")));
        CHECK(!missing.error().isEmpty());
    }

    // --- one puzzle into one item -----------------------------------------
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    PuzzleFeed feed(0);
    feed.setPaths(tmp.path(), themesPath);
    CHECK(feed.load());

    const QJsonArray puzzles = fixture();
    CHECK(puzzles.size() == 2);

    for (int i = 0; i < puzzles.size(); ++i) {
        const QJsonObject item = feed.itemFromPuzzle(puzzles.at(i).toObject());
        CHECK(!item.isEmpty());
        if (item.isEmpty())
            continue;

        const QString fen = item.value(QStringLiteral("fen")).toString();
        const QJsonArray line = item.value(QStringLiteral("line")).toArray();
        CHECK(!fen.isEmpty());
        CHECK(line.size() >= 1);
        // The line has to end on the learner's move (§6.5).
        CHECK((line.size() % 2) == 1);

        // **The off-by-one.** Every move of the line has to play from the
        // position the item claims. One half move early and the first move is
        // not even legal.
        core::Position position;
        CHECK(position.setFen(fen.toStdString()));
        bool playable = true;
        for (int move = 0; move < line.size() && playable; ++move)
            playable = position.play(line.at(move).toString().toStdString());
        CHECK(playable);

        CHECK(item.value(QStringLiteral("solution")).toString() == line.first().toString());
        CHECK(item.value(QStringLiteral("difficulty")).toInt() > 0);
        CHECK(item.value(QStringLiteral("id")).toString().startsWith(QLatin1String("li-")));
        CHECK(item.value(QStringLiteral("source")).toString() == QLatin1String("lichess-api"));
        // teacher.md §6.6: never a bare nothing. Where the themes have no
        // sentence, one is computed from the board.
        CHECK(!item.value(QStringLiteral("explanation")).toString().isEmpty());
    }

    // The first fixture puzzle is an endgame with a pin; the second is a mate
    // in three whose themes carry no sentence, so its explanation is computed.
    {
        const QJsonObject first = feed.itemFromPuzzle(puzzles.at(0).toObject());
        CHECK(first.value(QStringLiteral("dimension")).toString() == QLatin1String("END"));
        CHECK(first.value(QStringLiteral("difficulty")).toInt() == 1369);
        CHECK(first.value(QStringLiteral("line")).toArray().size() == 3);
        CHECK(first.value(QStringLiteral("explanation")).toString()
              .contains(QStringLiteral("Fesselung")));

        const QJsonObject second = feed.itemFromPuzzle(puzzles.at(1).toObject());
        CHECK(second.value(QStringLiteral("dimension")).toString() == QLatin1String("REC"));
        CHECK(second.value(QStringLiteral("line")).toArray().size() == 5);
        CHECK(second.value(QStringLiteral("explanation")).toString()
              .contains(QStringLiteral("Matt")));
    }

    // --- rubbish in, nothing out -------------------------------------------
    {
        QJsonObject broken = puzzles.at(0).toObject();
        QJsonObject puzzle = broken.value(QStringLiteral("puzzle")).toObject();
        puzzle.insert(QStringLiteral("initialPly"), 5000);
        broken.insert(QStringLiteral("puzzle"), puzzle);
        // A puzzle that cannot be replayed produces no item, and no crash.
        CHECK(feed.itemFromPuzzle(broken).isEmpty());

        CHECK(feed.itemFromPuzzle(QJsonObject()).isEmpty());
    }

    // --- nothing reaches the network unless it was allowed -----------------
    {
        CHECK(!feed.allowed());
        feed.setTarget(200);
        // Not allowed and no client: neither the background trickle nor an
        // explicit request may put anything on the wire.
        feed.scheduleRefill();
        feed.fetchNow();
        CHECK(feed.count() == 0);
        CHECK(!feed.fetching());

        // And held during a live game, whatever the setting says. The client
        // sends one request at a time (platform.md §3.4): a batch of fifty
        // must never sit in front of a move on a clock.
        feed.setHeld(true);
        feed.setAllowed(true);
        feed.fetchNow();
        CHECK(!feed.fetching());
        feed.setHeld(false);
    }

    if (failures) {
        std::fprintf(stderr, "\n%d Prüfung(en) fehlgeschlagen\n", failures);
        return 1;
    }
    std::printf("test_feed: alles grün\n");
    return 0;
}
