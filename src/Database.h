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
#ifndef SCHACH_DATABASE_H
#define SCHACH_DATABASE_H

// SQLite through QtSql, created on first run (docs/design.md §6,
// platform.md §5.1): games, plies, findings, cards, reviews, skills.
//
// Operating rules from platform.md §5.1: WAL, synchronous NORMAL, the schema
// version in PRAGMA user_version, and every import in one transaction —
// otherwise 5 000 games cost minutes instead of seconds. EPD is what gets
// indexed; the full FEN is only stored.
//
// The layout already has the columns a later Lichess import needs (source,
// ext_id), because docs/design.md §1 says stage 1 has no network but the
// storage must not have to be rewritten for stage 2.

#include "core/Card.h"
#include "core/Taxonomy.h"

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace schach {

struct GameRecord {
    qint64 id = -1;
    QString source = QStringLiteral("local");
    QString externalId;
    qint64 playedAt = 0;           // seconds since the epoch
    QString white, black, result;
    QString eco, opening, timeControl;
    QString initialFen, pgn;
};

struct PlyRecord {
    qint64 gameId = -1;
    int ply = 0;
    QString san, uci, fenBefore;
    int evalCp = 0;
    int evalMate = 0;              // 0 = none
    QString bestUci;
    int bestEvalCp = 0;
    int clockMs = -1;
    bool hasEval = false;
};

struct FindingRecord {
    qint64 id = -1;
    qint64 gameId = -1;
    int ply = 0;
    QString code;                  // the stable error key, never the enum value
    QString dimension;
    int severity = 0;
    double deltaW = 0.0;
    QString fen, playedUci, bestUci, motif, sentence;
};

// How often one error class turned up in the window, and when the most recent
// of them was played. This is what the thinking routine of core/Routine.h
// orders itself by (teacher.md §6.6 hints, §7.5 fade-out): the learner's own
// record, not a fixed list.
struct ClassTally {
    QString code;              // the stable error key, "A1", "B1", …
    int count = 0;
    qint64 lastPlayedAt = 0;   // seconds since the epoch, 0 when unknown
};

class Database : public QObject
{
    Q_OBJECT

public:
    explicit Database(QObject* parent = 0);
    ~Database();

    // `path` is a file; the directory is created if needed. An empty path
    // opens an in-memory database, which is what the tests use.
    bool open(const QString& path);
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }
    int schemaVersion() const;

    // --- games and moves ----------------------------------------------------
    qint64 insertGame(const GameRecord& game);
    bool insertPlies(qint64 gameId, const QVector<PlyRecord>& plies);
    QVector<GameRecord> recentGames(int limit = 10) const;
    int gameCount() const;
    // What the incremental Lichess sync needs (platform.md §3.6): the newest
    // game we already have from that source, so the next download can ask for
    // `since`, and the question whether one particular game is already here.
    // `playedAt` is in seconds; the Lichess parameter is in milliseconds.
    qint64 newestPlayedAt(const QString& source) const;
    bool hasExternalId(const QString& externalId) const;
    qint64 gameIdOfExternalId(const QString& externalId) const;
    int gameCountOfSource(const QString& source) const;
    int ownMoveCount() const;

    // --- findings -----------------------------------------------------------
    qint64 insertFinding(const FindingRecord& finding);
    QVector<FindingRecord> findingsOfGame(qint64 gameId) const;
    // The error mass per dimension over the last `games` games — this is a_d
    // (teacher.md §3.2 b), the actual diagnosis.
    QVector<double> errorMassByDimension(int games = 10) const;
    // The same window, counted per error class instead of per dimension.
    QVector<ClassTally> findingTallies(int games = 10) const;
    int blunderCount(int games = 10) const;

    // --- cards and reviews --------------------------------------------------
    bool upsertCard(const core::Card& card);
    bool loadCard(const QString& id, core::Card& card) const;
    QVector<core::Card> dueCards(qint64 today, int limit = 50) const;
    int dueCardCount(qint64 today) const;
    int backlogCount(qint64 today) const;
    int newCardsCreatedOn(qint64 day) const;
    bool recordReview(const QString& cardId, qint64 at, core::Rating rating, int milliseconds,
                      bool correct, bool usedHint);

    // --- skills -------------------------------------------------------------
    bool recordSkill(qint64 at, double theta, const QVector<double>& thetaPerDimension,
                     double blunderRate);
    bool latestSkill(double& theta, QVector<double>& thetaPerDimension) const;
    // When the last measurement was taken, 0 if there never was one. The start
    // page needs to tell "never measured" from "measured and unremarkable".
    qint64 lastMeasuredAt() const;

private:
    bool exec(const QString& sql);
    bool createSchema();

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_lastError;
};

} // namespace schach

#endif // SCHACH_DATABASE_H
