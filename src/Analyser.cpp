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
#include "Analyser.h"
#include "core/Skill.h"
#include "core/WinProb.h"

#include <algorithm>
#include <deque>

namespace schach {

namespace {

using core::Position;

bool isOwn(char piece, bool white)
{
    if (piece == ' ')
        return false;
    return white ? (piece >= 'A' && piece <= 'Z') : (piece >= 'a' && piece <= 'z');
}

// The score the engine reports is always from the side to move's point of
// view. For the position *after* the learner's move that is the opponent, so
// everything the taxonomy sees has to be negated once.
core::Score negated(const core::Score& score)
{
    if (!score.valid)
        return score;
    core::Score out = score;
    if (score.isMate)
        out.mateIn = -score.mateIn;
    else
        out.cp = -score.cp;
    return out;
}

double medianOf(QVector<int> values)
{
    QVector<int> kept;
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i) >= 0)
            kept.append(values.at(i));
    }
    if (kept.isEmpty())
        return -1.0;
    std::sort(kept.begin(), kept.end());
    return kept.at(kept.size() / 2);
}

// The starter list of beginner traps from teacher.md §8.6 (c). They are stored
// as SAN lines rather than as EPD constants so that they can be checked by
// replaying them: a mistyped FEN would silently never match.
struct TrapLine {
    const char* name;
    const char* moves;   // SAN, space separated, ending on the losing move
};

const TrapLine kTraps[] = {
    { "Narrenmatt", "f3 e5 g4" },
    { "Schäfermatt", "e4 e5 Bc4 Nc6 Qh5 Nf6" },
    { "Seekadettenmatt (Légal)", "e4 e5 Nf3 d6 Bc4 Bg4 Nc3 g6" },
    { "Blackburne-Falle", "e4 e5 Nf3 Nc6 Bc4 Nd4 Nxe5" },
    { "Fegatello", "e4 e5 Nf3 Nc6 Bc4 Nf6 Ng5 d5 exd5 Nxd5" },
    { "Elefantenfalle", "d4 d5 c4 e6 Nc3 Nf6 Bg5 Nbd7 cxd5 exd5 Nxd5" },
    { "Lasker-Falle", "d4 d5 c4 e5 dxe5 d4 e3" },
};

// How many tempi the opponent gains by attacking the learner's queen in the
// next four half moves (teacher.md D3).
int queenTempi(const Position& start, const QStringList& moves, int fromPly, bool learnerWhite)
{
    Position position = start;
    int tempi = 0;
    for (int i = fromPly; i < moves.size() && i < fromPly + 4; ++i) {
        const std::string move = moves.at(i).toStdString();
        if (!position.isLegal(move))
            break;
        const bool opponentToMove = position.whiteToMove() != learnerWhite;
        const Position next = position.after(move);
        if (opponentToMove) {
            // Find the learner's queen and see whether the move attacks it.
            for (int square = 0; square < 64; ++square) {
                const char piece = next.pieceAt(square);
                if ((piece == 'Q' || piece == 'q') && isOwn(piece, learnerWhite)) {
                    const std::vector<int> attackers = next.attackersOf(square, !learnerWhite);
                    const int to = core::squareFromName(move.substr(2, 2));
                    if (std::find(attackers.begin(), attackers.end(), to) != attackers.end())
                        ++tempi;
                }
            }
        }
        position = next;
    }
    return tempi;
}

// teacher.md G4: after a pawn move a square in the learner's own half can no
// longer be covered by any of his pawns, and within six half moves an enemy
// knight settles there.
bool weakSquareTaken(const Position& before, const Position& after, const QStringList& moves,
                     int fromPly, bool learnerWhite)
{
    std::vector<int> lost;
    for (int square = 0; square < 64; ++square) {
        const int rank = core::rankOf(square);
        const bool ownHalf = learnerWhite ? rank <= 3 : rank >= 4;
        if (!ownHalf)
            continue;
        // "can still be covered by a pawn" is approximated by "a pawn of ours
        // stands on a neighbouring file at most two ranks behind it".
        auto coverable = [&](const Position& position) {
            for (int other = 0; other < 64; ++other) {
                const char piece = position.pieceAt(other);
                if (!(piece == (learnerWhite ? 'P' : 'p')))
                    continue;
                if (std::abs(core::fileOf(other) - core::fileOf(square)) != 1)
                    continue;
                const int ahead = learnerWhite ? core::rankOf(square) - core::rankOf(other)
                                               : core::rankOf(other) - core::rankOf(square);
                if (ahead >= 1)
                    return true;
            }
            return false;
        };
        if (coverable(before) && !coverable(after))
            lost.push_back(square);
    }
    if (lost.empty())
        return false;

    Position position = after;
    for (int i = fromPly + 1; i < moves.size() && i < fromPly + 7; ++i) {
        const std::string move = moves.at(i).toStdString();
        if (!position.isLegal(move))
            break;
        position = position.after(move);
        for (std::size_t j = 0; j < lost.size(); ++j) {
            const char piece = position.pieceAt(lost[j]);
            if (piece == (learnerWhite ? 'n' : 'N'))
                return true;
        }
    }
    return false;
}

} // namespace

Analyser::Analyser(EngineProcess* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
    , m_nodes(1500000)
    , m_cursor(0)
    , m_stage(StageBefore)
    , m_running(false)
{
    qRegisterMetaType<QVector<schach::core::Finding> >("QVector<schach::core::Finding>");
    if (m_engine) {
        connect(m_engine, SIGNAL(result(schach::EngineResult)),
                this, SLOT(onEngineResult(schach::EngineResult)));
        connect(m_engine, SIGNAL(failed(QString)), this, SLOT(onEngineFailed(QString)));
    }
}

QStringList Analyser::knownTrapEpds()
{
    QStringList out;
    for (std::size_t i = 0; i < sizeof(kTraps) / sizeof(kTraps[0]); ++i) {
        Position position;
        const QStringList moves = QString::fromLatin1(kTraps[i].moves).split(QLatin1Char(' '));
        bool ok = true;
        for (int j = 0; j < moves.size(); ++j) {
            if (!position.playSan(moves.at(j).toStdString())) {
                ok = false;   // a mistyped line is dropped rather than silently wrong
                break;
            }
        }
        if (ok)
            out << QString::fromStdString(position.epd());
    }
    return out;
}

void Analyser::start(const GameInput& game)
{
    cancel();
    m_game = game;
    m_evals.clear();
    m_learnerPlies.clear();

    Position position(game.initialFen.isEmpty()
                              ? std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
                              : game.initialFen.toStdString());
    for (int i = 0; i < game.moves.size(); ++i) {
        if (position.whiteToMove() == game.learnerIsWhite)
            m_learnerPlies.append(i);
        if (!position.play(game.moves.at(i).toStdString()))
            break;
    }
    m_evals.resize(game.moves.size());

    if (!m_engine || !m_engine->available()) {
        emit failed(tr("Ohne Engine kann ich die Partie nicht durchsehen. "
                       "Brett, Wiederholungen und Regeln funktionieren trotzdem."));
        return;
    }
    m_running = true;
    m_cursor = 0;
    m_stage = StageBefore;
    requestNext();
}

void Analyser::cancel()
{
    if (!m_running)
        return;
    m_running = false;
    if (m_engine)
        m_engine->cancelAll();
}

void Analyser::requestNext()
{
    if (!m_running)
        return;
    if (m_cursor >= m_learnerPlies.size()) {
        finish();
        return;
    }

    const int ply = m_learnerPlies.at(m_cursor);
    QStringList prefix;
    for (int i = 0; i < ply; ++i)
        prefix << m_game.moves.at(i);

    if (m_stage == StageBefore) {
        // MultiPV 3: the best move, the runner-up for C5, and the third for the
        // criticality span of I1/I2.
        m_engine->analyseNodes(m_game.initialFen, prefix, m_nodes, 3, ply);
    } else {
        QStringList withMove = prefix;
        withMove << m_game.moves.at(ply);
        m_engine->analyseNodes(m_game.initialFen, withMove, m_nodes, 1, ply);
    }
}

void Analyser::onEngineResult(const EngineResult& result)
{
    if (!m_running || m_cursor >= m_learnerPlies.size())
        return;
    const int ply = m_learnerPlies.at(m_cursor);
    if (result.tag.toInt() != ply)
        return;

    PlyEval& eval = m_evals[ply];
    if (m_stage == StageBefore) {
        eval.valid = true;
        eval.before = result.score();
        eval.best = result.bestMove;
        if (result.lines.size() > 1)
            eval.second = result.lines.at(1).score;
        if (result.lines.size() > 2)
            eval.third = result.lines.at(2).score;
        if (!result.lines.isEmpty()) {
            const std::vector<std::string>& pv = result.lines.first().pv;
            for (std::size_t i = 0; i < pv.size(); ++i)
                eval.bestPv << QString::fromStdString(pv[i]);
        }
        // C5: is `best` the only move that holds? With MultiPV 3 that is
        // decidable: the runner-up has to be at least 8 pp worse.
        if (eval.second.valid) {
            const double gap = core::winProbability(eval.before)
                    - core::winProbability(eval.second);
            eval.bestIsOnlyMove = gap >= 8.0;
        }
        m_stage = StageAfter;
    } else {
        eval.after = negated(result.score());
        eval.opponentReply = result.bestMove;
        // Without a third call the threat measure stays neutral; buildFeatures
        // then simply reports no threat, which is the conservative answer.
        eval.nullBefore = eval.before;
        eval.nullAfter = eval.after;
        m_stage = StageBefore;
        ++m_cursor;
        emit progress(m_cursor, m_learnerPlies.size());
    }
    requestNext();
}

void Analyser::onEngineFailed(const QString& reason)
{
    if (!m_running)
        return;
    m_running = false;
    emit failed(reason);
}

void Analyser::finish()
{
    m_running = false;
    emit finished(diagnose(m_game, m_evals));
}

QVector<core::Finding> Analyser::diagnose(const GameInput& game, const QVector<PlyEval>& evals)
{
    QVector<core::Finding> out;
    const std::string startFen = game.initialFen.isEmpty()
            ? std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
            : game.initialFen.toStdString();

    const double medianTime = medianOf(game.moveTimesMs);
    const QStringList trapEpds = knownTrapEpds();

    // --- pass 1: features and classification per half move ------------------
    struct Event {
        core::MoveFeatures features;
        QVector<core::Finding> findings;
        int ply;
    };
    QVector<Event> events;
    // A deque, not a vector: MoveFeatures keeps a pointer into this, and a
    // reallocation would leave every earlier event pointing at freed memory.
    std::deque<core::GameContext> contexts;
    events.reserve(evals.size());

    Position position(startFen);
    bool castlingRightSeenLate = false;
    int wingPawnMoves = 0;
    int developedMinors = 0;
    int opponentDevelopedMinors = 0;
    QVector<int> pieceMoveCount(64, 0);

    for (int ply = 0; ply < game.moves.size(); ++ply) {
        const std::string move = game.moves.at(ply).toStdString();
        if (!position.isLegal(move))
            break;
        const bool learnersTurn = position.whiteToMove() == game.learnerIsWhite;
        const int from = core::squareFromName(move.substr(0, 2));
        const int to = core::squareFromName(move.substr(2, 2));
        const char piece = position.pieceAt(from);

        // Opening bookkeeping, before the move is played.
        if (learnersTurn && ply < 20) {
            if (position.board().castlingRights().has(
                        game.learnerIsWhite ? chess::Color::WHITE : chess::Color::BLACK))
                castlingRightSeenLate = true;
        }
        if (learnersTurn && ply < 16 && (piece == 'P' || piece == 'p')) {
            const int file = core::fileOf(from);
            if (file <= 1 || file >= 6)
                ++wingPawnMoves;
        }

        if (learnersTurn && ply < evals.size() && evals.at(ply).valid) {
            const PlyEval& eval = evals.at(ply);

            core::GameContext context;
            context.learnerIsWhite = game.learnerIsWhite;
            context.learnerElo = game.learnerElo;
            context.ply = ply;
            context.moveTimeMs = ply < game.moveTimesMs.size() ? game.moveTimesMs.at(ply) : -1;
            context.medianMoveTimeMs = medianTime;
            context.onlyLegalMove = position.legalMoves().size() == 1;
            context.repetition = position.board().isRepetition(1);
            context.castlingRightUntilPly20 = castlingRightSeenLate;
            context.kingStillOnStartSquare =
                    position.pieceAt(core::squareFromName(game.learnerIsWhite ? "e1" : "e8"))
                    == (game.learnerIsWhite ? 'K' : 'k');
            context.samePieceMovesFirst20 = ply < 20 ? pieceMoveCount[from] + 1 : 0;
            context.wingPawnMovesFirst16 = wingPawnMoves;
            context.undevelopedMinors = 4 - developedMinors;
            context.developmentDeficitAtPly20 = opponentDevelopedMinors - developedMinors;
            const Position afterMove = position.after(move);
            context.knownTrapPosition = trapEpds.contains(QString::fromStdString(afterMove.epd()));
            if (piece == 'Q' || piece == 'q')
                context.queenAttackTempiNext4 = queenTempi(afterMove, game.moves, ply + 1,
                                                           game.learnerIsWhite);
            if (piece == 'P' || piece == 'p')
                context.weakSquareOccupiedByKnight =
                        weakSquareTaken(position, afterMove, game.moves, ply, game.learnerIsWhite);
            if (game.baseTimeMs > 0.0 && context.moveTimeMs >= 0) {
                // A rough remaining-clock estimate; a real PGN carries %clk and
                // the caller fills it in instead.
                double used = 0.0;
                for (int i = ply % 2; i <= ply && i < game.moveTimesMs.size(); i += 2)
                    used += std::max(0, game.moveTimesMs.at(i));
                context.remainingFraction = 1.0 - used / game.baseTimeMs;
            }

            core::EngineView view;
            view.before = eval.before;
            view.after = eval.after;
            view.nullBefore = eval.nullBefore;
            view.nullAfter = eval.nullAfter;
            view.second = eval.second;
            view.third = eval.third;
            view.best = eval.best.toStdString();
            view.opponentReply = eval.opponentReply.toStdString();
            view.bestIsOnlyMove = eval.bestIsOnlyMove;
            view.refutationConfirmed = eval.refutationConfirmed;
            view.tbBefore = eval.tbBefore;
            view.tbAfter = eval.tbAfter;
            for (int i = 0; i < eval.bestPv.size(); ++i)
                view.bestPv.push_back(eval.bestPv.at(i).toStdString());
            // E5: the played move appears later in the main line of `best`.
            for (std::size_t i = 0; i < view.bestPv.size(); ++i) {
                if (view.bestPv[i] == move) {
                    view.playedIndexInBestPv = static_cast<int>(i);
                    break;
                }
            }

            contexts.push_back(context);
            Event event;
            event.ply = ply;
            event.features = core::buildFeatures(position, move, view, contexts.back());
            events.append(event);
        }

        // Development bookkeeping after the move.
        const char type = (piece >= 'a' && piece <= 'z') ? static_cast<char>(piece - 'a' + 'A') : piece;
        if ((type == 'N' || type == 'B') && core::rankOf(from) == (learnersTurn
                    ? (game.learnerIsWhite ? 0 : 7)
                    : (game.learnerIsWhite ? 7 : 0))) {
            if (learnersTurn)
                ++developedMinors;
            else
                ++opponentDevelopedMinors;
        }
        if (learnersTurn && type == 'K' && std::abs(core::fileOf(to) - core::fileOf(from)) > 1)
            ++developedMinors;   // castling counts as one step of development
        const int movedBefore = pieceMoveCount[from];
        pieceMoveCount[from] = 0;
        pieceMoveCount[to] = movedBefore + 1;
        position.play(move);
    }

    // --- U7: at most as many events as the strength band allows -------------
    const int maxEvents = core::maxCardsPerGame(game.learnerElo);
    std::vector<double> deltas;
    QVector<int> candidates;
    for (int i = 0; i < events.size(); ++i) {
        if (core::suppressionOf(events[i].features) != core::Suppression::None)
            continue;
        if (events[i].features.dW < core::learningThreshold(game.learnerElo))
            continue;
        candidates.append(i);
        deltas.push_back(events[i].features.dW);
    }
    const std::vector<std::size_t> keep = core::selectEvents(deltas, static_cast<std::size_t>(maxEvents));
    std::vector<bool> kept(events.size(), false);
    for (std::size_t i = 0; i < keep.size(); ++i)
        kept[candidates.at(static_cast<int>(keep[i]))] = true;

    int lastQuarterEvents = 0;
    const int quarterStart = game.moves.size() * 3 / 4;
    for (int i = 0; i < events.size(); ++i) {
        const bool over = events[i].features.dW >= core::learningThreshold(game.learnerElo);
        // Events over the threshold that U7 dropped still count towards a_d,
        // but they produce no card: the diagnosis must not be bent by the
        // workload filter (§3.2 b).
        const std::vector<core::Finding> raw = core::classify(events[i].features, game.learnerElo);
        for (std::size_t j = 0; j < raw.size(); ++j) {
            core::Finding finding = raw[j];
            finding.ply = events[i].ply;
            if (over && !kept[static_cast<std::size_t>(i)])
                finding.makesCard = false;
            out.append(finding);
        }
        if (over && events[i].ply >= quarterStart)
            ++lastQuarterEvents;
    }

    // I3 is a verdict about the whole game, so it can only be made afterwards.
    int totalEvents = 0;
    for (int i = 0; i < out.size(); ++i) {
        if (out.at(i).dW >= core::learningThreshold(game.learnerElo))
            ++totalEvents;
    }
    if (totalEvents >= 3 && lastQuarterEvents * 10 >= totalEvents * 6) {
        core::Finding finding;
        finding.cls = core::ErrorClass::I3;
        finding.dimension = core::dimensionOf(core::ErrorClass::I3);
        finding.sentence = core::errorTemplate(core::ErrorClass::I3);
        finding.ply = game.moves.size() - 1;
        finding.makesCard = false;
        out.append(finding);
    }
    return out;
}

QVector<core::Card> Analyser::cardsFor(const QVector<core::Finding>& findings, qint64 today)
{
    QVector<core::Card> cards;
    for (int i = 0; i < findings.size(); ++i) {
        const core::Finding& finding = findings.at(i);
        if (!finding.makesCard)
            continue;
        const std::string id = core::cardIdFor(finding.cls, finding.motif);
        bool merged = false;
        for (int j = 0; j < cards.size(); ++j) {
            if (cards[j].id == id) {
                core::applyFinding(cards[j], finding, today);
                merged = true;
                break;
            }
        }
        if (!merged)
            cards.append(core::cardFromFinding(finding, today));
    }
    // §5.4: at most two of them go active today; the rest wait, sorted by the
    // accumulated dW of their class, so one bad game cannot flood the plan.
    std::stable_sort(cards.begin(), cards.end(), [](const core::Card& a, const core::Card& b) {
        return a.queuedMass > b.queuedMass;
    });
    for (int i = core::kMaxNewErrorCardsPerDay; i < cards.size(); ++i)
        cards[i].srs.dueDay = today + 1 + (i - core::kMaxNewErrorCardsPerDay) / 2;
    return cards;
}

} // namespace schach
