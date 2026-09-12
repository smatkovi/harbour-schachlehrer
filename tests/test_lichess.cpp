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
// The Lichess protocol, checked against recorded ndjson — without an account
// and without touching the network (chess-spec/platform.md §3.5, §3.6).
//
// Everything here is fed through the very functions the live streams use, so
// what passes here is what will run on the device. The lines are the examples
// from the official OpenAPI specification, byte for byte where possible.
//
// The two pitfalls of §3.5 get a test each, because both are silent when they
// go wrong:
//   * `gameState.moves` is the complete list every time, never a delta.
//   * castling arrives king-takes-rook (`e1h1`), Chess960-compatible.
#include "Database.h"
#include "GameSync.h"
#include "Lichess.h"
#include "core/Position.h"

#include <QCoreApplication>
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

QJsonObject json(const QByteArray& text)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text, &error);
    if (error.error != QJsonParseError::NoError)
        std::fprintf(stderr, "bad fixture: %s\n", qPrintable(error.errorString()));
    return document.object();
}

// --- the splitter ------------------------------------------------------------
void testSplitter()
{
    NdjsonSplitter splitter;

    // A line that arrives in three pieces is still one line.
    CHECK(splitter.feed(QByteArray("{\"type\":\"ga")).isEmpty());
    CHECK(splitter.feed(QByteArray("meStart\",\"game\":{\"gam")).isEmpty());
    QVector<QJsonObject> out = splitter.feed(QByteArray("eId\":\"abcd1234\"}}\n"));
    CHECK(out.size() == 1);
    CHECK(out.first().value(QStringLiteral("type")).toString() == QStringLiteral("gameStart"));

    // Empty lines are the keep-alive of §3.5: counted, never delivered.
    out = splitter.feed(QByteArray("\n\n\n"));
    CHECK(out.isEmpty());
    CHECK(splitter.keepAlives() == 3);

    // Two objects in one chunk.
    out = splitter.feed(QByteArray("{\"type\":\"a\"}\n{\"type\":\"b\"}\n"));
    CHECK(out.size() == 2);
    CHECK(out.at(1).value(QStringLiteral("type")).toString() == QStringLiteral("b"));

    // Rubbish is skipped, not fatal — one bad line must not kill a game.
    out = splitter.feed(QByteArray("{not json}\n{\"type\":\"c\"}\n"));
    CHECK(out.size() == 1);
    CHECK(splitter.malformed() == 1);

    // A megabyte without a newline is not ndjson.
    NdjsonSplitter guard;
    guard.feed(QByteArray(2 << 20, 'x'));
    CHECK(guard.overflowed());
}

// --- gameFull / gameState ----------------------------------------------------
void testGameStream()
{
    // Verbatim from schemas/GameFullEvent.yaml.
    const QByteArray gameFull =
            "{\"id\":\"BEOucQJo\",\"variant\":{\"key\":\"standard\",\"name\":\"Standard\","
            "\"short\":\"Std\"},\"speed\":\"rapid\",\"perf\":{\"name\":\"Rapid\"},"
            "\"rated\":false,\"createdAt\":1745112707998,"
            "\"white\":{\"id\":\"bobby\",\"name\":\"Bobby\",\"title\":null,\"rating\":1751},"
            "\"black\":{\"id\":\"mary\",\"name\":\"Mary\",\"title\":null,\"rating\":1021},"
            "\"initialFen\":\"startpos\",\"clock\":{\"initial\":900000,\"increment\":0},"
            "\"type\":\"gameFull\",\"state\":{\"type\":\"gameState\",\"moves\":\"d2d3\","
            "\"wtime\":900000,\"btime\":900000,\"winc\":0,\"binc\":0,\"status\":\"started\"}}";

    LichessGame game;
    CHECK(parseGameFull(json(gameFull), game, QStringLiteral("mary")));
    CHECK(game.valid);
    CHECK(game.id == QStringLiteral("BEOucQJo"));
    CHECK(game.variant == QStringLiteral("standard"));
    CHECK(game.speed == QStringLiteral("rapid"));
    CHECK(!game.rated);
    // "startpos" is not a FEN and must not be handed to the board as one.
    CHECK(game.initialFen.isEmpty());
    CHECK(game.initialMs == 900000);
    CHECK(game.incrementMs == 0);
    CHECK(game.white.name == QStringLiteral("Bobby"));
    CHECK(game.black.rating == 1021);
    // Our own side comes from the account id, not from the display name.
    CHECK(!game.weAreWhite);
    CHECK(game.opponent().name == QStringLiteral("Bobby"));
    CHECK(game.us().name == QStringLiteral("Mary"));
    CHECK(game.moves.size() == 1);
    CHECK(game.started());
    CHECK(!game.finished());
    // One half move played, so it is Black's turn, and we are Black.
    CHECK(game.ourTurn());

    // …and the other way round for the same line.
    LichessGame asWhite;
    CHECK(parseGameFull(json(gameFull), asWhite, QStringLiteral("BOBBY")));
    CHECK(asWhite.weAreWhite);
    CHECK(!asWhite.ourTurn());

    // §3.5, first pitfall: every gameState carries the full move list.
    const QByteArray state =
            "{\"type\":\"gameState\",\"moves\":\"e2e4 c7c5 f2f4 d7d6 g1f3 b8c6\","
            "\"wtime\":7598040,\"btime\":8395220,\"winc\":10000,\"binc\":10000,"
            "\"wdraw\":false,\"bdraw\":true,\"wtakeback\":false,\"btakeback\":false,"
            "\"status\":\"started\"}";
    CHECK(parseGameState(json(state), game));
    CHECK(game.moves.size() == 6);
    CHECK(game.moves.last() == QStringLiteral("b8c6"));
    CHECK(game.whiteMs == 7598040);
    CHECK(game.blackIncMs == 10000);
    CHECK(game.blackOffersDraw);
    // We are Black in this game, so a black draw offer is ours, not theirs.
    CHECK(game.weOfferDraw());
    CHECK(!game.opponentOffersDraw());
    CHECK(!game.opponentWantsTakeback());
    // Six half moves: White to move, and we are Black.
    CHECK(!game.ourTurn());

    // A takeback request from the other side.
    CHECK(parseGameState(json("{\"type\":\"gameState\",\"moves\":\"e2e4 c7c5\",\"wtime\":1,"
                              "\"btime\":1,\"winc\":0,\"binc\":0,\"wtakeback\":true,"
                              "\"status\":\"started\"}"),
                         game));
    CHECK(game.opponentWantsTakeback());

    // §3.5: the first move has a deadline, or the game is aborted.
    CHECK(parseGameState(json("{\"type\":\"gameState\",\"moves\":\"\",\"wtime\":600000,"
                              "\"btime\":600000,\"winc\":0,\"binc\":0,\"status\":\"started\","
                              "\"expiration\":{\"idleMillis\":4000,\"millisToMove\":30000}}"),
                         game));
    CHECK(game.millisToMove == 30000);
    CHECK(game.moves.isEmpty());

    // The end. Everything that is not "started"/"created" is a finished game,
    // and that is a normal end, not an error (§3.5 step 6).
    CHECK(parseGameState(json("{\"type\":\"gameState\",\"moves\":\"e2e4 e7e5\",\"wtime\":1,"
                              "\"btime\":1,\"winc\":0,\"binc\":0,\"status\":\"resign\","
                              "\"winner\":\"white\"}"),
                         game));
    CHECK(game.finished());
    CHECK(game.winner == QStringLiteral("white"));

    // opponentGone, with the seconds until a win may be claimed.
    CHECK(parseOpponentGone(json("{\"type\":\"opponentGone\",\"gone\":true,"
                                 "\"claimWinInSeconds\":8}"),
                            game));
    CHECK(game.opponentGone);
    CHECK(game.claimWinInSeconds == 8);

    // Wrong types are rejected rather than half-parsed.
    LichessGame untouched;
    CHECK(!parseGameState(json("{\"type\":\"chatLine\",\"text\":\"hi\"}"), untouched));
    CHECK(!parseGameFull(json("{\"type\":\"gameState\"}"), untouched, QString()));
}

// --- the castling notation ----------------------------------------------------
void testCastlingNotation()
{
    // §3.5, second pitfall: the stream writes the king onto its own rook.
    core::Position position;
    const char* moves[] = { "e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "f8c5" };
    for (int i = 0; i < 6; ++i)
        CHECK(position.play(moves[i]));

    CHECK(normaliseCastling(position, QStringLiteral("e1h1")) == QStringLiteral("e1g1"));
    CHECK(position.isLegal(normaliseCastling(position, QStringLiteral("e1h1")).toStdString()));
    // The standard spelling is passed through untouched.
    CHECK(normaliseCastling(position, QStringLiteral("e1g1")) == QStringLiteral("e1g1"));
    // An ordinary move is never touched.
    CHECK(normaliseCastling(position, QStringLiteral("d2d4")) == QStringLiteral("d2d4"));
    CHECK(normaliseCastling(position, QStringLiteral("e7e8q")) == QStringLiteral("e7e8q"));
    CHECK(normaliseCastling(position, QStringLiteral("")) == QStringLiteral(""));

    // Long castling, and the black side.
    core::Position black("r3kbnr/pppqpppp/2np4/8/8/2NP4/PPPQPPPP/R3KBNR b KQkq - 0 1");
    CHECK(normaliseCastling(black, QStringLiteral("e8a8")) == QStringLiteral("e8c8"));
    CHECK(black.isLegal("e8c8"));

    CHECK(splitMoves(QStringLiteral("")).isEmpty());
    CHECK(splitMoves(QStringLiteral("  ")).isEmpty());
    CHECK(splitMoves(QStringLiteral("e2e4  e7e5")).size() == 2);
}

// --- the event stream ---------------------------------------------------------
void testEventStream()
{
    Lichess api;
    // Nothing in this test may reach the network; port 9 is discard.
    api.setEndpoint(QStringLiteral("http://127.0.0.1:9"));

    // Counted with plain lambdas rather than QSignalSpy: the tests link Core,
    // Network and Sql, and one assertion is not worth a dependency on QtTest.
    int startedCount = 0;
    QString startedId;
    int finishedCount = 0;
    QString finishedStatus, finishedWinner;
    int challengesCount = 0;
    QObject::connect(&api, &Lichess::gameStarted, [&](const QString& id) {
        ++startedCount;
        startedId = id;
    });
    QObject::connect(&api, &Lichess::gameFinished,
                     [&](const QString&, const QString& status, const QString& winner) {
        ++finishedCount;
        finishedStatus = status;
        finishedWinner = winner;
    });
    QObject::connect(&api, &Lichess::challengesChanged, [&] { ++challengesCount; });

    // Verbatim from examples/stream-gameStart.json.yaml.
    api.handleEventLine(json(
            "{\"type\":\"gameStart\",\"game\":{\"fullId\":\"g6jPpbuFtKKT\","
            "\"gameId\":\"g6jPpbuF\",\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\","
            "\"color\":\"white\",\"lastMove\":\"\",\"source\":\"friend\","
            "\"status\":{\"id\":20,\"name\":\"started\"},"
            "\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},\"speed\":\"blitz\","
            "\"perf\":\"blitz\",\"rated\":true,\"hasMoved\":false,"
            "\"opponent\":{\"id\":\"aaron\",\"username\":\"Aaron\",\"rating\":808},"
            "\"isMyTurn\":true,\"secondsLeft\":300,\"rating\":830,"
            "\"compat\":{\"bot\":false,\"board\":true},\"id\":\"g6jPpbuF\"}}"));
    CHECK(startedCount == 1);
    CHECK(startedId == QStringLiteral("g6jPpbuF"));
    CHECK(api.game().id == QStringLiteral("g6jPpbuF"));
    CHECK(api.game().weAreWhite);
    CHECK(api.game().rated);
    CHECK(api.game().opponent().name == QStringLiteral("Aaron"));

    // A challenge arrives, then is cancelled again.
    api.handleEventLine(json(
            "{\"type\":\"challenge\",\"challenge\":{\"id\":\"JzJNN8d9\","
            "\"url\":\"https://lichess.org/JzJNN8d9\",\"status\":\"created\","
            "\"challenger\":{\"name\":\"Adriana\",\"id\":\"adriana\",\"rating\":633},"
            "\"destUser\":{\"name\":\"Gabriela\",\"id\":\"gabriela\",\"rating\":1530},"
            "\"variant\":{\"key\":\"standard\",\"name\":\"Standard\"},\"rated\":false,"
            "\"speed\":\"rapid\",\"timeControl\":{\"type\":\"clock\",\"limit\":600,"
            "\"increment\":5},\"color\":\"random\"},"
            "\"compat\":{\"bot\":false,\"board\":true}}"));
    CHECK(api.challenges().size() == 1);
    CHECK(api.challenges().first().challengerName == QStringLiteral("Adriana"));
    CHECK(api.challenges().first().timeControl == QStringLiteral("10+5"));
    CHECK(api.challenges().first().boardCompatible);
    CHECK(challengesCount >= 1);

    api.handleEventLine(json("{\"type\":\"challengeCanceled\",\"challenge\":{\"id\":\"JzJNN8d9\"}}"));
    CHECK(api.challenges().isEmpty());

    // And the game ends through the event stream as well (§3.5 step 6: both
    // the closed game stream and gameFinish are normal ends).
    api.handleEventLine(json(
            "{\"type\":\"gameFinish\",\"game\":{\"gameId\":\"g6jPpbuF\",\"color\":\"white\","
            "\"status\":{\"id\":31,\"name\":\"resign\"},\"winner\":\"black\","
            "\"id\":\"g6jPpbuF\"}}"));
    CHECK(finishedCount == 1);
    CHECK(finishedStatus == QStringLiteral("resign"));
    CHECK(finishedWinner == QStringLiteral("black"));

    // A challenge that the Board API cannot play is marked as such rather than
    // offered and then refused by the server (§3.1).
    api.handleEventLine(json(
            "{\"type\":\"challenge\",\"challenge\":{\"id\":\"zzz\",\"status\":\"created\","
            "\"challenger\":{\"name\":\"Bot\",\"id\":\"bot\"},"
            "\"variant\":{\"key\":\"crazyhouse\"},\"rated\":true,\"speed\":\"bullet\","
            "\"timeControl\":{\"type\":\"unlimited\"}},"
            "\"compat\":{\"bot\":true,\"board\":false}}"));
    CHECK(api.challenges().size() == 1);
    CHECK(!api.challenges().first().boardCompatible);
}

// --- the token ----------------------------------------------------------------
void testToken(const QString& directory)
{
    Lichess api;
    api.setEndpoint(QStringLiteral("http://127.0.0.1:9"));
    api.setDataDirectory(directory);
    CHECK(api.tokenPath() == directory + QStringLiteral("/lichess.token"));
    // No token, no login — and no network traffic either.
    CHECK(!api.loadToken());
    CHECK(api.state() == Lichess::LoggedOut);

    // A token from an earlier run is picked up on start.
    QFile file(api.tokenPath());
    CHECK(file.open(QIODevice::WriteOnly));
    file.write("lio_TestTokenTestTokenTestToken0123456789");
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    CHECK(api.loadToken());
    CHECK(api.state() == Lichess::LoggedIn);
    CHECK(api.hasToken());
    CHECK(api.bearer().startsWith("Bearer lio_"));

    // §3.3: the file is the app's own, and nobody else's business.
    CHECK((QFile::permissions(api.tokenPath())
           & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther))
          == 0);

    // Logging out deletes it. That is the visible way out of §3.3.
    api.logOut();
    CHECK(!QFile::exists(api.tokenPath()));
    CHECK(!api.hasToken());
    CHECK(api.state() == Lichess::LoggedOut);
}

// --- the game download (§3.6) --------------------------------------------------
void testGameSync(const QString& databasePath)
{
    // Verbatim shape from §3.6, with the fields the store needs.
    const QByteArray line =
            "{\"id\":\"JgnYgdxp\",\"rated\":true,\"variant\":\"standard\",\"speed\":\"rapid\","
            "\"perf\":\"rapid\",\"createdAt\":1789196017165,\"lastMoveAt\":1789196517165,"
            "\"status\":\"mate\",\"source\":\"pool\","
            "\"players\":{\"white\":{\"user\":{\"name\":\"Lernender\",\"id\":\"lernender\"},"
            "\"rating\":1204,\"ratingDiff\":2},"
            "\"black\":{\"user\":{\"name\":\"Aaron\",\"id\":\"aaron\"},\"rating\":1230}},"
            "\"winner\":\"white\",\"opening\":{\"eco\":\"C50\",\"name\":\"Italienisch\"},"
            "\"clock\":{\"initial\":600,\"increment\":5,\"totalTime\":720},"
            "\"moves\":\"e2e4 e7e5 g1f3 b8c6 f1c4\",\"pgn\":\"1. e4 e5 2. Nf3 Nc6 3. Bc4 1-0\","
            "\"analysis\":[{\"eval\":23},{\"eval\":-11},{\"eval\":30},{\"mate\":2},{\"eval\":5}]}";

    SyncedGame game;
    CHECK(parseSyncedGame(json(line), QStringLiteral("lernender"), game));
    CHECK(game.record.externalId == QStringLiteral("JgnYgdxp"));
    CHECK(game.record.source == QStringLiteral("lichess"));
    // Lichess counts milliseconds, the store seconds.
    CHECK(game.record.playedAt == 1789196517);
    CHECK(game.record.white == QStringLiteral("Lernender"));
    CHECK(game.record.black == QStringLiteral("Aaron"));
    CHECK(game.record.result == QStringLiteral("1-0"));
    CHECK(game.record.eco == QStringLiteral("C50"));
    CHECK(game.record.opening == QStringLiteral("Italienisch"));
    CHECK(game.record.timeControl == QStringLiteral("600+5"));
    CHECK(game.record.initialFen.isEmpty());
    CHECK(!game.record.pgn.isEmpty());
    CHECK(game.moves.size() == 5);
    CHECK(game.learnerPlayed);
    CHECK(game.learnerIsWhite);
    CHECK(game.finished);
    CHECK(game.standard);
    CHECK(game.evalCp.size() == 5);
    CHECK(game.evalCp.at(0) == 23);
    CHECK(game.evalCp.at(3) == 10000);

    // The name comparison is case-insensitive in both directions, because
    // Lichess hands out both spellings.
    SyncedGame asBlack;
    CHECK(parseSyncedGame(json(line), QStringLiteral("AARON"), asBlack));
    CHECK(asBlack.learnerPlayed);
    CHECK(!asBlack.learnerIsWhite);

    // Somebody else's game is not ours to diagnose.
    SyncedGame stranger;
    CHECK(parseSyncedGame(json(line), QStringLiteral("someone-else"), stranger));
    CHECK(!stranger.learnerPlayed);

    // A running game is never imported — §3.6 delays them on purpose, and a
    // half game would be diagnosed as a blunder-strewn disaster.
    SyncedGame ongoing;
    CHECK(parseSyncedGame(json("{\"id\":\"ZZZ\",\"status\":\"started\",\"createdAt\":1,"
                               "\"lastMoveAt\":2,\"players\":{},\"moves\":\"e2e4\"}"),
                          QStringLiteral("lernender"), ongoing));
    CHECK(!ongoing.finished);

    // A variant is not what the taxonomy was written for.
    SyncedGame variant;
    CHECK(parseSyncedGame(json("{\"id\":\"YYY\",\"status\":\"mate\",\"variant\":\"atomic\","
                               "\"createdAt\":1,\"lastMoveAt\":2,\"players\":{}}"),
                          QStringLiteral("lernender"), variant));
    CHECK(!variant.standard);

    CHECK(!parseSyncedGame(json("{\"nope\":1}"), QStringLiteral("x"), variant));

    // The incremental watermark of §3.6: `since` is the newest game we have.
    Database database;
    CHECK(database.open(databasePath));
    CHECK(database.newestPlayedAt(QStringLiteral("lichess")) == 0);
    CHECK(database.insertGame(game.record) > 0);
    CHECK(database.newestPlayedAt(QStringLiteral("lichess")) == 1789196517);
    CHECK(database.hasExternalId(QStringLiteral("JgnYgdxp")));
    CHECK(!database.hasExternalId(QStringLiteral("nothing")));
    CHECK(database.gameCountOfSource(QStringLiteral("lichess")) == 1);
    CHECK(database.gameCountOfSource(QStringLiteral("local")) == 0);
    // The local games of the learning mode are untouched by all of this.
    GameRecord local;
    local.playedAt = 1789200000;
    local.white = QStringLiteral("Lernender");
    CHECK(database.insertGame(local) > 0);
    CHECK(database.newestPlayedAt(QStringLiteral("lichess")) == 1789196517);
    CHECK(database.gameCount() == 2);
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

    testSplitter();
    testGameStream();
    testCastlingNotation();
    testEventStream();
    testToken(directory.path());
    testGameSync(directory.path() + QStringLiteral("/lichess.sqlite"));

    if (failures == 0)
        std::printf("test_lichess: alles grün\n");
    else
        std::fprintf(stderr, "test_lichess: %d Prüfung(en) fehlgeschlagen\n", failures);
    return failures == 0 ? 0 : 1;
}
