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
#include "GameSync.h"

#include "core/Position.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace schach {

namespace {

QString playerName(const QJsonObject& side)
{
    if (side.contains(QStringLiteral("aiLevel")))
        return QObject::tr("Lichess-Computer Stufe %1").arg(side.value(QStringLiteral("aiLevel")).toInt());
    const QJsonObject user = side.value(QStringLiteral("user")).toObject();
    const QString name = user.value(QStringLiteral("name")).toString();
    return name.isEmpty() ? user.value(QStringLiteral("id")).toString() : name;
}

QString playerId(const QJsonObject& side)
{
    return side.value(QStringLiteral("user")).toObject().value(QStringLiteral("id")).toString();
}

bool matches(const QJsonObject& side, const QString& username)
{
    if (username.isEmpty())
        return false;
    return playerId(side).compare(username, Qt::CaseInsensitive) == 0
            || playerName(side).compare(username, Qt::CaseInsensitive) == 0;
}

} // namespace

bool parseSyncedGame(const QJsonObject& line, const QString& username, SyncedGame& out)
{
    out = SyncedGame();
    const QString id = line.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return false;

    out.record.source = QStringLiteral("lichess");
    out.record.externalId = id;
    // Lichess counts in milliseconds, the store in seconds.
    const qint64 createdAt = static_cast<qint64>(line.value(QStringLiteral("createdAt")).toDouble());
    out.lastMoveAtMs = static_cast<qint64>(line.value(QStringLiteral("lastMoveAt")).toDouble());
    out.record.playedAt = (out.lastMoveAtMs > 0 ? out.lastMoveAtMs : createdAt) / 1000;

    const QJsonObject players = line.value(QStringLiteral("players")).toObject();
    const QJsonObject white = players.value(QStringLiteral("white")).toObject();
    const QJsonObject black = players.value(QStringLiteral("black")).toObject();
    out.record.white = playerName(white);
    out.record.black = playerName(black);

    if (matches(white, username)) {
        out.learnerIsWhite = true;
        out.learnerPlayed = true;
    } else if (matches(black, username)) {
        out.learnerIsWhite = false;
        out.learnerPlayed = true;
    }

    const QString status = line.value(QStringLiteral("status")).toString();
    const QString winner = line.value(QStringLiteral("winner")).toString();
    out.finished = !status.isEmpty() && status != QLatin1String("started")
            && status != QLatin1String("created");
    if (winner == QLatin1String("white"))
        out.record.result = QStringLiteral("1-0");
    else if (winner == QLatin1String("black"))
        out.record.result = QStringLiteral("0-1");
    else if (status == QLatin1String("draw") || status == QLatin1String("stalemate"))
        out.record.result = QStringLiteral("1/2-1/2");
    else
        out.record.result = QStringLiteral("*");

    const QString variant = line.value(QStringLiteral("variant")).toString();
    out.standard = variant.isEmpty() || variant == QLatin1String("standard");

    const QJsonObject opening = line.value(QStringLiteral("opening")).toObject();
    out.record.eco = opening.value(QStringLiteral("eco")).toString();
    out.record.opening = opening.value(QStringLiteral("name")).toString();

    const QJsonObject clock = line.value(QStringLiteral("clock")).toObject();
    if (!clock.isEmpty()) {
        out.record.timeControl = QStringLiteral("%1+%2")
                                         .arg(clock.value(QStringLiteral("initial")).toInt())
                                         .arg(clock.value(QStringLiteral("increment")).toInt());
    } else if (line.contains(QStringLiteral("daysPerTurn"))) {
        out.record.timeControl = QStringLiteral("%1d")
                                         .arg(line.value(QStringLiteral("daysPerTurn")).toInt());
    }

    const QString initialFen = line.value(QStringLiteral("initialFen")).toString();
    if (!initialFen.isEmpty() && initialFen != QLatin1String("startpos"))
        out.record.initialFen = initialFen;
    out.record.pgn = line.value(QStringLiteral("pgn")).toString();
    out.moves = splitMoves(line.value(QStringLiteral("moves")).toString());

    // §3.6: `evals=true` gives us whatever Lichess has already analysed. There
    // is no endpoint to ask for a new server analysis, so everything else is
    // computed locally — after the game, which is where we compute it anyway.
    const QJsonArray analysis = line.value(QStringLiteral("analysis")).toArray();
    for (int i = 0; i < analysis.size(); ++i) {
        const QJsonObject entry = analysis.at(i).toObject();
        if (entry.contains(QStringLiteral("eval")))
            out.evalCp.append(entry.value(QStringLiteral("eval")).toInt());
        else if (entry.contains(QStringLiteral("mate")))
            out.evalCp.append(entry.value(QStringLiteral("mate")).toInt() > 0 ? 10000 : -10000);
        else
            out.evalCp.append(0);
    }
    return true;
}

GameSync::GameSync(Lichess* api, Database* database, QObject* parent)
    : QObject(parent)
    , m_api(api)
    , m_database(database)
    , m_net(new QNetworkAccessManager(this))
    , m_reply(0)
    , m_imported(0)
    , m_seen(0)
{
}

void GameSync::setUsername(const QString& username)
{
    m_username = username;
}

void GameSync::start()
{
    if (m_reply || !m_api || !m_database)
        return;
    if (m_username.isEmpty()) {
        m_message = tr("Ich weiß noch nicht, wie dein Lichess-Konto heißt.");
        emit failed(m_message);
        return;
    }
    if (!m_database->isOpen()) {
        m_message = tr("Die Partiendatenbank ist nicht offen.");
        emit failed(m_message);
        return;
    }

    m_imported = 0;
    m_seen = 0;
    m_splitter.reset();

    // §3.6: the incremental rule. `since` is the newest Lichess game already
    // stored, in milliseconds; the first run has none and downloads everything
    // once. A second is added so the newest game itself does not come back.
    const qint64 newest = m_database->newestPlayedAt(QStringLiteral("lichess"));
    QString url = m_api->endpoint() + QStringLiteral("/api/games/user/")
            + QString::fromLatin1(QUrl::toPercentEncoding(m_username));
    url += QStringLiteral("?moves=true&pgnInJson=true&tags=true&opening=true"
                          "&evals=true&clocks=false&finished=true&sort=dateAsc");
    if (newest > 0)
        url += QStringLiteral("&since=%1").arg((newest + 1) * 1000LL);

    QNetworkRequest request((QUrl(url)));
    request.setRawHeader("Accept", "application/x-ndjson");
    const QByteArray bearer = m_api->bearer();
    // §3.6: our own games do not need a scope at all; a token only raises the
    // rate from 20 to 60 games per second.
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", bearer);

    m_message = tr("Ich hole deine Partien von Lichess.");
    m_reply = m_net->get(request);
    connect(m_reply, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    connect(m_reply, SIGNAL(finished()), this, SLOT(onFinished()));
}

void GameSync::cancel()
{
    if (!m_reply)
        return;
    QNetworkReply* reply = m_reply;
    m_reply = 0;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void GameSync::onReadyRead()
{
    if (!m_reply)
        return;
    // Streamed and written as it arrives: an account with thousands of games
    // must never be held in memory in one piece (§3.6).
    const QVector<QJsonObject> lines = m_splitter.feed(m_reply->readAll());
    for (int i = 0; i < lines.size(); ++i)
        consume(lines.at(i));
    if (!lines.isEmpty())
        emit progress(m_seen, m_imported);
}

void GameSync::consume(const QJsonObject& line)
{
    ++m_seen;
    SyncedGame game;
    if (!parseSyncedGame(line, m_username, game))
        return;
    if (!game.finished || !game.standard || !game.learnerPlayed)
        return;
    if (m_database->hasExternalId(game.record.externalId))
        return;

    const qint64 id = m_database->insertGame(game.record);
    if (id <= 0)
        return;
    ++m_imported;

    // The move list, so the diagnosis has something to walk over. EPD is what
    // gets indexed, the full FEN is only stored (platform.md §5.2).
    core::Position position(game.record.initialFen.isEmpty()
                                   ? std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
                                   : game.record.initialFen.toStdString());
    QVector<PlyRecord> plies;
    for (int i = 0; i < game.moves.size(); ++i) {
        const QString uci = normaliseCastling(position, game.moves.at(i));
        PlyRecord ply;
        ply.gameId = id;
        ply.ply = i;
        ply.uci = uci;
        ply.san = QString::fromStdString(position.sanOf(uci.toStdString()));
        ply.fenBefore = QString::fromStdString(position.epd());
        if (i < game.evalCp.size()) {
            ply.evalCp = game.evalCp.at(i);
            ply.hasEval = true;
        }
        plies.append(ply);
        game.moves[i] = uci;
        if (!position.play(uci.toStdString()))
            break;
    }
    m_database->insertPlies(id, plies);

    // The game is finished and stored. Whoever listens may analyse it now —
    // see the block comment in GameSync.h: after the game, never during. This
    // is the allowed half of platform.md §3.7 and the whole reason the online
    // feature exists.
    emit gameStored(id, game.record.initialFen, game.moves, game.learnerIsWhite);
}

void GameSync::onFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
        reply->deleteLater();
    if (reply != m_reply)
        return;
    m_reply = 0;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    // Whatever is still in the buffer without a trailing newline.
    const QVector<QJsonObject> rest = m_splitter.feed(reply->readAll() + QByteArray("\n"));
    for (int i = 0; i < rest.size(); ++i)
        consume(rest.at(i));

    if (status == 429) {
        // §3.4. There is no point retrying at once; the user can start the
        // sync again, and it is incremental, so nothing is lost.
        m_message = tr("Lichess bremst uns gerade aus. Versuch den Abgleich in "
                       "einer Minute noch einmal.");
        emit failed(m_message);
        return;
    }
    if ((status != 0 && (status < 200 || status >= 300))
            || (status == 0 && error != QNetworkReply::NoError)) {
        // A dead connection is not "nothing new"; saying so would quietly
        // convince the learner that Lichess has no games of his.
        m_message = tr("Deine Partien lassen sich gerade nicht abholen. "
                       "Was schon da ist, bleibt da.");
        emit failed(m_message);
        return;
    }
    m_message = m_imported == 0
            ? tr("Keine neuen Partien. Es war schon alles da.")
            : tr("%n neue Partie(n) übernommen.", "", m_imported);
    emit progress(m_seen, m_imported);
    emit finished(m_imported);
}

} // namespace schach
