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
#ifndef SCHACH_GAMESYNC_H
#define SCHACH_GAMESYNC_H

// The learner's own Lichess games, downloaded into the existing SQLite store
// (platform.md §3.6, docs/design.md §6).
//
// **This is the point of the whole online feature.** The loop of the app is
// messen → spielen → die eigenen Fehler diagnostizieren → drillen → nachmessen
// (teacher.md §1.5), and until now the "spielen" step could only be a sparring
// game against our own engine. These are real games against real people, and
// they feed the same diagnosis.
//
// ────────────────────────────────────────────────────────────────────────────
// WHEN THE ENGINE IS ALLOWED TO LOOK AT THEM — read this before moving any
// call in this file or in its callers:
//
// A game downloaded here is a **finished** game. Running `Analyser` over a
// finished game is explicitly allowed: the Lichess fair-play rules forbid
// engine assistance "whilst a game you are involved in is ongoing"
// (platform.md §3.7), and nothing at all afterwards. That is exactly where the
// learning happens, and it is why this whole feature exists.
//
// Doing the same thing one move earlier — while the game is still running — is
// cheating, is detected automatically, and gets **the user's account** marked,
// not ours. So: the analysis of a game starts after `gameFinish`, never
// before, and `TeacherEngine::m_liveGameId` is what enforces it. Nobody may
// move an analysis call in front of the end of the game, however convenient
// a live evaluation would look on screen.
// ────────────────────────────────────────────────────────────────────────────
//
// The download itself is `GET /api/games/user/{u}` as ndjson, streamed and
// written incrementally, with `since` = the newest game already stored. At 60
// games per second (our own games, authenticated) a 5 000-game account is done
// in about eighty seconds once, and in seconds every time after that.

#include "Database.h"
#include "Lichess.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

namespace schach {

// One game as §3.6 delivers it, already turned into what the store and the
// analyser need. Pure data — `parseGame()` is a free function so it can be
// checked against recorded ndjson without a network and without an account.
struct SyncedGame {
    GameRecord record;
    QStringList moves;            // UCI
    bool learnerIsWhite = true;
    bool learnerPlayed = false;   // false when the name matches neither side
    bool standard = true;         // only standard chess is diagnosed
    bool finished = true;
    qint64 lastMoveAtMs = 0;
    QVector<int> evalCp;          // from `analysis`, empty when Lichess has none
};

// `username` decides which side the learner was on; the comparison is
// case-insensitive, because Lichess hands out both spellings.
bool parseSyncedGame(const QJsonObject& line, const QString& username, SyncedGame& out);

class GameSync : public QObject
{
    Q_OBJECT

public:
    GameSync(Lichess* api, Database* database, QObject* parent = 0);

    void setUsername(const QString& username);
    QString username() const { return m_username; }
    bool running() const { return m_reply != 0; }
    int imported() const { return m_imported; }
    int seen() const { return m_seen; }
    QString message() const { return m_message; }

    // Incremental: `since` is the newest Lichess game already in the store.
    void start();
    void cancel();

signals:
    void progress(int seen, int imported);
    void finished(int imported);
    void failed(const QString& sentence);
    // A finished game that has just been stored. The listener may hand it to
    // `Analyser` — see the block comment above: after the game, never during.
    void gameStored(qint64 databaseId, const QString& initialFen,
                    const QStringList& moves, bool learnerIsWhite);

private slots:
    void onReadyRead();
    void onFinished();

private:
    void consume(const QJsonObject& line);

    Lichess* m_api;
    Database* m_database;
    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply;
    NdjsonSplitter m_splitter;
    QString m_username;
    QString m_message;
    int m_imported;
    int m_seen;
};

} // namespace schach

#endif // SCHACH_GAMESYNC_H
