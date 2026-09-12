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
#include "TeacherEngine.h"

#include <QtGlobal>

#include <QDateTime>

namespace schach {

namespace {

// Sparring: 50–250 ms per move (docs/design.md §5). Enough for a decent move
// on a phone, short enough that the board never feels stuck.
const int kSparringMovetimeMs = 150;
const int kOpponentDelayMs = 350;

QString pieceKey(char piece)
{
    if (piece == ' ')
        return QString();
    const bool white = piece >= 'A' && piece <= 'Z';
    QString key = white ? QStringLiteral("w") : QStringLiteral("b");
    key += QChar::fromLatin1(white ? piece : static_cast<char>(piece - 'a' + 'A'));
    return key;
}

} // namespace

TeacherEngine::TeacherEngine(QObject* parent)
    : QObject(parent)
    , m_engine(new EngineProcess(this))
    , m_database(new Database(this))
    , m_analyser(new Analyser(m_engine, this))
    , m_sparring(new ::schach::Sparring(this))
    , m_opponentTimer(new QTimer(this))
    , m_placement(0)
    , m_reviewIndex(-1)
    , m_liveSaved(false)
    , m_sessionIndex(0)
    , m_analysisDone(0)
    , m_analysisTotal(0)
    , m_hintLevel(0)
    , m_learnerIsWhite(true)
    , m_flipped(false)
    , m_selected(-1)
    , m_mode(Idle)
    , m_currentGameId(-1)
{
    m_opponentTimer->setSingleShot(true);
    connect(m_opponentTimer, SIGNAL(timeout()), this, SLOT(onOpponentTurn()));
    connect(m_engine, SIGNAL(result(schach::EngineResult)),
            this, SLOT(onEngineResult(schach::EngineResult)));
    connect(m_engine, SIGNAL(failed(QString)), this, SLOT(onEngineFailed(QString)));
    connect(m_engine, SIGNAL(readyChanged()), this, SIGNAL(engineChanged()));
    connect(m_engine, SIGNAL(busyChanged()), this, SIGNAL(engineChanged()));
    connect(m_analyser, SIGNAL(finished(QVector<schach::core::Finding>)),
            this, SLOT(onAnalysisFinished(QVector<schach::core::Finding>)));
    connect(m_analyser, SIGNAL(progress(int, int)), this, SLOT(onAnalysisProgress(int, int)));
    connect(m_analyser, SIGNAL(failed(QString)), this, SLOT(onEngineFailed(QString)));
    connect(m_sparring, SIGNAL(chanceMissed(QString, int)), this, SLOT(onChanceMissed(QString, int)));

    m_prompt = tr("Willkommen. Fang mit dem Aufwärmen an oder spiel gleich eine Partie.");
}

TeacherEngine::~TeacherEngine()
{
    delete m_placement;
}

void TeacherEngine::setPaths(const QString& enginePath, const QString& syzygyPath,
                             const QString& evalFile, const QString& databasePath)
{
    m_engine->setEnginePath(enginePath);
    m_engine->setSyzygyPath(syzygyPath);
    m_engine->setEvalFile(evalFile);
    m_engine->setHashMb(16);
    m_engine->setThreads(1);
    m_database->open(databasePath);

    double theta = 1000.0;
    QVector<double> perDimension;
    if (m_database->latestSkill(theta, perDimension)) {
        m_skill.theta = theta;
        for (int i = 0; i < perDimension.size() && i < core::kDimensionCount; ++i)
            m_skill.delta[i] = perDimension.at(i) - theta;
    }
    // A missing engine is a normal state: the board, the repetitions and the
    // rules work without it, only sparring and analysis do not (§5).
    if (!m_engine->start())
        m_engineMessage = m_engine->lastError();
    emit engineChanged();
    emit progressChanged();
}

qint64 TeacherEngine::today() const
{
    return QDateTime::currentMSecsSinceEpoch() / (1000LL * 60 * 60 * 24);
}

int TeacherEngine::learnerElo() const { return static_cast<int>(m_skill.theta + 0.5); }

// --- board -------------------------------------------------------------------

QString TeacherEngine::fen() const { return QString::fromStdString(m_position.fen()); }
bool TeacherEngine::whiteToMove() const { return m_position.whiteToMove(); }

QVariantList TeacherEngine::squares() const
{
    QVariantList out;
    for (int square = 0; square < 64; ++square) {
        QVariantMap entry;
        entry[QStringLiteral("index")] = square;
        entry[QStringLiteral("file")] = core::fileOf(square);
        entry[QStringLiteral("rank")] = core::rankOf(square);
        entry[QStringLiteral("light")] = ((core::fileOf(square) + core::rankOf(square)) & 1) != 0;
        entry[QStringLiteral("piece")] = pieceKey(m_position.pieceAt(square));
        entry[QStringLiteral("name")] = QString::fromStdString(core::squareName(square));
        out.append(entry);
    }
    return out;
}

QVariantList TeacherEngine::legalTargets() const
{
    QVariantList out;
    if (m_selected < 0)
        return out;
    const std::vector<int> targets = m_position.legalTargets(m_selected);
    for (std::size_t i = 0; i < targets.size(); ++i)
        out.append(targets[i]);
    return out;
}

void TeacherEngine::setSelectedSquare(int square)
{
    if (m_selected == square)
        return;
    // Only the learner's own pieces can be picked up, and only on his turn.
    if (square >= 0) {
        const char piece = m_position.pieceAt(square);
        const bool white = piece >= 'A' && piece <= 'Z';
        if (piece == ' ' || white != m_position.whiteToMove())
            square = -1;
    }
    m_selected = square;
    emit selectionChanged();
}

void TeacherEngine::setFlipped(bool flipped)
{
    if (m_flipped == flipped)
        return;
    m_flipped = flipped;
    emit boardChanged();
}

QVariantList TeacherEngine::moveList() const
{
    QVariantList out;
    const std::vector<std::string>& san = m_position.sanHistory();
    for (std::size_t i = 0; i < san.size(); ++i) {
        QVariantMap entry;
        entry[QStringLiteral("ply")] = static_cast<int>(i);
        entry[QStringLiteral("number")] = static_cast<int>(i / 2) + 1;
        entry[QStringLiteral("white")] = (i % 2) == 0;
        entry[QStringLiteral("san")] = QString::fromStdString(san[i]);
        out.append(entry);
    }
    return out;
}

QString TeacherEngine::lastMove() const
{
    // While looking back, the board marks the move that was right, on the
    // position it was asked in.
    if (m_reviewIndex >= 0 && m_reviewIndex < m_answered.size())
        return m_answered.at(m_reviewIndex).solution;

    return QString::fromStdString(m_position.lastMove());
}

QString TeacherEngine::gameResult() const
{
    switch (m_position.outcome()) {
    case core::Outcome::Ongoing:
        return QString();
    case core::Outcome::Draw:
        switch (m_position.endReason()) {
        case core::EndReason::Stalemate: return tr("Patt — unentschieden.");
        case core::EndReason::InsufficientMaterial: return tr("Es ist zu wenig Material übrig: remis.");
        case core::EndReason::FiftyMoves: return tr("Fünfzig Züge ohne Schlag oder Bauernzug: remis.");
        case core::EndReason::Repetition: return tr("Dieselbe Stellung zum dritten Mal: remis.");
        default: return tr("Unentschieden.");
        }
    case core::Outcome::WhiteWins:
        return m_learnerIsWhite ? tr("Matt. Gewonnen.") : tr("Matt. Diese geht an ihn.");
    case core::Outcome::BlackWins:
        return m_learnerIsWhite ? tr("Matt. Diese geht an ihn.") : tr("Matt. Gewonnen.");
    }
    return QString();
}

// --- mode and feedback -------------------------------------------------------

bool TeacherEngine::engineReady() const { return m_engine->ready(); }
bool TeacherEngine::thinking() const { return m_engine->busy() || m_analyser->running(); }

void TeacherEngine::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    emit modeChanged();
}

void TeacherEngine::setPrompt(const QString& prompt)
{
    if (m_prompt == prompt)
        return;
    m_prompt = prompt;
    emit taskChanged();
}

void TeacherEngine::setFeedback(const QString& text, const QString& key, const core::Score* score)
{
    // teacher.md §6.6: never a number alone, never a bare "wrong". `cp` and
    // `wp` are carried for the log, not for the screen.
    QVariantMap feedback;
    feedback[QStringLiteral("text")] = text;
    feedback[QStringLiteral("key")] = key;
    if (score && score->valid) {
        if (score->isMate)
            feedback[QStringLiteral("mate")] = score->mateIn;
        else
            feedback[QStringLiteral("cp")] = score->cp;
        feedback[QStringLiteral("wp")] = core::winProbability(*score);
    }
    m_feedback = feedback;
    emit feedbackChanged();
}

void TeacherEngine::clearFeedback()
{
    if (m_feedback.isEmpty())
        return;
    m_feedback.clear();
    emit feedbackChanged();
}

// --- progress ----------------------------------------------------------------

QVariantList TeacherEngine::skills() const
{
    const std::array<double, core::kDimensionCount> a = core::shares(m_skill);
    QVariantList out;
    for (int i = 0; i < core::kDimensionCount; ++i) {
        const core::Dimension dimension = static_cast<core::Dimension>(i);
        QVariantMap entry;
        entry[QStringLiteral("key")] = QString::fromLatin1(core::dimensionKey(dimension));
        entry[QStringLiteral("name")] = QString::fromLatin1(core::dimensionName(dimension));
        entry[QStringLiteral("question")] = QString::fromLatin1(core::dimensionQuestion(dimension));
        // theta_d picks the difficulty of the next task, a_d picks what is
        // trained. That split runs through the whole app (teacher.md §3.2).
        entry[QStringLiteral("theta")] = core::thetaOf(m_skill, dimension);
        entry[QStringLiteral("share")] = a[i];
        entry[QStringLiteral("events")] = m_skill.events[i];
        out.append(entry);
    }
    return out;
}

QVariantMap TeacherEngine::session() const
{
    QVariantMap out;
    out[QStringLiteral("reviewCount")] = static_cast<int>(m_sessionPlan.blockA.size());
    out[QStringLiteral("newPattern")] = m_sessionPlan.blockB.empty()
            ? QString()
            : QString::fromStdString(m_sessionPlan.blockB.front().cardId);
    out[QStringLiteral("newPatternSkipped")] = m_sessionPlan.newPatternSkipped;
    out[QStringLiteral("sparringSeconds")] = m_sessionPlan.sparringSeconds;
    out[QStringLiteral("index")] = m_sessionIndex;
    out[QStringLiteral("total")] = static_cast<int>(m_sessionPlan.blockA.size()
                                                    + m_sessionPlan.blockB.size());
    // The one absolute number the app shows (teacher.md §3.2 c).
    out[QStringLiteral("blunderRate")] = core::blunderRate(m_skill);
    out[QStringLiteral("analysisDone")] = m_analysisDone;
    out[QStringLiteral("analysisTotal")] = m_analysisTotal;
    if (!m_engineMessage.isEmpty())
        out[QStringLiteral("engineMessage")] = m_engineMessage;
    return out;
}

int TeacherEngine::dueCards() const
{
    return m_database->isOpen() ? m_database->dueCardCount(today()) : 0;
}

// --- the invokables ----------------------------------------------------------

void TeacherEngine::setItemBankPath(const QString& path)
{
    if (!m_items.load(path))
        qWarning("placement items unavailable: %s", qPrintable(m_items.error()));
}

void TeacherEngine::startPlacement()
{
    delete m_placement;
    m_placement = new core::Placement(0);
    m_usedItems.clear();
    setMode(Placement);
    m_taskClock.start();
    // §4.1: it must not feel like an exam — no timer, no running score, the
    // heading says "warming up".
    setPrompt(tr("Aufwärmen: ein paar Stellungen, damit ich weiß, wo wir anfangen."));
    loadNextTask();
}

void TeacherEngine::startSession()
{
    if (!m_database->isOpen()) {
        setPrompt(tr("Ich kann gerade nichts speichern, aber spielen geht."));
        return;
    }
    const qint64 day = today();
    const QVector<core::Card> due = m_database->dueCards(day);
    std::vector<core::Card> pool;
    for (int i = 0; i < due.size(); ++i)
        pool.push_back(due.at(i));

    QString newPattern;
    core::Dimension newDimension = core::Dimension::TAK;
    const std::vector<core::Dimension> byNeed = core::byShare(m_skill);
    if (!byNeed.empty())
        newDimension = byNeed.front();

    m_sessionPlan = core::buildSession(pool, newPattern.toStdString(), newDimension,
                                       m_database->newCardsCreatedOn(day),
                                       m_database->backlogCount(day));
    m_sessionCards = due;
    m_sessionIndex = 0;
    setMode(Drill);
    setPrompt(m_sessionPlan.blockA.empty()
                      ? tr("Heute ist nichts fällig. Spiel eine Partie — daraus wird das Material.")
                      : tr("Wiederholen, dann etwas Neues, dann spielen."));
    emit progressChanged();
    loadNextTask();
}

void TeacherEngine::startSparring(int handicap)
{
    m_learnerIsWhite = (handicap % 2) == 0;
    startNewGame(m_learnerIsWhite);
    setMode(Sparring);

    // §7.3: the opponent makes the mistakes the learner does not punish, so
    // the classes are drawn from the learner's own error list.
    QVector<core::ErrorClass> wanted;
    const std::vector<core::Dimension> byNeed = core::byShare(m_skill);
    for (std::size_t i = 0; i < byNeed.size() && wanted.size() < 3; ++i) {
        for (int j = 0; j < core::kClassCount; ++j) {
            const core::ErrorClass cls = core::kAllClasses[j];
            if (core::dimensionOf(cls) == byNeed[i] && core::makesCard(cls)) {
                wanted.append(cls);
                break;
            }
        }
    }
    m_sparring->reset(learnerElo(), wanted);
    setPrompt(tr("Spiel deine Partie. Ich sage dir hinterher, was wichtig war."));
    clearFeedback();
    if (!m_learnerIsWhite)
        askOpponent();
}

void TeacherEngine::startNewGame(bool learnerPlaysWhite)
{
    m_position.reset();
    m_learnerIsWhite = learnerPlaysWhite;
    m_flipped = !learnerPlaysWhite;
    m_selected = -1;
    m_findings.clear();
    m_currentGameId = -1;
    emit positionChanged();
    emit selectionChanged();
    emit boardChanged();
}

bool TeacherEngine::play(int fromSquare, int toSquare, const QString& promotion)
{
    // Looking at a solved item is reading, not playing.
    if (m_reviewIndex >= 0 || m_position.gameOver())
        return false;
    std::string uci = core::squareName(fromSquare) + core::squareName(toSquare);
    if (m_position.needsPromotion(fromSquare, toSquare)) {
        const QString piece = promotion.isEmpty() ? QStringLiteral("q") : promotion.toLower();
        uci += piece.left(1).toStdString();
    }
    if (!m_position.isLegal(uci))
        return false;

    if (m_mode == Drill || m_mode == Placement) {
        const int milliseconds = m_taskClock.isValid() ? static_cast<int>(m_taskClock.elapsed()) : 0;
        // An item may have more than one move that is just as good; the engine
        // decided that when the bank was built, not the learner here.
        const QString played = QString::fromStdString(uci);
        const bool correct = !m_solutionUci.isEmpty()
                && (played == m_solutionUci
                    || m_task.value(QStringLiteral("alsoAccepted")).toStringList().contains(played));
        m_lastAnswer = played;
        m_position.play(uci);
        m_selected = -1;
        emit positionChanged();
        emit selectionChanged();
        finishDrillTask(correct, milliseconds);
        return true;
    }

    m_position.play(uci);
    m_selected = -1;
    emit positionChanged();
    emit selectionChanged();

    if (m_mode == Sparring) {
        m_sparring->noteMovePlayed();
        if (m_sparring->noteLearnerMove(m_position)) {
            // §7.4 fired: the chance is gone and became an exercise.
        }
        if (!m_position.gameOver())
            askOpponent();
        else
            analyseCurrentGame();
    }
    return true;
}

void TeacherEngine::askOpponent()
{
    if (!m_engine->available()) {
        setFeedback(tr("Ohne Engine kann ich nicht mitspielen. Brett und Wiederholungen "
                       "funktionieren trotzdem."),
                    QStringLiteral("engine.missing"));
        return;
    }
    m_opponentTimer->start(kOpponentDelayMs);
}

void TeacherEngine::onOpponentTurn()
{
    if (m_mode != Sparring || m_position.gameOver())
        return;
    QStringList moves;
    const std::vector<std::string>& history = m_position.history();
    for (std::size_t i = 0; i < history.size(); ++i)
        moves << QString::fromStdString(history[i]);
    // MultiPV 8: §7.3 picks the deliberate mistake from the eight best moves.
    m_engine->analyseMovetime(QString::fromStdString(m_position.startFen()), moves,
                              kSparringMovetimeMs, 8, QStringLiteral("sparring"));
}

void TeacherEngine::onEngineResult(const EngineResult& result)
{
    if (result.tag.toString() != QLatin1String("sparring"))
        return;
    if (m_mode != Sparring || m_position.gameOver())
        return;

    QVector<ScoredMove> candidates;
    for (int i = 0; i < result.lines.size(); ++i) {
        const core::uciproto::Info& info = result.lines.at(i);
        if (info.pv.empty() || !info.score.valid)
            continue;
        ScoredMove move;
        move.uci = QString::fromStdString(info.pv.front());
        move.score = info.score;
        candidates.append(move);
    }
    if (candidates.isEmpty() && !result.bestMove.isEmpty()) {
        ScoredMove move;
        move.uci = result.bestMove;
        candidates.append(move);
    }

    const QString chosen = m_sparring->chooseMove(m_position, candidates);
    if (chosen.isEmpty() || !m_position.play(chosen.toStdString()))
        return;
    emit positionChanged();
    if (m_position.gameOver())
        analyseCurrentGame();
}

void TeacherEngine::onEngineFailed(const QString& reason)
{
    m_engineMessage = reason;
    emit engineChanged();
    emit progressChanged();
}

void TeacherEngine::takeBack()
{
    if (!m_position.undo())
        return;
    // §7.6: taking back is free and unlimited, and the event is written into
    // the error list anyway. Taking back changes the game, not the diagnosis —
    // and the learner is told so.
    if (m_mode == Sparring) {
        m_position.undo();   // the opponent's reply as well
        setFeedback(tr("Zurückgenommen. Ich merke es mir trotzdem — sonst kann ich dir nicht helfen."),
                    QStringLiteral("takeback"));
    }
    m_selected = -1;
    emit positionChanged();
    emit selectionChanged();
}

void TeacherEngine::requestHint()
{
    // teacher.md §6.6: four levels, and none of them names the theme.
    ++m_hintLevel;
    switch (m_hintLevel) {
    case 1:
        setFeedback(tr("Schau dir an, was nach deinem Zug mit deinen ungedeckten Steinen passiert."),
                    QStringLiteral("hint.1"));
        break;
    case 2:
        setFeedback(tr("Dieselbe Idee mit vier Steinen: Wo stehen die beiden Figuren, die er "
                       "gleichzeitig erreichen kann?"),
                    QStringLiteral("hint.2"));
        break;
    case 3:
        if (!m_solutionUci.isEmpty()) {
            setFeedback(tr("Der erste Zug ist %1. Die Folge findest du selbst.")
                                .arg(QString::fromStdString(m_position.sanOf(m_solutionUci.toStdString()))),
                        QStringLiteral("hint.3"));
        }
        break;
    default:
        if (!m_solutionUci.isEmpty()) {
            setFeedback(tr("Die Lösung ist %1.")
                                .arg(QString::fromStdString(m_position.sanOf(m_solutionUci.toStdString()))),
                        QStringLiteral("hint.4"));
        }
        break;
    }
}

void TeacherEngine::skipTask()
{
    if (m_reviewIndex >= 0)
        return;
    if (m_mode == Placement) {
        // "I don't see it" is a valid answer and the page says so — but it has
        // to be counted as one, or the test never reaches its twenty-five.
        if (m_placement) {
            const double difficulty = m_task.value(QStringLiteral("difficulty")).toDouble();
            const core::Dimension dimension = core::dimensionFromKey(
                m_task.value(QStringLiteral("dimension")).toString().toStdString());
            m_placement->record(dimension, difficulty, false);
        }
        m_lastAnswer.clear();
        rememberAnswer(false);
        setFeedback(m_answered.isEmpty() || m_answered.last().solutionSan.isEmpty()
                        ? tr("Übersprungen.")
                        : tr("Übersprungen. Der Zug war %1.").arg(m_answered.last().solutionSan),
                    QStringLiteral("placement"));
        m_hintLevel = 0;
        loadNextTask();
        return;
    }
    if (m_mode == Drill) {
        ++m_sessionIndex;
        m_hintLevel = 0;
        loadNextTask();
    }
}

void TeacherEngine::loadNextTask()
{
    m_hintLevel = 0;
    m_solutionUci.clear();
    m_currentCardId.clear();
    m_task.clear();

    if (m_mode == Placement && m_placement) {
        const double elapsed = m_taskClock.isValid() ? m_taskClock.elapsed() / 1000.0 : 0.0;
        if (m_placement->finished(elapsed)) {
            const core::PlacementPlan plan = m_placement->plan();
            m_skill.theta = plan.theta;
            for (int i = 0; i < core::kDimensionCount; ++i)
                m_skill.delta[i] = plan.thetaD[i] - plan.theta;
            if (m_database->isOpen()) {
                QVector<double> perDimension;
                for (int i = 0; i < core::kDimensionCount; ++i)
                    perDimension.append(plan.thetaD[i]);
                m_database->recordSkill(QDateTime::currentMSecsSinceEpoch() / 1000, plan.theta,
                                        perDimension, core::blunderRate(m_skill));
            }
            setMode(Idle);
            // §4.6: never "your endgame is 1240" — a starting point and at most
            // one remarkable dimension, because that is all 25 items support.
            setPrompt(plan.firstAreas.empty()
                              ? tr("Wir fangen bei Aufgaben um %1 an. Auffällig war nichts — "
                                   "das ist die ehrliche Auskunft.")
                                        .arg(static_cast<int>(plan.startDifficulty))
                              : tr("Wir fangen bei Aufgaben um %1 an und schauen uns zuerst %2 an.")
                                        .arg(static_cast<int>(plan.startDifficulty))
                                        .arg(QString::fromLatin1(
                                                core::dimensionName(plan.firstAreas.front()))));
            emit progressChanged();
            return;
        }
        const core::Dimension dimension = m_placement->nextDimension();
        const double difficulty = m_placement->nextDifficulty();
        const PlacementItem* item = m_items.pick(dimension, difficulty, m_usedItems);
        if (!item) {
            // No position, no question. Saying so is the only honest answer;
            // asking about the starting position would measure nothing.
            setMode(Idle);
            setPrompt(m_items.isEmpty()
                          ? tr("Der Aufgabenbestand für den Einstufungstest fehlt in dieser "
                               "Installation. Spielen und Üben geht trotzdem.")
                          : tr("Die Aufgaben sind aufgebraucht."));
            emit progressChanged();
            return;
        }
        m_usedItems << item->id;
        m_position.setFen(item->fen.toStdString());
        m_selected = -1;
        m_solutionUci = item->solution;
        m_taskFen = item->fen;
        m_lastAnswer.clear();

        QVariantMap task;
        task[QStringLiteral("kind")] = QStringLiteral("placement");
        // §6.2: the learner never learns the theme *before* solving, so the
        // dimension is carried for the estimator only, not for display.
        task[QStringLiteral("difficulty")] = difficulty;
        task[QStringLiteral("index")] = m_placement->answered() + 1;
        task[QStringLiteral("total")] = 25;
        task[QStringLiteral("dimension")] = QString::fromLatin1(core::dimensionKey(dimension));
        task[QStringLiteral("itemId")] = item->id;
        task[QStringLiteral("explanationAfterSolving")] = item->explanation;
        task[QStringLiteral("alsoAccepted")] = item->alsoAccepted;
        m_task = task;
        setPrompt(m_position.whiteToMove() ? tr("Weiß am Zug: Was machst du hier?")
                                           : tr("Schwarz am Zug: Was machst du hier?"));
        m_taskClock.restart();
        emit positionChanged();
        emit selectionChanged();
        emit taskChanged();
        return;
    }

    if (m_mode == Drill) {
        if (m_sessionIndex >= m_sessionCards.size()) {
            setMode(Idle);
            setPrompt(tr("Fertig für heute. Der Rest kommt aus deinen Partien."));
            emit progressChanged();
            return;
        }
        presentCard(m_sessionCards.at(m_sessionIndex));
    }
}

// --- looking back at the solutions --------------------------------------------------

void TeacherEngine::rememberAnswer(bool correct)
{
    AnsweredItem entry;
    entry.itemId = m_task.value(QStringLiteral("itemId")).toString();
    entry.fen = m_taskFen;
    entry.solution = m_solutionUci;
    entry.played = m_lastAnswer;
    entry.explanation = m_task.value(QStringLiteral("explanationAfterSolving")).toString();
    entry.correct = correct;
    entry.number = m_answered.size() + 1;
    entry.difficulty = m_task.value(QStringLiteral("difficulty")).toDouble();

    // The move reads better as "Sf7+" than as "g5f7"; both are worked out on
    // the position as it was asked, not on the one now on the board.
    if (!entry.fen.isEmpty()) {
        core::Position position;
        if (position.setFen(entry.fen.toStdString())) {
            if (!entry.solution.isEmpty())
                entry.solutionSan = QString::fromStdString(
                    position.sanOf(entry.solution.toStdString()));
            if (!entry.played.isEmpty())
                entry.playedSan = QString::fromStdString(
                    position.sanOf(entry.played.toStdString()));
        }
    }
    m_answered.append(entry);
    emit reviewChanged();
}

QVariantMap TeacherEngine::review() const
{
    QVariantMap map;
    if (m_reviewIndex < 0 || m_reviewIndex >= m_answered.size())
        return map;
    const AnsweredItem& entry = m_answered.at(m_reviewIndex);
    map[QStringLiteral("number")] = entry.number;
    map[QStringLiteral("total")] = m_answered.size();
    map[QStringLiteral("correct")] = entry.correct;
    map[QStringLiteral("solution")] = entry.solution;
    map[QStringLiteral("solutionSan")] = entry.solutionSan;
    map[QStringLiteral("played")] = entry.played;
    map[QStringLiteral("playedSan")] = entry.playedSan;
    map[QStringLiteral("skipped")] = entry.played.isEmpty();
    map[QStringLiteral("explanation")] = entry.explanation;
    map[QStringLiteral("difficulty")] = static_cast<int>(entry.difficulty);
    return map;
}

void TeacherEngine::showAnswered(int index)
{
    if (index < 0 || index >= m_answered.size())
        return;
    if (m_reviewIndex < 0) {
        m_liveFen = QString::fromStdString(m_position.fen());
        m_liveSolution = m_solutionUci;
        m_livePrompt = m_prompt;
        m_liveTask = m_task;
        m_liveSaved = true;
    }
    m_reviewIndex = index;
    const AnsweredItem& entry = m_answered.at(index);
    m_position.setFen(entry.fen.toStdString());
    m_selected = -1;
    // The board highlights lastMove(); while reviewing that is the solution,
    // so the right move is marked on the position it was asked in.
    setPrompt(entry.correct
                  ? tr("Aufgabe %1 von %2 — richtig.").arg(entry.number).arg(m_answered.size())
                  : tr("Aufgabe %1 von %2.").arg(entry.number).arg(m_answered.size()));
    QString text;
    if (!entry.solutionSan.isEmpty())
        text = tr("Der Zug war %1.").arg(entry.solutionSan);
    if (!entry.correct && !entry.playedSan.isEmpty())
        text = tr("Du hast %1 gespielt, richtig war %2.").arg(entry.playedSan, entry.solutionSan);
    else if (!entry.correct && entry.played.isEmpty())
        text = tr("Übersprungen. Richtig war %1.").arg(entry.solutionSan);
    if (!entry.explanation.isEmpty())
        text += QStringLiteral(" ") + entry.explanation;
    setFeedback(text, QStringLiteral("review"));
    emit positionChanged();
    emit selectionChanged();
    emit taskChanged();
    emit reviewChanged();
}

void TeacherEngine::reviewPrevious()
{
    if (m_answered.isEmpty())
        return;
    showAnswered(m_reviewIndex < 0 ? m_answered.size() - 1 : m_reviewIndex - 1);
}

void TeacherEngine::reviewNext()
{
    if (m_reviewIndex >= 0 && m_reviewIndex + 1 < m_answered.size())
        showAnswered(m_reviewIndex + 1);
    else if (m_reviewIndex >= 0)
        endReview();
}

void TeacherEngine::reviewItem(int index)
{
    showAnswered(index);
}

void TeacherEngine::endReview()
{
    if (m_reviewIndex < 0)
        return;
    m_reviewIndex = -1;
    emit reviewChanged();
    // Back to exactly where the test stood — the same item, not a fresh one.
    if (m_liveSaved) {
        if (!m_liveFen.isEmpty())
            m_position.setFen(m_liveFen.toStdString());
        m_solutionUci = m_liveSolution;
        m_task = m_liveTask;
        setPrompt(m_livePrompt);
        setFeedback(QString(), QString());
        m_selected = -1;
        m_liveSaved = false;
        m_taskClock.restart();
    }
    emit positionChanged();
    emit selectionChanged();
    emit taskChanged();
}

void TeacherEngine::presentCard(const core::Card& card)
{
    m_currentCardId = QString::fromStdString(card.id);
    m_solutionUci = QString::fromStdString(card.solutionUci);
    m_position.setFen(card.seedFen);
    m_selected = -1;

    QVariantMap task;
    task[QStringLiteral("kind")] = QStringLiteral("card");
    task[QStringLiteral("cardId")] = m_currentCardId;
    // The title is deliberately *not* part of the prompt: naming the motif
    // before solving destroys the measurement (§6.2).
    task[QStringLiteral("titleAfterSolving")] = QString::fromStdString(card.title);
    task[QStringLiteral("index")] = m_sessionIndex + 1;
    task[QStringLiteral("total")] = m_sessionCards.size();
    m_task = task;
    setPrompt(tr("Am Zug: Was machst du hier?"));
    m_taskClock.restart();
    emit positionChanged();
    emit selectionChanged();
    emit taskChanged();
}

void TeacherEngine::finishDrillTask(bool correct, int milliseconds)
{
    if (m_mode == Placement && m_placement) {
        const double difficulty = m_task.value(QStringLiteral("difficulty")).toDouble();
        const core::Dimension dimension =
                core::dimensionFromKey(m_task.value(QStringLiteral("dimension")).toString().toStdString());
        m_placement->record(dimension, difficulty, correct);
        rememberAnswer(correct);
        // §4.1: no right/wrong signal after every item, only the solution.
        setFeedback(correct ? tr("So geht es.") : tr("Hier war %1 besser.")
                                      .arg(m_answered.isEmpty() || m_answered.last().solutionSan.isEmpty()
                                               ? tr("ein anderer Zug")
                                               : m_answered.last().solutionSan),
                    QStringLiteral("placement"));
        loadNextTask();
        return;
    }

    // §5.1: the grade is derived, never asked.
    const core::Rating rating = core::ratingFor(correct, m_hintLevel > 0, milliseconds);
    if (m_database->isOpen() && !m_currentCardId.isEmpty()) {
        core::Card card;
        if (m_database->loadCard(m_currentCardId, card)) {
            card.srs = core::applyReview(card.srs, rating, today());
            if (core::retirementReached(card.srs, card.transferSightings,
                                        today() - card.createdDay,
                                        today() - card.lastOccurrenceDay))
                card.srs.state = core::CardState::Retired;
            m_database->upsertCard(card);
        }
        m_database->recordReview(m_currentCardId, QDateTime::currentMSecsSinceEpoch() / 1000,
                                 rating, milliseconds, correct, m_hintLevel > 0);
    }
    setFeedback(correct
                        ? tr("Richtig. %1").arg(m_task.value(QStringLiteral("titleAfterSolving")).toString())
                        : tr("Der Zug hält nicht. Richtig war %1.").arg(m_solutionUci),
                QStringLiteral("drill"));
    ++m_sessionIndex;
    emit progressChanged();
    loadNextTask();
}

void TeacherEngine::analyseCurrentGame()
{
    if (m_position.history().empty())
        return;
    if (!m_engine->available()) {
        setFeedback(tr("Ohne Engine kann ich die Partie nicht durchsehen."),
                    QStringLiteral("engine.missing"));
        return;
    }
    setMode(Review);

    GameInput input;
    input.initialFen = QString::fromStdString(m_position.startFen());
    const std::vector<std::string>& history = m_position.history();
    for (std::size_t i = 0; i < history.size(); ++i)
        input.moves << QString::fromStdString(history[i]);
    input.learnerIsWhite = m_learnerIsWhite;
    input.learnerElo = learnerElo();

    if (m_database->isOpen()) {
        GameRecord record;
        record.playedAt = QDateTime::currentMSecsSinceEpoch() / 1000;
        record.white = m_learnerIsWhite ? tr("Lernender") : tr("Trainingsgegner");
        record.black = m_learnerIsWhite ? tr("Trainingsgegner") : tr("Lernender");
        record.result = m_position.outcome() == core::Outcome::WhiteWins ? QStringLiteral("1-0")
                      : m_position.outcome() == core::Outcome::BlackWins ? QStringLiteral("0-1")
                      : m_position.outcome() == core::Outcome::Draw ? QStringLiteral("1/2-1/2")
                                                                    : QStringLiteral("*");
        core::PgnTags tags;
        tags.white = record.white.toStdString();
        tags.black = record.black.toStdString();
        tags.result = record.result.toStdString();
        tags.date = QDateTime::currentDateTime().toString(QStringLiteral("yyyy.MM.dd")).toStdString();
        record.pgn = QString::fromStdString(m_position.toPgn(tags));
        record.initialFen = input.initialFen;
        m_currentGameId = m_database->insertGame(record);
        input.gameId = m_currentGameId;
    }

    m_analysisDone = 0;
    m_analysisTotal = input.moves.size() / 2;
    setPrompt(tr("Ich schaue mir die Partie an. Das dauert einen Moment."));
    m_analyser->start(input);
}

void TeacherEngine::onAnalysisProgress(int done, int total)
{
    m_analysisDone = done;
    m_analysisTotal = total;
    emit progressChanged();
}

void TeacherEngine::onAnalysisFinished(const QVector<core::Finding>& findings)
{
    m_findings = findings;
    for (int i = 0; i < findings.size(); ++i) {
        core::Finding finding = findings.at(i);
        core::addEvent(m_skill, finding);
        if (!m_database->isOpen())
            continue;
        FindingRecord record;
        record.gameId = m_currentGameId;
        record.ply = finding.ply;
        record.code = QString::fromLatin1(core::errorKey(finding.cls));
        record.dimension = QString::fromLatin1(core::dimensionKey(finding.dimension));
        record.severity = finding.severity;
        record.deltaW = finding.dW;
        record.fen = QString::fromStdString(finding.fen);
        record.playedUci = QString::fromStdString(finding.playedMove);
        record.bestUci = QString::fromStdString(finding.bestMove);
        record.motif = QString::fromLatin1(core::motifKey(finding.motif));
        record.sentence = QString::fromStdString(finding.sentence);
        m_database->insertFinding(record);
    }
    m_skill.ownMoves += m_position.history().size() / 2;

    const QVector<core::Card> cards = Analyser::cardsFor(findings, today());
    for (int i = 0; i < cards.size(); ++i) {
        if (m_database->isOpen())
            m_database->upsertCard(cards.at(i));
    }

    // §7.7: one sentence about the result, at most five moments, one sentence
    // about the consequence. No move list, no evaluation graph, no accuracy.
    int events = 0;
    for (int i = 0; i < findings.size(); ++i) {
        if (findings.at(i).makesCard)
            ++events;
    }
    setPrompt(events == 0
                      ? tr("Sauber. Diesmal war nichts dabei, was sich zu üben lohnt.")
                      : tr("%n Stelle(n) waren wichtig. Schau sie dir an.", "", events));
    setMode(Review);
    emit progressChanged();
}

void TeacherEngine::onChanceMissed(const QString& fen, int errorClass)
{
    // §7.4: the missed chance becomes a card in the same session.
    if (!m_database->isOpen())
        return;
    core::Finding finding;
    finding.cls = static_cast<core::ErrorClass>(errorClass);
    finding.dimension = core::dimensionOf(finding.cls);
    finding.fen = fen.toStdString();
    finding.makesCard = core::makesCard(finding.cls);
    finding.dW = 20.0;
    finding.sentence = core::errorTemplate(finding.cls);
    const core::Card card = core::cardFromFinding(finding, today());
    m_database->upsertCard(card);
    emit progressChanged();
}

QVariantList TeacherEngine::lastFindings() const
{
    QVariantList out;
    for (int i = 0; i < m_findings.size(); ++i) {
        const core::Finding& finding = m_findings.at(i);
        QVariantMap entry;
        entry[QStringLiteral("key")] = QString::fromLatin1(core::errorKey(finding.cls));
        entry[QStringLiteral("name")] = QString::fromLatin1(core::errorName(finding.cls));
        entry[QStringLiteral("dimension")] =
                QString::fromLatin1(core::dimensionKey(finding.dimension));
        entry[QStringLiteral("motif")] = QString::fromLatin1(core::motifName(finding.motif));
        entry[QStringLiteral("severity")] = finding.severity;
        entry[QStringLiteral("ply")] = finding.ply;
        entry[QStringLiteral("fen")] = QString::fromStdString(finding.fen);
        entry[QStringLiteral("played")] = QString::fromStdString(finding.playedMove);
        entry[QStringLiteral("best")] = QString::fromStdString(finding.bestMove);
        // Always the sentence. Never a number on its own (§6.6).
        entry[QStringLiteral("text")] = QString::fromStdString(finding.sentence);
        entry[QStringLiteral("makesCard")] = finding.makesCard;
        out.append(entry);
    }
    return out;
}

} // namespace schach
