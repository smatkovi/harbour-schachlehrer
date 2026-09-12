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
#ifndef SCHACH_ANALYSER_H
#define SCHACH_ANALYSER_H

// Stage ③ of the loop (teacher.md §1.5): a finished game plus one engine
// verdict per half move become error events, and those become practice cards.
//
// The split in this class is deliberate. diagnose() is a *pure* function over
// pre-computed evaluations, so the whole diagnosis can be checked without an
// engine binary; start() only drives EngineProcess to produce those
// evaluations. Two engine calls per half move are enough (P_before with
// MultiPV 3, P_after with MultiPV 1); the null-move call is a third and is
// only made when the event actually crosses the threshold.

#include "EngineProcess.h"
#include "core/Card.h"
#include "core/Position.h"
#include "core/Taxonomy.h"

#include <QMetaType>
#include <QObject>
#include <QStringList>
#include <QVector>

namespace schach {

// Everything the engine has to say about one half move of the learner.
struct PlyEval {
    bool valid = false;
    core::Score before, after, nullBefore, nullAfter, second, third;
    QString best, opponentReply;
    QStringList bestPv;
    core::Tb tbBefore = core::Tb::Unknown;
    core::Tb tbAfter = core::Tb::Unknown;
    bool bestIsOnlyMove = false;
    bool refutationConfirmed = true;
};

struct GameInput {
    QString initialFen;            // empty = start position
    QStringList moves;             // UCI, the whole game
    bool learnerIsWhite = true;
    int learnerElo = 1000;
    QVector<int> moveTimesMs;      // per half move, -1 where unknown
    double baseTimeMs = -1.0;      // for the time-trouble rule U5
    qint64 gameId = -1;
};

class Analyser : public QObject
{
    Q_OBJECT

public:
    explicit Analyser(EngineProcess* engine, QObject* parent = 0);

    // teacher.md §2.2 [EMPFEHLUNG]: a fixed node count, not a fixed depth.
    void setNodeBudget(qint64 nodes) { m_nodes = nodes; }
    qint64 nodeBudget() const { return m_nodes; }

    bool running() const { return m_running; }
    void start(const GameInput& game);
    void cancel();

    // --- the pure part ------------------------------------------------------
    // The whole of §2.3 and §2.4 over pre-computed evaluations: suppression,
    // the event budget of U7, the check order, at most two classes per event.
    static QVector<core::Finding> diagnose(const GameInput& game, const QVector<PlyEval>& evals);
    // §5.4: at most two fresh error cards become active per day; the rest wait
    // in a queue sorted by the accumulated dW of their class.
    static QVector<core::Card> cardsFor(const QVector<core::Finding>& findings, qint64 today);
    // The opening traps shipped with the app (§8.6 c), as EPDs of the position
    // *after* the losing move.
    static QStringList knownTrapEpds();

signals:
    void progress(int done, int total);
    void finished(const QVector<core::Finding>& findings);
    void failed(const QString& reason);

private slots:
    void onEngineResult(const schach::EngineResult& result);
    void onEngineFailed(const QString& reason);

private:
    enum Stage { StageBefore = 0, StageAfter, StageNullBefore, StageNullAfter };

    void requestNext();
    void finish();

    EngineProcess* m_engine;
    GameInput m_game;
    QVector<PlyEval> m_evals;
    QVector<int> m_learnerPlies;   // indices into m_game.moves
    qint64 m_nodes;
    int m_cursor;                  // index into m_learnerPlies
    int m_stage;
    bool m_running;
};

} // namespace schach

Q_DECLARE_METATYPE(QVector<schach::core::Finding>)

#endif // SCHACH_ANALYSER_H
