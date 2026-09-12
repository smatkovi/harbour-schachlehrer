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
// The regression test chess-spec/platform.md §3.7 asks for — the one rule that
// outranks every other design decision in this app:
//
//   "Eine Engine-Bewertung, ein Pfeil des besten Zuges, eine Eval-Leiste oder
//    eine Tablebase-Auskunft während einer laufenden Lichess-Partie ist
//    Cheating … Nicht wir werden gesperrt, sondern unsere Nutzer."
//
// and the architectural consequence it declares non-negotiable:
//
//   "eine einzige Instanzvariable `m_liveGameId`; ist sie gesetzt, verweigert
//    `EngineService::analyse()` jede Anfrage, der Analyse-Menüpunkt ist
//    ausgeblendet (nicht nur ausgegraut), die Tablebase-Abfrage ist gesperrt,
//    und der Engine-Prozess wird beim Start einer Lichess-Partie **beendet**,
//    nicht nur pausiert. Ein Regressionstest muss das prüfen."
//
// This is that test. It proves four things:
//
//   1. Every path into the engine refuses while a game is live —
//      analyseMovetime(), analyseNodes(), start(), available(), and through
//      TeacherEngine the hint, the analysis, the sparring opponent, the
//      placement test and the daily session.
//   2. The tablebase path refuses on its own.
//   3. The process is really **gone**, not paused: QProcess::state() is
//      NotRunning while the lock is on.
//   4. It all comes back afterwards, because analysing a finished game is
//      allowed and is where the learning happens.
//
// Plus the one thing a grep can prove and a reviewer forgets: the Bot API
// upgrade endpoint appears nowhere in the source tree (§3.1).
//
// No network is touched. The Lichess client is pointed at a discard port and
// driven with recorded ndjson through the same two functions the real streams
// use, so the test needs neither an account nor a connection.
#include "EngineProcess.h"
#include "Lichess.h"
#include "TeacherEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>

using namespace schach;

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

// A UCI engine in eleven lines of shell. It answers the handshake and every
// search; what it says is irrelevant here — the question is only whether it is
// ever asked, and whether it is running at all.
QString writeFakeEngine(const QString& directory)
{
    const QString path = directory + QStringLiteral("/attrappe-engine");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    file.write(
            "#!/bin/sh\n"
            "while IFS= read -r line; do\n"
            "  case \"$line\" in\n"
            "    uci) echo 'id name Attrappe 1.0'; echo 'uciok' ;;\n"
            "    isready) echo 'readyok' ;;\n"
            "    go*) echo 'info depth 6 multipv 1 score cp 24 nodes 4096 pv e2e4 e7e5';"
            " echo 'bestmove e2e4' ;;\n"
            "    quit) exit 0 ;;\n"
            "  esac\n"
            "done\n");
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

// Spin the event loop until `predicate` holds or the time is up.
template <typename Predicate>
bool waitFor(Predicate predicate, int milliseconds = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < milliseconds)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

QJsonObject json(const char* text)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray(text), &error);
    if (error.error != QJsonParseError::NoError)
        std::fprintf(stderr, "bad test fixture: %s\n", qPrintable(error.errorString()));
    return document.object();
}

// ---------------------------------------------------------------------------
// 1. The lock on EngineProcess itself.
// ---------------------------------------------------------------------------
void testEngineProcessLock(const QString& enginePath, const QString& syzygyPath)
{
    EngineProcess engine;
    engine.setEnginePath(enginePath);
    engine.setSyzygyPath(syzygyPath);

    CHECK(engine.binaryPresent());
    CHECK(engine.available());
    CHECK(!engine.fairPlayLocked());
    CHECK(engine.start());
    CHECK(waitFor([&] { return engine.ready(); }));
    CHECK(engine.ready());
    CHECK(engine.processRunning());

    // The doors are open before the lock: otherwise the test below would pass
    // for the wrong reason.
    const int before = engine.analyseMovetime(QString(), QStringList(), 50);
    CHECK(before > 0);
    CHECK(engine.tablebaseAvailable());
    const int probeBefore = engine.probeTablebase(
            QStringLiteral("8/8/8/8/8/4k3/4p3/4K3 w - - 0 1"));
    CHECK(probeBefore > 0);
    CHECK(waitFor([&] { return !engine.busy() && engine.pending() == 0; }));

    // ---- the Lichess game starts ----
    engine.setFairPlayLock(true);

    CHECK(engine.fairPlayLocked());
    // §3.7: terminated, not paused. This is the assertion the whole test
    // exists for.
    CHECK(!engine.processRunning());
    CHECK(!engine.ready());
    CHECK(!engine.available());
    // …while the binary is still perfectly present: the refusal is the lock,
    // not a missing file, and the app can say so.
    CHECK(engine.binaryPresent());

    // Every path in.
    CHECK(engine.analyseMovetime(QString(), QStringList(), 50) == 0);
    CHECK(engine.analyseNodes(QString(), QStringList(), 100000) == 0);
    CHECK(!engine.start());
    CHECK(!engine.processRunning());
    // The tablebase is its own path and refuses on its own.
    CHECK(!engine.tablebaseAvailable());
    CHECK(engine.probeTablebase(QStringLiteral("8/8/8/8/8/4k3/4p3/4K3 w - - 0 1")) == 0);
    CHECK(engine.pending() == 0);
    CHECK(!engine.fairPlayReason().isEmpty());

    // Nothing may leak out of a queue that was filled before the lock.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    CHECK(!engine.processRunning());

    // ---- the game is over ----
    engine.setFairPlayLock(false);
    CHECK(!engine.fairPlayLocked());
    CHECK(engine.available());
    CHECK(engine.start());
    CHECK(waitFor([&] { return engine.ready(); }));
    CHECK(engine.processRunning());
    CHECK(engine.analyseMovetime(QString(), QStringList(), 50) > 0);
    CHECK(engine.tablebaseAvailable());
    CHECK(waitFor([&] { return !engine.busy(); }));
    engine.stop();
    CHECK(!engine.processRunning());
}

// ---------------------------------------------------------------------------
// 2. The lock through the whole app, driven by recorded ndjson.
// ---------------------------------------------------------------------------
void testTeacherEngineLock(const QString& enginePath, const QString& syzygyPath,
                           const QString& databasePath)
{
    TeacherEngine teacher;
    // Nothing in this test may reach the network. Port 9 is discard; the
    // client never gets an answer and never needs one, because every line it
    // would have read is fed in by hand below.
    teacher.lichess()->setEndpoint(QStringLiteral("http://127.0.0.1:9"));
    teacher.setPaths(enginePath, syzygyPath, QString(), databasePath);
#ifdef SCHACH_SOURCE_DIR
    teacher.setItemBankPath(QStringLiteral(SCHACH_SOURCE_DIR "/assets/items/placement.json"));
#endif

    CHECK(waitFor([&] { return teacher.engineReady(); }));
    CHECK(teacher.engineReady());
    CHECK(teacher.analysisAvailable());
    CHECK(!teacher.liveGame());
    CHECK(teacher.liveGameId().isEmpty());
    CHECK(teacher.engine()->processRunning());

    // The app must be fully usable without any Lichess account at all. This is
    // the state every existing user is in, and M8 may not change it.
    CHECK(teacher.lichessState() == static_cast<int>(Lichess::LoggedOut));
    CHECK(!teacher.lichessLoggedIn());
    teacher.startSparring(0);
    CHECK(teacher.mode() == TeacherEngine::Sparring);
    teacher.startPlacement();
#ifdef SCHACH_SOURCE_DIR
    // With the item bank in place the test really runs; without it the app
    // says so and falls back to Idle, which is the honest answer either way.
    CHECK(teacher.mode() == TeacherEngine::Placement);
#endif
    CHECK(teacher.mode() != TeacherEngine::Online);
    CHECK(!teacher.prompt().isEmpty());

    // ---- a Lichess game starts: the recorded event-stream line ----
    teacher.lichess()->handleEventLine(json(
            "{\"type\":\"gameStart\",\"game\":{\"gameId\":\"g6jPpbuF\",\"fullId\":\"g6jPpbuFtKKT\","
            "\"color\":\"white\",\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\","
            "\"rated\":true,\"speed\":\"rapid\",\"hasMoved\":false,"
            "\"opponent\":{\"id\":\"aaron\",\"username\":\"Aaron\",\"rating\":1408},"
            "\"isMyTurn\":true,\"compat\":{\"bot\":false,\"board\":true},\"id\":\"g6jPpbuF\"}}"));

    CHECK(teacher.liveGame());
    CHECK(teacher.liveGameId() == QStringLiteral("g6jPpbuF"));
    CHECK(teacher.mode() == TeacherEngine::Online);

    // 1. The process is gone. Not paused, not idle — gone.
    CHECK(!teacher.engine()->processRunning());
    CHECK(teacher.engine()->fairPlayLocked());
    CHECK(!teacher.engineReady());

    // 2. The analysis entry is hidden, not greyed out: this is the property
    //    the pages bind `visible:` to.
    CHECK(!teacher.analysisAvailable());
    CHECK(!teacher.canAnalyseFinishedGame());
    CHECK(!teacher.fairPlayNotice().isEmpty());

    // 3. Every path into the engine refuses, and says why.
    CHECK(teacher.engine()->analyseMovetime(QString(), QStringList(), 50) == 0);
    CHECK(teacher.engine()->analyseNodes(QString(), QStringList(), 1000) == 0);
    CHECK(!teacher.engine()->start());

    // 4. The tablebase refuses.
    CHECK(!teacher.engine()->tablebaseAvailable());
    CHECK(teacher.engine()->probeTablebase(
                  QStringLiteral("8/8/8/8/8/4k3/4p3/4K3 w - - 0 1")) == 0);

    // 5. And the ways in through the facade: the hint, the analysis, sparring,
    //    the placement test and the daily session all refuse, the mode stays
    //    Online, and the engine process stays gone.
    teacher.requestHint();
    CHECK(teacher.feedback().value(QStringLiteral("key")).toString()
          == QStringLiteral("lichess.fairplay"));
    teacher.analyseCurrentGame();
    CHECK(!teacher.thinking());
    CHECK(teacher.mode() == TeacherEngine::Online);
    teacher.startSparring(0);
    CHECK(teacher.mode() == TeacherEngine::Online);
    teacher.startPlacement();
    CHECK(teacher.mode() == TeacherEngine::Online);
    teacher.startSession();
    CHECK(teacher.mode() == TeacherEngine::Online);
    teacher.analyseSyncedGame();
    CHECK(!teacher.thinking());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    CHECK(!teacher.engine()->processRunning());
    CHECK(!teacher.engineReady());

    // The board still follows the game — the lock is on the engine, not on the
    // app. (`gameFull` first, then a `gameState`, exactly as §3.5 describes.)
    teacher.lichess()->handleGameLine(json(
            "{\"type\":\"gameFull\",\"id\":\"g6jPpbuF\",\"rated\":true,"
            "\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},\"speed\":\"rapid\","
            "\"perf\":{\"name\":\"Rapid\"},\"createdAt\":1745112707998,"
            "\"white\":{\"id\":\"lernender\",\"name\":\"Lernender\",\"rating\":1380},"
            "\"black\":{\"id\":\"aaron\",\"name\":\"Aaron\",\"rating\":1408},"
            "\"initialFen\":\"startpos\",\"clock\":{\"initial\":600000,\"increment\":0},"
            "\"state\":{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":600000,"
            "\"btime\":600000,\"winc\":0,\"binc\":0,\"status\":\"started\"}}"));
    CHECK(teacher.moveList().size() >= 1);
    CHECK(teacher.liveGame());
    CHECK(!teacher.engine()->processRunning());

    // ---- the game ends: status leaves "started" (§3.5 step 6) ----
    teacher.lichess()->handleGameLine(json(
            "{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7\","
            "\"wtime\":580000,\"btime\":570000,\"winc\":0,\"binc\":0,"
            "\"status\":\"mate\",\"winner\":\"white\"}"));

    CHECK(!teacher.liveGame());
    CHECK(teacher.liveGameId().isEmpty());
    CHECK(!teacher.engine()->fairPlayLocked());
    CHECK(teacher.mode() == TeacherEngine::Review);

    // 6. And it comes back. Analysing a game that has ended is allowed, and it
    //    is the whole reason the online feature exists (platform.md §3.7,
    //    GameSync.h).
    CHECK(waitFor([&] { return teacher.engineReady(); }));
    CHECK(teacher.engine()->processRunning());
    CHECK(teacher.analysisAvailable());
    CHECK(teacher.canAnalyseFinishedGame());
    CHECK(teacher.engine()->available());
    CHECK(teacher.engine()->tablebaseAvailable());
    CHECK(teacher.engine()->analyseMovetime(QString(), QStringList(), 50) > 0);
    CHECK(!teacher.gameResult().isEmpty());
}

// ---------------------------------------------------------------------------
// 3. §3.1: the Bot API upgrade must not exist anywhere in the tree.
// ---------------------------------------------------------------------------
void testNoBotUpgrade()
{
#ifdef SCHACH_SOURCE_DIR
    const QString root = QStringLiteral(SCHACH_SOURCE_DIR);
    const QStringList directories = QStringList()
            << root + QStringLiteral("/src")
            << root + QStringLiteral("/src/core")
            << root + QStringLiteral("/sailfish")
            << root + QStringLiteral("/tests");
    int scanned = 0;
    for (int d = 0; d < directories.size(); ++d) {
        QDir directory(directories.at(d));
        const QStringList files = directory.entryList(
                QStringList() << QStringLiteral("*.cpp") << QStringLiteral("*.h")
                              << QStringLiteral("*.qml"),
                QDir::Files);
        for (int f = 0; f < files.size(); ++f) {
            QFile file(directory.filePath(files.at(f)));
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray content = file.readAll();
            ++scanned;
            // The upgrade is irreversible and turns the user's account into a
            // bot account. It may not appear, not even in a comment that some
            // later hand could uncomment.
            if (content.contains("api/bot/account/upgrade")
                    && !content.contains("must not be added")) {
                std::fprintf(stderr, "FAILED: %s mentions the bot upgrade endpoint\n",
                             qPrintable(files.at(f)));
                ++failures;
            }
        }
    }
    CHECK(scanned > 10);
#else
    std::fprintf(stderr, "note: SCHACH_SOURCE_DIR not defined, skipping the source scan\n");
#endif
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::fprintf(stderr, "FAILED: no temporary directory\n");
        return 1;
    }
    const QString enginePath = writeFakeEngine(directory.path());
    if (enginePath.isEmpty()) {
        std::fprintf(stderr, "FAILED: could not write the stand-in engine\n");
        return 1;
    }
    const QString syzygyPath = directory.path() + QStringLiteral("/syzygy");
    QDir().mkpath(syzygyPath);

    testEngineProcessLock(enginePath, syzygyPath);
    testTeacherEngineLock(enginePath, syzygyPath,
                          directory.path() + QStringLiteral("/schach.sqlite"));
    testNoBotUpgrade();

    if (failures == 0)
        std::printf("test_fairplay: alles grün\n");
    else
        std::fprintf(stderr, "test_fairplay: %d Prüfung(en) fehlgeschlagen\n", failures);
    return failures == 0 ? 0 : 1;
}
