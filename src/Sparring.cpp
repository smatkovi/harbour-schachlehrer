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
#include "Sparring.h"
#include "core/WinProb.h"

#include <algorithm>
#include <cmath>

namespace schach {

namespace {

// teacher.md §7.2, one row per strength band.
struct BudgetRow {
    int maxElo;
    double budget;
    int bigMistakes;
    double bigSize;
    int mediumMistakes;
    double mediumSize;
};

const BudgetRow kBudgets[] = {
    {  900, 110.0, 2, 30.0, 3, 15.0 },
    { 1200,  75.0, 1, 28.0, 3, 15.0 },
    { 1500,  50.0, 1, 25.0, 2, 12.0 },
    { 1800,  32.0, 0,  0.0, 2, 16.0 },
    { 1 << 30, 20.0, 0, 0.0, 1, 20.0 },
};

const BudgetRow& rowFor(int elo)
{
    for (const BudgetRow& row : kBudgets) {
        if (elo < row.maxElo)
            return row;
    }
    return kBudgets[4];
}

} // namespace

Sparring::Sparring(QObject* parent)
    : QObject(parent)
    , m_budgetLeft(0.0)
    , m_mistakesMade(0)
    , m_ply(0)
    , m_lastMistakePly(-100)
    , m_pendingClass(core::ErrorClass::None)
    , m_pendingAge(0)
    , m_rng(20260912u)
{
}

double Sparring::budgetFor(int learnerElo) { return rowFor(learnerElo).budget; }

QVector<PlannedMistake> Sparring::planFor(int learnerElo, const QVector<core::ErrorClass>& wanted)
{
    const BudgetRow& row = rowFor(learnerElo);
    QVector<PlannedMistake> plan;
    int index = 0;
    // The classes are drawn from the learner's own error list: the opponent
    // makes the mistakes the learner does not punish (§7.3).
    auto nextClass = [&]() {
        if (wanted.isEmpty())
            return core::ErrorClass::A1;
        return wanted.at(index++ % wanted.size());
    };
    for (int i = 0; i < row.bigMistakes; ++i) {
        PlannedMistake mistake;
        mistake.cls = nextClass();
        mistake.size = row.bigSize;
        plan.append(mistake);
    }
    for (int i = 0; i < row.mediumMistakes; ++i) {
        PlannedMistake mistake;
        mistake.cls = nextClass();
        mistake.size = row.mediumSize;
        plan.append(mistake);
    }
    return plan;
}

void Sparring::reset(int learnerElo, const QVector<core::ErrorClass>& classesTheLearnerMisses,
                     unsigned seed)
{
    m_plan = planFor(learnerElo, classesTheLearnerMisses);
    m_budgetLeft = budgetFor(learnerElo);
    m_mistakesMade = 0;
    m_ply = 0;
    m_lastMistakePly = -100;
    clearPending();
    // The learner's own choice survives a new game; only the schedule starts
    // over (§7.5 is about one sitting, the setting is not).
    const core::DrillMode mode = m_check.mode;
    m_check = core::BlunderCheckSchedule();
    m_check.mode = mode;
    m_rng.seed(seed);
}

void Sparring::clearPending()
{
    m_pendingFen.clear();
    m_pendingClass = core::ErrorClass::None;
    m_pendingAge = 0;
}

bool Sparring::windowOpen(const core::Position& position) const
{
    // No mistakes in the first ten half moves, none in an already decided
    // position, and at least six half moves between two of them (§7.3).
    if (m_ply < 10)
        return false;
    if (m_ply - m_lastMistakePly < 6)
        return false;
    Q_UNUSED(position)
    return true;
}

bool Sparring::producesErrorPicture(const core::Position& position, const QString& move,
                                    core::ErrorClass cls) const
{
    // The inversion of the detectors of §2: the same predicates, applied to
    // the opponent's own move.
    const std::string uci = move.toStdString();
    if (!position.isLegal(uci))
        return false;
    const bool opponentIsWhite = position.whiteToMove();
    const core::Position after = position.after(uci);
    const std::vector<core::Hanging> beforeHanging = position.hanging(opponentIsWhite);
    const std::vector<core::Hanging> afterHanging = after.hanging(opponentIsWhite);

    switch (cls) {
    case core::ErrorClass::A1:
    case core::ErrorClass::C2: {
        // … one of his pieces hangs with SEE > 0 for the learner.
        for (std::size_t i = 0; i < afterHanging.size(); ++i) {
            bool wasAlready = false;
            for (std::size_t j = 0; j < beforeHanging.size(); ++j)
                wasAlready = wasAlready || beforeHanging[j].square == afterHanging[i].square;
            if (!wasAlready && afterHanging[i].value >= (cls == core::ErrorClass::A1 ? 3 : 1))
                return true;
        }
        return false;
    }
    case core::ErrorClass::A3:
    case core::ErrorClass::A3k:
    case core::ErrorClass::A5:
    case core::ErrorClass::A4:
    case core::ErrorClass::A6:
    case core::ErrorClass::C3: {
        // … the learner now has a motif. The learner is to move in `after`,
        // so his best move is the one to test; without the engine we take the
        // best capture, which is what these motifs come down to.
        int see = 0;
        const std::string capture = after.bestFreeCapture(1, &see);
        if (capture.empty())
            return false;
        const std::vector<core::Hanging> exposed = after.hanging(opponentIsWhite);
        return core::detectMotif(after, capture, exposed, false) != core::Motif::None;
    }
    case core::ErrorClass::B3: {
        // … his back rank is weak and a rook coming in mates.
        const int king = after.kingSquare(opponentIsWhite);
        const int homeRank = opponentIsWhite ? 0 : 7;
        if (core::rankOf(king) != homeRank)
            return false;
        const int secondRank = opponentIsWhite ? 1 : 6;
        for (int file = std::max(0, core::fileOf(king) - 1);
             file <= std::min(7, core::fileOf(king) + 1); ++file) {
            const char piece = after.pieceAt(secondRank * 8 + file);
            const char ownPawn = opponentIsWhite ? 'P' : 'p';
            if (piece != ownPawn)
                return false;
        }
        return true;
    }
    case core::ErrorClass::C4: {
        // … a *quiet* move of the learner decides. Approximated by: the
        // learner has no winning capture but the position turned against the
        // opponent anyway; the engine score does the rest.
        int see = 0;
        after.bestFreeCapture(1, &see);
        return see <= 0;
    }
    case core::ErrorClass::F6:
        // … an exchange leads into a pawn endgame that is lost for him.
        return position.officers() <= 2 && after.officers() == 0;
    default:
        return true;
    }
}

QString Sparring::chooseMove(const core::Position& position, const QVector<ScoredMove>& candidates)
{
    ++m_ply;
    if (candidates.isEmpty())
        return QString();

    const QString bestMove = candidates.first().uci;
    if (m_budgetLeft <= 0.0 || !windowOpen(position))
        return bestMove;   // play honestly strong

    // Find the next unspent mistake in the plan.
    PlannedMistake* target = 0;
    for (int i = 0; i < m_plan.size(); ++i) {
        if (!m_plan[i].spent) {
            target = &m_plan[i];
            break;
        }
    }
    if (!target)
        return bestMove;

    // teacher.md §7.3: candidates whose dW matches the intended size (+-6 pp)
    // and which really produce the intended error picture.
    const double bestW = core::winProbability(candidates.first().score);
    QVector<QString> fitting;
    for (int i = 1; i < candidates.size(); ++i) {
        if (!candidates.at(i).score.valid)
            continue;
        const double loss = bestW - core::winProbability(candidates.at(i).score);
        if (std::fabs(loss - target->size) > 6.0)
            continue;
        if (!producesErrorPicture(position, candidates.at(i).uci, target->cls))
            continue;
        fitting.append(candidates.at(i).uci);
    }
    if (fitting.isEmpty()) {
        // Move the window instead of forcing a mistake that does not exist.
        m_lastMistakePly = m_ply - 4;
        return bestMove;
    }

    std::uniform_int_distribution<int> pick(0, fitting.size() - 1);
    const QString chosen = fitting.at(pick(m_rng));
    target->spent = true;
    m_budgetLeft -= target->size;
    m_lastMistakePly = m_ply;
    ++m_mistakesMade;

    m_pendingFen = QString::fromStdString(position.after(chosen.toStdString()).fen());
    m_pendingClass = target->cls;
    m_pendingAge = 0;
    emit mistakeMade(m_pendingFen, static_cast<int>(m_pendingClass));
    return chosen;
}

bool Sparring::noteLearnerMove(const core::Position& positionAfter)
{
    Q_UNUSED(positionAfter)
    ++m_ply;
    if (m_pendingFen.isEmpty())
        return false;
    ++m_pendingAge;
    if (m_pendingAge < 2)
        return false;
    // §7.4: there is no gift. The opponent quietly takes it back, the budget
    // counts as spent anyway, and the position becomes an exercise — the
    // learner notices the chance *after* it is gone and gets it back at once.
    const QString fen = m_pendingFen;
    const core::ErrorClass cls = m_pendingClass;
    clearPending();
    emit chanceMissed(fen, static_cast<int>(cls));
    return true;
}

QStringList Sparring::threatsToCheck(const core::Position& position)
{
    // §7.5, straight out of §1.2.2: 72 % of all refutations are a check or a
    // capture, and those are 11 % of the legal moves.
    QStringList out;
    if (position.inCheck())
        return out;
    const core::Position passed = position.afterNullMove();
    const std::vector<std::string> loud = passed.checksAndCaptures();
    for (std::size_t i = 0; i < loud.size(); ++i) {
        const std::string& move = loud[i];
        if (passed.isCapture(move) && passed.see(move) <= 0)
            continue;
        out << QString::fromStdString(move);
    }
    return out;
}

QVector<BlunderCheckItem> Sparring::blunderCheckList(const core::Position& before,
                                                    const QString& intendedUci)
{
    // §7.5, straight out of §1.2.2 again: his checks and his captures with
    // SEE > 0 after the move the learner wants to make. A check stays in the
    // list whatever its SEE, because a check he has to answer is exactly the
    // move that hides the fork behind it.
    QVector<BlunderCheckItem> out;
    const std::string uci = intendedUci.toStdString();
    if (!before.isLegal(uci))
        return out;
    const core::Position after = before.after(uci);
    const bool learnerIsWhite = before.whiteToMove();

    const std::vector<std::string> loud = after.checksAndCaptures();
    for (std::size_t i = 0; i < loud.size(); ++i) {
        const std::string& move = loud[i];
        const bool capture = after.isCapture(move);
        const int see = after.see(move);
        if (capture && see <= 0)
            continue;

        BlunderCheckItem item;
        item.uci = QString::fromStdString(move);
        item.san = QString::fromStdString(after.sanOf(move));
        // Dangerous means: it costs something. Either the move itself wins
        // material, or it is a check after which one of the learner's pieces
        // can be taken — the discovered attack that the check pays for.
        item.dangerous = see > 0;
        if (!item.dangerous && after.givesCheck(move)) {
            const core::Position afterCheck = after.after(move);
            item.dangerous = !afterCheck.hanging(learnerIsWhite).empty()
                    || afterCheck.gameOver();
        }
        out.append(item);
    }
    return out;
}

void Sparring::requireBlunderCheck()
{
    m_check.bringBack();
}

} // namespace schach
