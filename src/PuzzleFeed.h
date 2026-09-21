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
#ifndef SCHACH_PUZZLEFEED_H
#define SCHACH_PUZZLEFEED_H

// New exercises, fetched from Lichess at start and kept for offline use.
//
// Why this exists. The shipped bank is 3 944 items built on a build host out
// of the CC0 database export (tools/import_lichess_puzzles.py). That export is
// 304 MB; it is not something a phone downloads, and the app does not. But a
// bank that never grows runs into teacher.md §5.2 sooner or later — a position
// that comes back measures memory, not skill — and 3 944 items divided over six
// dimensions and sixteen difficulty bands is about forty per cell.
//
// So: `GET /api/puzzle/batch/mix?nb=50&difficulty=…`, which needs **no token**,
// hands out fifty at a time and costs a few kilobytes. The five difficulty
// levels are asked in turn, which is what spreads the pool over roughly 700 to
// 2 350 — measured, see the class comment in the .cpp.
//
// What it must never become: a requirement. teacher.md §0.2 makes "everything
// works offline" a non-goal to break — network access is optional and
// additive. With no connection, no account and no permission, the app runs on
// the shipped bank and nothing says otherwise.
//
// The idea is salichess' PuzzleStore (CREDITS/CODE.md); what is taken is the
// shape — a pool topped up while online, so that what is played comes out of
// it. The contents are converted into this app's own item format instead of
// being kept as Lichess puzzles, because the placement test picks by dimension
// and difficulty and knows nothing about themes.
//
// **What this cannot do, and what that costs.** The batch endpoint serves the
// `mix` angle only; a theme angle answers with a web page, not with JSON.
// Measured on 250 fetched puzzles: 2 % quiet move, 4 % defensive move. So the
// feed does **not** carry the mixture of teacher.md §6.3 — the shipped bank
// does, by construction, and the feed dilutes its share as the pool grows.
//
// That is tolerable because §6.3 states the quota for "Block A und jeden
// Transfertest", i.e. for what a *session* serves, not for what the bank
// holds; the bank's composition is a proxy the importer could enforce and this
// cannot. When the session builder of §5.6 grows a quiet quota of its own,
// this note is the reason it has to.

#include "ItemBank.h"
#include "Themes.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QSet>
#include <QString>
#include <QVector>

namespace schach {

class Lichess;

class PuzzleFeed : public QObject
{
    Q_OBJECT

public:
    explicit PuzzleFeed(Lichess* lichess, QObject* parent = 0);

    // `directory` is the app's own data directory; the pool is a file in it.
    // `themesPath` is assets/items/themes.json. Both are set once at start,
    // before load().
    void setPaths(const QString& directory, const QString& themesPath);
    QString poolPath() const;

    // Reads what was fetched before. Call before the first refill, and merge
    // poolPath() into the ItemBank afterwards.
    bool load();

    int count() const { return m_items.size(); }
    int target() const { return m_target; }
    void setTarget(int items);
    bool fetching() const { return m_pending; }
    // Empty while nothing has gone wrong. A sentence, never a status code.
    QString message() const { return m_message; }

    // Ask for what is missing after the usual pause. This is what start-up
    // and every automatic top-up use: the first request then lands a few
    // seconds in, when the board is already on screen, instead of competing
    // with the launch.
    void scheduleRefill();
    // The learner pressed "Jetzt holen": forget that it gave up, skip the
    // pause, ask again. An explicit request is not the background trickle.
    void fetchNow();
    // Off by default: nothing reaches the network until the learner says so.
    void setAllowed(bool allowed);
    bool allowed() const { return m_allowed; }

    // Held while a Lichess game is running. Not a setting the learner made and
    // not an error — the client sends one request at a time (platform.md
    // §3.4), and a batch of fifty puzzles must never sit in front of a move on
    // a clock. Nobody is waiting for the pool; the player is waiting for the
    // board.
    void setHeld(bool held);

    // Forgets every fetched item, file and all.
    void clear();

    // Exposed for the tests: turn one `{game, puzzle}` object of the Lichess
    // batch answer into an item. Returns an empty object when the puzzle
    // cannot be replayed — which is a data problem, not an error to report.
    QJsonObject itemFromPuzzle(const QJsonObject& entry) const;

signals:
    void changed();

private slots:
    void onJsonArrived(const QString& tag, int status, const QByteArray& body);
    void onPauseOver();

private:
    // Ask for what is missing now. Does nothing when the pool is full, when a
    // request is already out, when the pause is running, or when `allowed` is
    // false. Everything outside goes through scheduleRefill() or fetchNow().
    void refill();
    void save();
    void requestBatch();

    Lichess* m_lichess;
    Themes m_themes;
    QString m_directory;
    QString m_themesPath;
    QString m_message;
    QVector<QJsonObject> m_items;
    QSet<QString> m_ids;
    // Between batches, so that filling the pool is a trickle and not a burst.
    QTimer* m_pause;
    int m_target;
    int m_level;          // index into the five difficulty names
    int m_emptyRounds;    // how many answers in a row brought nothing new
    bool m_pending;
    bool m_allowed;
    bool m_held;
};

} // namespace schach

#endif // SCHACH_PUZZLEFEED_H
