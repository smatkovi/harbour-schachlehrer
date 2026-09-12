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
// Not one of the five core tests: a build-host smoke check for the Qt layer.
// It exercises the parts that do not need an engine binary — the database
// schema, the pure half of the analyser, the opening-trap list, the sparring
// budget and the QML facade's properties — so that a missing engine really is
// a normal state and not a crash (docs/design.md §5).
#include "Analyser.h"
#include "Database.h"
#include "EngineProcess.h"
#include "Sparring.h"
#include "TeacherEngine.h"

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
        std::fprintf(stderr, "FAILED line %d: %s\n", line, what);
        ++failures;
    }
}
#define CHECK(x) check((x), #x, __LINE__)
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- the database, created on first run --------------------------------
    {
        QTemporaryDir directory;
        Database database;
        CHECK(database.open(directory.path() + QStringLiteral("/schach/lernen.db")));
        CHECK(database.isOpen());
        CHECK(database.schemaVersion() == 1);
        CHECK(database.gameCount() == 0);

        GameRecord game;
        game.playedAt = 1757635200;
        game.white = QStringLiteral("Lernender");
        game.black = QStringLiteral("Trainingsgegner");
        game.result = QStringLiteral("0-1");
        game.pgn = QStringLiteral("1. e4 e5 2. Nf3 Nc6 0-1");
        const qint64 gameId = database.insertGame(game);
        CHECK(gameId > 0);
        CHECK(database.gameCount() == 1);

        QVector<PlyRecord> plies;
        PlyRecord ply;
        ply.ply = 0;
        ply.san = QStringLiteral("e4");
        ply.uci = QStringLiteral("e2e4");
        ply.fenBefore = QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
        ply.evalCp = 30;
        ply.hasEval = true;
        plies.append(ply);
        CHECK(database.insertPlies(gameId, plies));

        FindingRecord finding;
        finding.gameId = gameId;
        finding.ply = 6;
        finding.code = QStringLiteral("A1");
        finding.dimension = QStringLiteral("SRG");
        finding.severity = 31;
        finding.deltaW = 31.0;
        finding.sentence = QStringLiteral("Nf3 lässt deinen Läufer stehen.");
        CHECK(database.insertFinding(finding) > 0);
        CHECK(database.findingsOfGame(gameId).size() == 1);
        CHECK(database.blunderCount() == 1);
        const QVector<double> mass = database.errorMassByDimension();
        CHECK(mass.size() == core::kDimensionCount);
        CHECK(mass.at(static_cast<int>(core::Dimension::SRG)) > 30.0);

        // Cards survive a round trip with their whole FSRS state.
        core::Finding source;
        source.cls = core::ErrorClass::A3;
        source.dimension = core::dimensionOf(core::ErrorClass::A3);
        source.motif = core::Motif::Fork;
        source.dW = 31.0;
        source.fen = "7k/8/8/1n6/4R3/8/R6P/4K3 w - - 0 1";
        source.bestMove = "e4e5";
        core::Card card = core::cardFromFinding(source, 20000);
        card.srs = core::applyReview(card.srs, core::Rating::Good, 20000);
        CHECK(database.upsertCard(card));
        core::Card loaded;
        CHECK(database.loadCard(QString::fromStdString(card.id), loaded));
        CHECK(loaded.id == card.id);
        CHECK(loaded.motif == core::Motif::Fork);
        CHECK(loaded.dimension == core::Dimension::TAK);
        CHECK(loaded.srs.state == core::CardState::Review);
        CHECK(qAbs(loaded.srs.stability - card.srs.stability) < 1e-9);
        CHECK(database.dueCardCount(card.srs.dueDay) == 1);
        CHECK(database.dueCardCount(20000) == 0);
        CHECK(database.newCardsCreatedOn(20000) == 1);
        CHECK(database.recordReview(QString::fromStdString(card.id), 1757635200,
                                    core::Rating::Good, 12000, true, false));

        QVector<double> perDimension;
        for (int i = 0; i < core::kDimensionCount; ++i)
            perDimension.append(1100.0 + i);
        CHECK(database.recordSkill(1757635200, 1100.0, perDimension, 2.5));
        double theta = 0.0;
        QVector<double> back;
        CHECK(database.latestSkill(theta, back));
        CHECK(qAbs(theta - 1100.0) < 1e-9);
        CHECK(back.size() == core::kDimensionCount);
    }

    // --- the opening traps replay cleanly ----------------------------------
    {
        const QStringList traps = Analyser::knownTrapEpds();
        // Seven lines in teacher.md §8.6 (c); a mistyped one is dropped, so a
        // short list here means a typo in the table.
        CHECK(traps.size() == 7);
        for (int i = 0; i < traps.size(); ++i)
            CHECK(traps.at(i).count(QLatin1Char(' ')) == 3);   // a well-formed EPD
    }

    // --- the analyser without an engine ------------------------------------
    {
        GameInput input;
        input.learnerIsWhite = true;
        input.learnerElo = 1500;
        // 1.e4 e5 2.Nf3 Nc6 3.Bc4 Nd4 4.Nxe5 — the Blackburne trap.
        input.moves << QStringLiteral("e2e4") << QStringLiteral("e7e5")
                    << QStringLiteral("g1f3") << QStringLiteral("b8c6")
                    << QStringLiteral("f1c4") << QStringLiteral("c6d4")
                    << QStringLiteral("f3e5");
        QVector<PlyEval> evals(input.moves.size());
        PlyEval& sixth = evals[6];
        sixth.valid = true;
        sixth.before = core::Score::centipawns(20);
        sixth.after = core::Score::centipawns(-320);
        sixth.nullBefore = sixth.before;
        sixth.nullAfter = sixth.after;
        sixth.best = QStringLiteral("f3d4");
        sixth.opponentReply = QStringLiteral("d8g5");
        const QVector<core::Finding> findings = Analyser::diagnose(input, evals);
        CHECK(!findings.isEmpty());
        bool sawTrap = false;
        for (int i = 0; i < findings.size(); ++i) {
            sawTrap = sawTrap || findings.at(i).cls == core::ErrorClass::H4;
            CHECK(!findings.at(i).sentence.empty());
        }
        // The position after 4.Nxe5 is in the shipped trap list, and H4 is the
        // one opening class with high priority because it pays off at once.
        CHECK(sawTrap);

        const QVector<core::Card> cards = Analyser::cardsFor(findings, 20000);
        CHECK(!cards.isEmpty());
        for (int i = 0; i < cards.size() && i < core::kMaxNewErrorCardsPerDay; ++i)
            CHECK(cards.at(i).srs.dueDay == 20000);
    }

    // --- the sparring budget, §7.2 -----------------------------------------
    {
        CHECK(qAbs(Sparring::budgetFor(800) - 110.0) < 1e-9);
        CHECK(qAbs(Sparring::budgetFor(1100) - 75.0) < 1e-9);
        CHECK(qAbs(Sparring::budgetFor(1400) - 50.0) < 1e-9);
        CHECK(qAbs(Sparring::budgetFor(1700) - 32.0) < 1e-9);
        CHECK(qAbs(Sparring::budgetFor(2000) - 20.0) < 1e-9);
        CHECK(Sparring::budgetFor(800) > Sparring::budgetFor(2000));

        QVector<core::ErrorClass> wanted;
        wanted << core::ErrorClass::C2 << core::ErrorClass::A1;
        const QVector<PlannedMistake> plan = Sparring::planFor(1100, wanted);
        CHECK(plan.size() == 4);          // one big and three medium
        double sum = 0.0;
        for (int i = 0; i < plan.size(); ++i)
            sum += plan.at(i).size;
        CHECK(qAbs(sum - 73.0) < 1.0);    // ~28 + 3*15, inside the 75 budget

        Sparring sparring;
        sparring.reset(1100, wanted);
        CHECK(sparring.budgetLeft() > 0.0);
        CHECK(!sparring.hasPending());
        // The blunder-check drill fades as it is answered (§7.5).
        CHECK(sparring.blunderCheckEvery() == 3);
        for (int i = 0; i < 3; ++i)
            sparring.noteBlunderCheck(true);
        CHECK(sparring.blunderCheckEvery() == 5);
        for (int i = 0; i < 3; ++i)
            sparring.noteBlunderCheck(true);
        CHECK(sparring.blunderCheckEvery() == 8);
        for (int i = 0; i < 3; ++i)
            sparring.noteBlunderCheck(true);
        CHECK(sparring.blunderCheckEvery() == 0);
        sparring.requireBlunderCheck();   // an A1/B1/C2 event brings it back
        CHECK(sparring.blunderCheckEvery() == 3);
        CHECK(sparring.blunderCheckDue());

        // The threat list is checks and captures only: 11 % of the moves that
        // refute 72 % of all blunders (§1.2.2).
        core::Position position("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 4 4");
        const QStringList threats = Sparring::threatsToCheck(position);
        CHECK(threats.size() < static_cast<int>(position.legalMoves().size()));
    }

    // --- the QML facade without an engine ----------------------------------
    {
        TeacherEngine teacher;
        teacher.setPaths(QStringLiteral("/nonexistent/stockfish"), QString(), QString(), QString());
        CHECK(!teacher.engineReady());
        CHECK(!teacher.thinking());
        CHECK(teacher.mode() == TeacherEngine::Idle);
        CHECK(teacher.squares().size() == 64);
        CHECK(teacher.fen().startsWith(QStringLiteral("rnbqkbnr")));
        CHECK(teacher.whiteToMove());
        CHECK(teacher.gameResult().isEmpty());
        CHECK(teacher.moveList().isEmpty());
        CHECK(teacher.skills().size() == core::kDimensionCount);
        CHECK(!teacher.prompt().isEmpty());

        // The board works without an engine: selection, legal targets, a move.
        teacher.setSelectedSquare(core::squareFromName("e2"));
        CHECK(teacher.selectedSquare() == core::squareFromName("e2"));
        CHECK(teacher.legalTargets().size() == 2);
        CHECK(teacher.play(core::squareFromName("e2"), core::squareFromName("e4")));
        CHECK(!teacher.whiteToMove());
        CHECK(teacher.moveList().size() == 1);
        CHECK(teacher.lastMove() == QStringLiteral("e2e4"));
        CHECK(!teacher.play(core::squareFromName("a1"), core::squareFromName("a8")));
        // A piece of the side not to move cannot be picked up.
        teacher.setSelectedSquare(core::squareFromName("d1"));
        CHECK(teacher.selectedSquare() == -1);

        const QVariantMap session = teacher.session();
        CHECK(session.contains(QStringLiteral("blunderRate")));
        CHECK(session.contains(QStringLiteral("engineMessage")));   // said in plain words
        CHECK(teacher.lastFindings().isEmpty());

        // Sparring without an engine must not crash and must say why.
        teacher.startSparring(0);
        CHECK(teacher.mode() == TeacherEngine::Sparring);
        CHECK(teacher.play(core::squareFromName("e2"), core::squareFromName("e4")));
        const QVariantMap feedback = teacher.feedback();
        CHECK(feedback.contains(QStringLiteral("text")));
        CHECK(!feedback.value(QStringLiteral("text")).toString().isEmpty());
        // teacher.md §6.6: a sentence, never a bare number.
        CHECK(feedback.contains(QStringLiteral("key")));

        teacher.analyseCurrentGame();   // no engine: a sentence, not a crash
        CHECK(!teacher.feedback().value(QStringLiteral("text")).toString().isEmpty());
    }

    if (failures) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::printf("OK: Qt-Schicht (Datenbank, Analyse, Sparring, Fassade) ohne Engine\n");
    return 0;
}
