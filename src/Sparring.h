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
#ifndef SCHACH_SPARRING_H
#define SCHACH_SPARRING_H

// teacher.md §7 — the training opponent with a mistake budget instead of an
// Elo cap.
//
// Capping Stockfish produces an opponent who is *evenly* weaker: he spreads
// small inaccuracies over the whole game and plays inhumanly precisely in
// between. Such an opponent is useless for training, because he does not make
// the mistakes the learner has to learn to punish.
//
// So: the opponent plays properly, gets a budget in dW percentage points that
// he *must* spend, and every spend is a mistake of a deliberately chosen type
// — the type the learner currently fails to punish. That is the inversion of
// the usual training idea and the core of §7.3.
//
// And there is no gift (§7.4): if the learner does not punish within two of
// his own moves, the opponent quietly takes it back. The position is kept and
// becomes a card of the class that was missed.

#include "core/Position.h"
#include "core/Taxonomy.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <random>

namespace schach {

// One candidate move with its evaluation, as MultiPV delivers them.
struct ScoredMove {
    QString uci;
    core::Score score;    // from the mover's point of view
};

// One planned mistake.
struct PlannedMistake {
    core::ErrorClass cls = core::ErrorClass::A1;
    double size = 25.0;   // in dW percentage points
    bool spent = false;
};

class Sparring : public QObject
{
    Q_OBJECT

public:
    explicit Sparring(QObject* parent = 0);

    // teacher.md §7.2. The budget falls as the learner gets stronger.
    static double budgetFor(int learnerElo);
    static QVector<PlannedMistake> planFor(int learnerElo, const QVector<core::ErrorClass>& wanted);

    void reset(int learnerElo, const QVector<core::ErrorClass>& classesTheLearnerMisses,
               unsigned seed = 20260912u);
    double budgetLeft() const { return m_budgetLeft; }
    int mistakesMade() const { return m_mistakesMade; }
    // The position in which the opponent last made a deliberate mistake, and
    // the class it was — this is what becomes a card if it goes unpunished.
    QString pendingFen() const { return m_pendingFen; }
    core::ErrorClass pendingClass() const { return m_pendingClass; }
    bool hasPending() const { return !m_pendingFen.isEmpty(); }

    // Pick the opponent's move. `candidates` must be sorted best first.
    // Returns the chosen UCI move, or an empty string when there is none.
    QString chooseMove(const core::Position& position, const QVector<ScoredMove>& candidates);

    // Called after each of the learner's moves. Returns true when the missed
    // chance has just expired — the caller then makes a card of it (§7.4).
    bool noteLearnerMove(const core::Position& positionAfter);
    void clearPending();

    // §7.5 The blunder-check drill: all of the opponent's checks and captures
    // with SEE > 0, hidden, for the learner to mark. The scaffold fades as it
    // is answered correctly, and comes back on the next A1, B1 or C2 event.
    static QStringList threatsToCheck(const core::Position& position);
    bool blunderCheckDue() const;
    void noteBlunderCheck(bool correct);
    void requireBlunderCheck();       // an A1/B1/C2 event happened
    int blunderCheckEvery() const { return m_checkEvery; }
    void noteMovePlayed();

signals:
    void mistakeMade(const QString& fen, int errorClass);
    void chanceMissed(const QString& fen, int errorClass);

private:
    bool windowOpen(const core::Position& position) const;
    bool producesErrorPicture(const core::Position& position, const QString& move,
                              core::ErrorClass cls) const;

    QVector<PlannedMistake> m_plan;
    double m_budgetLeft;
    int m_mistakesMade;
    int m_ply;
    int m_lastMistakePly;
    QString m_pendingFen;
    core::ErrorClass m_pendingClass;
    int m_pendingAge;          // in the learner's own moves
    int m_checkEvery;          // 3 -> 5 -> 8 -> off
    int m_checkStreak;
    int m_movesSinceCheck;
    std::mt19937 m_rng;
};

} // namespace schach

#endif // SCHACH_SPARRING_H
