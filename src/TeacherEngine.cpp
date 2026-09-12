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

#include <QLocale>
#include <QtGlobal>

#include <QDateTime>
#include <QFileInfo>

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
    , m_measuredAt(0)
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
    , m_lichess(new Lichess(this))
    , m_sync(0)
    , m_clockTimer(new QTimer(this))
    , m_clockWhiteMs(0)
    , m_clockBlackMs(0)
    , m_clockStampMs(0)
    , m_clockRunning(false)
    , m_clockWhiteToMove(true)
    , m_gameSource(QStringLiteral("local"))
    , m_syncedGameId(-1)
    , m_syncedLearnerIsWhite(true)
    , m_syncSeen(0)
    , m_syncImported(0)
{
    m_sync = new GameSync(m_lichess, m_database, this);
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

    // Lichess (platform.md §3). The fair-play lock hangs off gameStarted and
    // gameFinished and off nothing else.
    connect(m_lichess, SIGNAL(stateChanged()), this, SLOT(onLichessChanged()));
    connect(m_lichess, SIGNAL(messageChanged()), this, SLOT(onLichessChanged()));
    connect(m_lichess, SIGNAL(accountChanged()), this, SLOT(onLichessChanged()));
    connect(m_lichess, SIGNAL(challengesChanged()), this, SLOT(onLichessChanged()));
    connect(m_lichess, SIGNAL(seekingChanged()), this, SLOT(onLichessChanged()));
    connect(m_lichess, SIGNAL(failed(QString)), this, SLOT(onLichessFailed(QString)));
    connect(m_lichess, SIGNAL(gameStarted(QString)), this, SLOT(onOnlineGameStarted(QString)));
    connect(m_lichess, SIGNAL(gameUpdated()), this, SLOT(onOnlineGameUpdated()));
    connect(m_lichess, SIGNAL(gameFinished(QString, QString, QString)),
            this, SLOT(onOnlineGameFinished(QString, QString, QString)));
    connect(m_sync, SIGNAL(gameStored(qint64, QString, QStringList, bool)),
            this, SLOT(onSyncStored(qint64, QString, QStringList, bool)));
    connect(m_sync, SIGNAL(finished(int)), this, SLOT(onSyncFinished(int)));
    connect(m_sync, SIGNAL(progress(int, int)), this, SLOT(onLichessChanged()));
    connect(m_sync, SIGNAL(failed(QString)), this, SLOT(onLichessFailed(QString)));
    m_clockTimer->setInterval(250);
    connect(m_clockTimer, SIGNAL(timeout()), this, SLOT(onClockTick()));
    // The routine is phase-bound (the opening and the endgame question exclude
    // each other), so it follows the board.
    connect(this, SIGNAL(positionChanged()), this, SIGNAL(routineChanged()));

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
    // §3.3: the token lives beside the database, in the app's own data
    // directory, with 0600 — never in QSettings.
    m_lichess->setDataDirectory(QFileInfo(databasePath).absolutePath());
    // A token from an earlier run means the event stream can come up at once;
    // no token means the app behaves exactly as it did before M8.
    m_lichess->loadToken();

    double theta = 1000.0;
    QVector<double> perDimension;
    if (m_database->latestSkill(theta, perDimension)) {
        m_skill.theta = theta;
        for (int i = 0; i < perDimension.size() && i < core::kDimensionCount; ++i)
            m_skill.delta[i] = perDimension.at(i) - theta;
        m_measuredAt = m_database->lastMeasuredAt();
    }
    reloadHistory();
    // A missing engine is a normal state: the board, the repetitions and the
    // rules work without it, only sparring and analysis do not (§5).
    if (!m_engine->start())
        m_engineMessage = m_engine->lastError();
    emit engineChanged();
    emit progressChanged();
    emit routineChanged();
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
    // An online game ends for reasons the board cannot see — resignation, a
    // flag, an abort. The server's verdict wins there.
    if (m_mode == Online && m_lichess->game().valid && m_lichess->game().finished()) {
        const LichessGame& game = m_lichess->game();
        const QString status = game.status;
        const bool weWon = !game.winner.isEmpty()
                && ((game.winner == QLatin1String("white")) == game.weAreWhite);
        if (status == QLatin1String("aborted"))
            return tr("Die Partie wurde abgebrochen.");
        if (status == QLatin1String("draw"))
            return tr("Remis.");
        if (status == QLatin1String("stalemate"))
            return tr("Patt — unentschieden.");
        if (game.winner.isEmpty())
            return tr("Die Partie ist zu Ende.");
        if (status == QLatin1String("mate"))
            return weWon ? tr("Matt. Gewonnen.") : tr("Matt. Diese geht an ihn.");
        if (status == QLatin1String("resign"))
            return weWon ? tr("Er hat aufgegeben.") : tr("Du hast aufgegeben.");
        if (status == QLatin1String("outoftime") || status == QLatin1String("timeout"))
            return weWon ? tr("Seine Zeit ist abgelaufen.") : tr("Deine Zeit ist abgelaufen.");
        return weWon ? tr("Gewonnen.") : tr("Verloren.");
    }
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

void TeacherEngine::setFeedback(const QString& text, const QString& key, const core::Score* score,
                                core::ErrorClass cls)
{
    // teacher.md §6.6: never a number alone, never a bare "wrong". `cp` and
    // `wp` are carried for the log, not for the screen.
    QVariantMap feedback;
    feedback[QStringLiteral("text")] = text;
    feedback[QStringLiteral("key")] = key;
    // The link back: which question of his own routine would have caught this
    // (§6.6 — the hints *are* the questions). The sentence above stays as it
    // is; this is added to it, never instead of it, and it is empty when the
    // class is not one the routine has a question for.
    const int index = core::questionIndexFor(cls);
    if (index >= 0) {
        const core::Question& question = core::questionAt(index);
        const int rank = core::rankOf(currentRoutine(), index);
        QVariantMap entry;
        entry[QStringLiteral("id")] = QString::fromLatin1(question.id);
        entry[QStringLiteral("text")] = QString::fromUtf8(question.text);
        entry[QStringLiteral("rank")] = rank;
        entry[QStringLiteral("sentence")] =
                QString::fromStdString(core::caughtSentence(rank, question));
        feedback[QStringLiteral("question")] = entry;
    }
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

// --- Was frage ich mich? (teacher.md §6.6, §7.5) ------------------------------

void TeacherEngine::reloadHistory()
{
    // The window is the one of §3.2 (b): the last ten games, unfiltered by L,
    // because this is a ratio and a filter would bend it.
    m_history.clear();
    if (!m_database->isOpen())
        return;
    const QVector<ClassTally> tallies = m_database->findingTallies(10);
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    for (int i = 0; i < tallies.size(); ++i) {
        const ClassTally& tally = tallies.at(i);
        const core::ErrorClass cls = core::errorFromKey(tally.code.toStdString());
        if (cls == core::ErrorClass::None)
            continue;
        int days = 0;
        if (tally.lastPlayedAt > 0 && now > tally.lastPlayedAt)
            days = static_cast<int>((now - tally.lastPlayedAt) / (60 * 60 * 24));
        m_history.add(cls, days, tally.count);
    }
}

core::RoutineView TeacherEngine::currentRoutine() const
{
    return core::personalise(m_history, m_position.phase());
}

QVariantList TeacherEngine::routine() const
{
    const core::RoutineView view = currentRoutine();
    QVariantList out;
    for (std::size_t i = 0; i < view.questions.size(); ++i) {
        const core::QuestionStatus& status = view.questions[i];
        const core::Question& question = core::questionAt(status.index);
        QVariantMap entry;
        entry[QStringLiteral("id")] = QString::fromLatin1(question.id);
        entry[QStringLiteral("text")] = QString::fromUtf8(question.text);
        entry[QStringLiteral("when")] = QString::fromUtf8(core::triggerName(question.when));
        entry[QStringLiteral("whenKey")] = QString::fromLatin1(core::triggerKey(question.when));
        entry[QStringLiteral("dimension")] =
                QString::fromLatin1(core::dimensionKey(question.dimension));
        entry[QStringLiteral("rank")] = static_cast<int>(i) + 1;
        entry[QStringLiteral("standing")] = QString::fromLatin1(core::standingKey(status.standing));
        entry[QStringLiteral("visible")] = status.standing != core::Standing::Retired;
        // The question that caught the last mistake is marked, and that is the
        // only emphasis in the list — no counters, no scores (§6.6, §9.2).
        entry[QStringLiteral("caught")] = status.caughtLatest;
        entry[QStringLiteral("fromSpec")] = question.fromSpec;
        entry[QStringLiteral("source")] = QString::fromUtf8(question.source);
        out.append(entry);
    }
    return out;
}

int TeacherEngine::drillMode() const
{
    switch (m_sparring->drillMode()) {
    case core::DrillMode::Always: return DrillAlways;
    case core::DrillMode::Never:  return DrillNever;
    case core::DrillMode::Auto:   break;
    }
    return DrillAuto;
}

void TeacherEngine::setDrillMode(int mode)
{
    const core::DrillMode wanted = mode == DrillAlways ? core::DrillMode::Always
                                 : mode == DrillNever  ? core::DrillMode::Never
                                                       : core::DrillMode::Auto;
    if (m_sparring->drillMode() == wanted)
        return;
    m_sparring->setDrillMode(wanted);
    if (wanted == core::DrillMode::Never && !m_heldMove.isEmpty()) {
        // Switching it off while it holds a move must not swallow the move.
        const QString held = m_heldMove;
        m_heldMove.clear();
        m_blunderCheck.clear();
        m_checkItems.clear();
        emit blunderCheckChanged();
        playInSparring(held.toStdString());
    }
    emit drillModeChanged();
}

bool TeacherEngine::beginBlunderCheck(const QString& uci)
{
    // §7.5: before the move is released, his checks and captures with SEE > 0,
    // hidden. §1.2.2 is the whole reason: 72 % of all refutations live in that
    // eleventh of the legal moves.
    m_checkItems = Sparring::blunderCheckList(m_position, uci);
    if (m_checkItems.isEmpty()) {
        // Nothing loud at all. Asking about an empty list teaches nothing, so
        // the move goes through and the counter is not spent.
        return false;
    }
    m_heldMove = uci;

    QVariantList moves;
    for (int i = 0; i < m_checkItems.size(); ++i) {
        QVariantMap entry;
        entry[QStringLiteral("index")] = i;
        entry[QStringLiteral("uci")] = m_checkItems.at(i).uci;
        // SAN only. Never the SEE, never an evaluation — that is the answer
        // and it is also a number (§6.6).
        entry[QStringLiteral("san")] = m_checkItems.at(i).san;
        moves.append(entry);
    }

    const core::Question* question = core::questionById("frage-schach-schlag");
    m_blunderCheck.clear();
    m_blunderCheck[QStringLiteral("active")] = true;
    m_blunderCheck[QStringLiteral("moves")] = moves;
    m_blunderCheck[QStringLiteral("question")] =
            question ? QString::fromUtf8(question->text) : QString();
    m_blunderCheck[QStringLiteral("prompt")] =
            tr("Bevor du den Zug freigibst: Das könnte er darauf spielen. "
               "Tipp an, was davon dich etwas kostet.");
    emit blunderCheckChanged();
    return true;
}

void TeacherEngine::answerBlunderCheck(const QVariantList& dangerous)
{
    if (m_heldMove.isEmpty())
        return;
    QVector<bool> marked(m_checkItems.size(), false);
    for (int i = 0; i < dangerous.size(); ++i) {
        const int index = dangerous.at(i).toInt();
        if (index >= 0 && index < marked.size())
            marked[index] = true;
    }
    bool correct = true;
    int missed = 0;
    for (int i = 0; i < m_checkItems.size(); ++i) {
        if (marked.at(i) != m_checkItems.at(i).dangerous) {
            correct = false;
            if (m_checkItems.at(i).dangerous && !marked.at(i))
                ++missed;
        }
    }
    m_sparring->noteBlunderCheck(correct);

    // A sentence in both cases, never a bare right/wrong (§6.6). The one after
    // a miss names the question the drill belongs to. It is set *before* the
    // move is released, because releasing it may end the game and the game's
    // own sentence then has the last word, which is the right order.
    if (correct)
        setFeedback(tr("Richtig gesehen. Jetzt dein Zug."), QStringLiteral("blundercheck.ok"));
    else if (missed > 0)
        setFeedback(tr("Einen davon hast du stehen lassen — genau so einer kostet die Partie. "
                       "Schau dir nach seinem Zug an, was er damit erreicht."),
                    QStringLiteral("blundercheck.missed"), 0, core::ErrorClass::A1);
    else
        setFeedback(tr("Die, die du angetippt hast, kosten dich nichts — er kann sie spielen, "
                       "du stehst danach genauso gut. Gefährlich ist nur, was Material gewinnt "
                       "oder ein Schach, hinter dem etwas hängt."),
                    QStringLiteral("blundercheck.overmarked"));

    finishBlunderCheck(correct);
}

void TeacherEngine::skipBlunderCheck()
{
    if (m_heldMove.isEmpty())
        return;
    // Not answering is not a wrong answer: the streak simply does not grow.
    finishBlunderCheck(false);
}

void TeacherEngine::finishBlunderCheck(bool correct)
{
    Q_UNUSED(correct)
    const QString held = m_heldMove;
    m_heldMove.clear();
    m_checkItems.clear();
    m_blunderCheck.clear();
    emit blunderCheckChanged();
    if (!held.isEmpty())
        playInSparring(held.toStdString());
}

void TeacherEngine::playInSparring(const std::string& uci)
{
    if (!m_position.isLegal(uci))
        return;
    m_position.play(uci);
    m_selected = -1;
    emit positionChanged();
    emit selectionChanged();

    m_sparring->noteMovePlayed();
    m_sparring->noteLearnerMove(m_position);   // §7.4 fires through chanceMissed()
    if (!m_position.gameOver())
        askOpponent();
    else
        analyseCurrentGame();
}

// --- progress ----------------------------------------------------------------

QString TeacherEngine::measuredOn() const
{
    if (m_measuredAt <= 0)
        return QString();
    // QLocale rather than Qt::DefaultLocaleShortDate: the same call works on
    // Qt 5.6 and is not deprecated on the newer Qt of the build host.
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(m_measuredAt * 1000).date(),
                              QLocale::ShortFormat);
}

int TeacherEngine::startDifficulty() const
{
    // §4.9: the level the first tasks are picked around, not a rating.
    return static_cast<int>(m_skill.theta + 0.5);
}

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
        // The ordinal verdict of §4.6. Twenty-five items carry about 76 Elo of
        // standard error, so a dimension has to be clearly apart before this
        // says anything — and saying "unremarkable" is the honest answer, not
        // a missing value. Without a measurement there is no verdict at all.
        const double apart = core::thetaOf(m_skill, dimension) - m_skill.theta;
        entry[QStringLiteral("band")] = !measured() ? QStringLiteral("")
                                     : (apart < -76.0 ? QStringLiteral("weak")
                                     : (apart > 76.0 ? QStringLiteral("strong")
                                                     : QStringLiteral("unremarkable")));
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
    if (refusedWhileLive())
        return;
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
    if (refusedWhileLive())
        return;
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
    // Nothing due yet — the usual state right after the placement test, because
    // cards are made from your own games and you have not played one here yet.
    // Rather than sending the learner away, start from the measured level with
    // the calibrated positions the test itself is built from. They need no
    // engine, so this also works when the engine is missing.
    bool starter = false;
    if (m_sessionCards.isEmpty()) {
        m_sessionCards = starterCards(newDimension);
        starter = !m_sessionCards.isEmpty();
    }
    m_sessionIndex = 0;
    setMode(Drill);
    setPrompt(!m_sessionCards.isEmpty()
                      ? (starter ? tr("Aufgaben auf deinem Niveau. Sobald du gespielt hast, "
                                      "kommen deine eigenen Fehler dazu.")
                                 : tr("Wiederholen, dann etwas Neues, dann spielen."))
                      : tr("Heute ist nichts fällig. Spiel eine Partie — daraus wird das Material."));
    emit progressChanged();
    loadNextTask();
}

void TeacherEngine::startSparring(int handicap)
{
    if (refusedWhileLive())
        return;
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
    m_heldMove.clear();
    m_checkItems.clear();
    m_blunderCheck.clear();
    emit blunderCheckChanged();
    // §7.5: the drill is compulsory for a learner whose error mass sits in
    // SRG. For everyone else it still runs, just as the fading scaffold it is.
    reloadHistory();
    emit routineChanged();
    setPrompt(tr("Spiel deine Partie. Ich sage dir hinterher, was wichtig war."));
    clearFeedback();
    if (!m_learnerIsWhite)
        askOpponent();
}

void TeacherEngine::startNewGame(bool learnerPlaysWhite)
{
    m_position.reset();
    m_gameSource = QStringLiteral("local");
    m_gameExternalId.clear();
    m_gameWhiteName.clear();
    m_gameBlackName.clear();
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

    if (m_mode == Online) {
        // §3.5 step 5: the move goes to Lichess and the board stays where it
        // is. The next `gameState` line is what moves it, and it is also what
        // carries the authoritative clocks — an optimistically advanced board
        // would disagree with the server on both.
        if (m_liveGameId.isEmpty() || !m_lichess->game().ourTurn())
            return false;
        m_lichess->sendMove(QString::fromStdString(uci));
        m_selected = -1;
        emit selectionChanged();
        return true;
    }

    if (m_mode == Sparring) {
        // §7.5: every third move (later every fifth, then off) the move is
        // held at the gate until the learner has looked at what the opponent
        // could answer. The move is accepted — it is not rejected, it waits.
        if (m_heldMove.isEmpty() && m_sparring->blunderCheckDue()
                && beginBlunderCheck(QString::fromStdString(uci))) {
            m_selected = -1;
            emit selectionChanged();
            return true;
        }
        playInSparring(uci);
        return true;
    }

    m_position.play(uci);
    m_selected = -1;
    emit positionChanged();
    emit selectionChanged();
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
    // A move the drill is holding was never played; letting it go is the
    // take-back (§7.6: taking back is free and costs nothing).
    if (!m_heldMove.isEmpty()) {
        m_heldMove.clear();
        m_checkItems.clear();
        m_blunderCheck.clear();
        emit blunderCheckChanged();
        return;
    }
    if (m_mode == Online) {
        // Online the take-back is not ours to grant: it is a request to the
        // opponent, and he may say no (§3.2, /takeback/yes).
        m_lichess->requestTakeback();
        setFeedback(tr("Ich habe deinen Gegner um Zurücknahme gebeten. "
                       "Er muss zustimmen."),
                    QStringLiteral("lichess.takeback"));
        return;
    }
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
    // platform.md §3.7: a hint during a running Lichess game is exactly the
    // "move recommendation from software" the fair-play rules forbid — even
    // when it comes from our own pocket. The door is closed here too, and it
    // says why.
    if (refusedWhileLive())
        return;
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
                m_measuredAt = QDateTime::currentMSecsSinceEpoch() / 1000;
                m_database->recordSkill(m_measuredAt, plan.theta,
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

// Practice material for a learner who has just been measured and has no cards
// yet: the placement items, picked around the measured level of the dimension
// that needs the work. They carry their own solution, so no engine is needed.
QVector<core::Card> TeacherEngine::starterCards(core::Dimension dimension) const
{
    QVector<core::Card> cards;
    if (m_items.isEmpty())
        return cards;
    const double level = core::thetaOf(m_skill, dimension);
    QStringList used;
    for (int i = 0; i < 6; ++i) {
        // Slightly below the measured level and rising: the first task of a
        // session should be solvable, not a test (teacher.md §5.6).
        const double target = level - 100.0 + i * 40.0;
        const PlacementItem* item = m_items.pick(dimension, target, used);
        if (!item)
            break;
        used << item->id;
        core::Card card;
        card.id = std::string("item/") + item->id.toStdString();
        card.dimension = item->dimension;
        card.pattern = "placement";
        card.title = item->explanation.isEmpty()
                ? QObject::tr("Übungsstellung").toStdString()
                : item->explanation.toStdString();
        card.origin = core::CardOrigin::Library;
        card.seedFen = item->fen.toStdString();
        card.solutionUci = item->solution.toStdString();
        cards.append(card);
    }
    return cards;
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
    // The hard rule of platform.md §3.7. The analysis of a game that is still
    // running is cheating, it is detected automatically, and it marks the
    // *user's* account. After the game it is allowed, and it is where the
    // learning happens — so this refuses while `m_liveGameId` is set and works
    // the moment the game is over. Never move this check.
    if (refusedWhileLive())
        return;
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
        record.source = m_gameSource;
        record.externalId = m_gameExternalId;
        record.playedAt = QDateTime::currentMSecsSinceEpoch() / 1000;
        record.white = m_gameWhiteName.isEmpty()
                ? (m_learnerIsWhite ? tr("Lernender") : tr("Trainingsgegner"))
                : m_gameWhiteName;
        record.black = m_gameBlackName.isEmpty()
                ? (m_learnerIsWhite ? tr("Trainingsgegner") : tr("Lernender"))
                : m_gameBlackName;
        record.result = m_position.outcome() == core::Outcome::WhiteWins ? QStringLiteral("1-0")
                      : m_position.outcome() == core::Outcome::BlackWins ? QStringLiteral("0-1")
                      : m_position.outcome() == core::Outcome::Draw ? QStringLiteral("1/2-1/2")
                                                                    : QStringLiteral("*");
        // A game that ended by resignation, on time or by agreement has an
        // "ongoing" board and a perfectly clear result; the server knows it and
        // the board does not.
        const LichessGame& online = m_lichess->game();
        if (m_gameSource == QLatin1String("lichess") && online.finished()) {
            record.result = online.winner == QLatin1String("white") ? QStringLiteral("1-0")
                          : online.winner == QLatin1String("black") ? QStringLiteral("0-1")
                          : online.status == QLatin1String("aborted") ? QStringLiteral("*")
                                                                      : QStringLiteral("1/2-1/2");
            record.timeControl = online.initialMs > 0
                    ? QStringLiteral("%1+%2").arg(online.initialMs / 1000).arg(online.incrementMs / 1000)
                    : record.timeControl;
        }
        core::PgnTags tags;
        tags.white = record.white.toStdString();
        tags.black = record.black.toStdString();
        tags.result = record.result.toStdString();
        tags.date = QDateTime::currentDateTime().toString(QStringLiteral("yyyy.MM.dd")).toStdString();
        record.pgn = QString::fromStdString(m_position.toPgn(tags));
        record.initialFen = input.initialFen;
        // A Lichess game may already be in the store from the download; it is
        // the same game and must not be counted twice.
        const qint64 known = m_database->gameIdOfExternalId(m_gameExternalId);
        m_currentGameId = known > 0 ? known : m_database->insertGame(record);
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

    // §7.5: an A1, B1 or C2 event brings the blunder check back at once.
    for (int i = 0; i < findings.size(); ++i) {
        if (core::BlunderCheckSchedule::bringsBack(findings.at(i).cls)) {
            m_sparring->requireBlunderCheck();
            break;
        }
    }
    // The record the routine orders itself by has just changed.
    reloadHistory();

    // §7.7: one sentence about the result, at most five moments, one sentence
    // about the consequence. No move list, no evaluation graph, no accuracy.
    int events = 0;
    const core::Finding* worst = 0;
    for (int i = 0; i < findings.size(); ++i) {
        if (findings.at(i).makesCard)
            ++events;
        if (!worst || findings.at(i).dW > worst->dW)
            worst = &findings.at(i);
    }
    setPrompt(events == 0
                      ? tr("Sauber. Diesmal war nichts dabei, was sich zu üben lohnt.")
                      : tr("%n Stelle(n) waren wichtig. Schau sie dir an.", "", events));
    // The sentence of §6.6 for the moment that cost the most, and after it the
    // question of his own routine that would have caught it. The sentence is
    // extended, not replaced.
    if (worst && !worst->sentence.empty())
        setFeedback(QString::fromStdString(worst->sentence),
                    QString::fromLatin1(core::errorKey(worst->cls)), 0, worst->cls);
    setMode(Review);
    emit progressChanged();
    emit routineChanged();
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
    // §7.5: C2 (and A1, B1) put the blunder check back on.
    if (core::BlunderCheckSchedule::bringsBack(finding.cls))
        m_sparring->requireBlunderCheck();
    setFeedback(tr("Die Gelegenheit ist vorbei — er hat sie stillschweigend beseitigt. "
                   "Du bekommst die Stellung gleich noch einmal."),
                QStringLiteral("chance.missed"), 0, finding.cls);
    emit progressChanged();
}

QVariantList TeacherEngine::lastFindings() const
{
    const core::RoutineView view = currentRoutine();
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
        // …and the question of the routine that would have caught it (§6.6).
        const int questionIndex = core::questionIndexFor(finding.cls);
        if (questionIndex >= 0) {
            const core::Question& question = core::questionAt(questionIndex);
            entry[QStringLiteral("question")] = QString::fromUtf8(question.text);
            entry[QStringLiteral("questionId")] = QString::fromLatin1(question.id);
            entry[QStringLiteral("questionSentence")] = QString::fromStdString(
                    core::caughtSentence(core::rankOf(view, questionIndex), question));
        }
        out.append(entry);
    }
    return out;
}

// =============================================================================
// Lichess (chess-spec/platform.md §3, docs/design.md §8 M8)
// =============================================================================
//
// The whole fair-play rule of §3.7 lives in four places and nowhere else:
//
//   1. `m_liveGameId` — set in beginLiveGame(), cleared in endLiveGame().
//   2. `EngineProcess::setFairPlayLock()` — terminates the process and refuses
//      every request, engine and tablebase alike.
//   3. `refusedWhileLive()` — every entry point in this class that would reach
//      the engine asks it first and answers with a sentence, not with silence.
//   4. `analysisAvailable` — what the pages read to **hide** the analysis
//      entry while a game is running. Hidden, not greyed out (§3.7).
//
// tests/test_fairplay.cpp is the regression test that keeps all four honest.

void TeacherEngine::beginLiveGame(const QString& gameId)
{
    if (gameId.isEmpty())
        return;
    m_liveGameId = gameId;
    // Not paused — terminated. A paused engine is one signal away from
    // answering a question that would mark the user's account.
    m_engine->cancelAll();
    m_analyser->cancel();
    m_opponentTimer->stop();
    m_engine->setFairPlayLock(true);
    m_heldMove.clear();
    m_checkItems.clear();
    m_blunderCheck.clear();
    emit blunderCheckChanged();
    emit engineChanged();
    emit onlineGameChanged();
}

void TeacherEngine::endLiveGame()
{
    if (m_liveGameId.isEmpty())
        return;
    m_liveGameId.clear();
    m_engine->setFairPlayLock(false);
    // The game is over, so the engine may come back. If the binary is missing
    // this fails exactly as it does everywhere else, and the app stays usable.
    if (m_engine->binaryPresent())
        m_engine->start();
    emit engineChanged();
    emit onlineGameChanged();
}

bool TeacherEngine::refusedWhileLive()
{
    if (m_liveGameId.isEmpty())
        return false;
    setFeedback(fairPlayNotice(), QStringLiteral("lichess.fairplay"));
    return true;
}

QString TeacherEngine::fairPlayNotice() const
{
    return tr("Solange deine Lichess-Partie läuft, ist die Engine aus — und zwar "
              "wirklich aus, das Programm läuft nicht mehr. Die Partie wird auf "
              "dem Server von Lichess gewertet, und jede Engine-Auskunft, jeder "
              "beste Zug und jede Endspieldatenbank wäre dort Betrug. Bestraft "
              "würde dein Konto, nicht die App. Nach der Partie sehen wir uns "
              "alles an — dann ist es erlaubt, und dann lernst du auch etwas daraus.");
}

bool TeacherEngine::analysisAvailable() const
{
    // False while a game is running: the pages hide the analysis entry on
    // this, they do not grey it out (§3.7).
    return m_liveGameId.isEmpty() && m_engine->available();
}

bool TeacherEngine::canAnalyseFinishedGame() const
{
    return analysisAvailable() && !m_position.history().empty();
}

int TeacherEngine::lichessState() const { return static_cast<int>(m_lichess->state()); }
bool TeacherEngine::lichessLoggedIn() const { return m_lichess->state() == Lichess::LoggedIn; }
QString TeacherEngine::lichessAccount() const { return m_lichess->account(); }
QString TeacherEngine::lichessMessage() const { return m_lichess->message(); }
QString TeacherEngine::lichessAuthUrl() const { return m_lichess->authorizationUrl(); }
bool TeacherEngine::lichessConnected() const { return m_lichess->eventStreamOpen(); }
bool TeacherEngine::lichessSeeking() const { return m_lichess->seeking(); }

QVariantList TeacherEngine::lichessChallenges() const
{
    QVariantList out;
    const QVector<LichessChallenge> challenges = m_lichess->challenges();
    for (int i = 0; i < challenges.size(); ++i) {
        const LichessChallenge& challenge = challenges.at(i);
        QVariantMap entry;
        entry[QStringLiteral("id")] = challenge.id;
        entry[QStringLiteral("incoming")] = challenge.incoming;
        entry[QStringLiteral("who")] = challenge.incoming ? challenge.challengerName
                                                          : challenge.destName;
        entry[QStringLiteral("rating")] = challenge.challengerRating;
        entry[QStringLiteral("rated")] = challenge.rated;
        entry[QStringLiteral("variant")] = challenge.variant;
        entry[QStringLiteral("speed")] = challenge.speed;
        entry[QStringLiteral("timeControl")] = challenge.timeControl;
        entry[QStringLiteral("playable")] = challenge.boardCompatible
                && challenge.variant == QLatin1String("standard");
        // Always a sentence, never a bare code.
        entry[QStringLiteral("text")] = challenge.incoming
                ? tr("%1 fordert dich heraus: %2, %3.")
                          .arg(challenge.challengerName.isEmpty() ? tr("Jemand")
                                                                  : challenge.challengerName,
                               challenge.timeControl,
                               challenge.rated ? tr("gewertet") : tr("ungewertet"))
                : tr("Du hast %1 herausgefordert: %2, %3.")
                          .arg(challenge.destName.isEmpty() ? tr("jemanden") : challenge.destName,
                               challenge.timeControl,
                               challenge.rated ? tr("gewertet") : tr("ungewertet"));
        out.append(entry);
    }
    return out;
}

QVariantMap TeacherEngine::gameSync() const
{
    QVariantMap out;
    out[QStringLiteral("running")] = m_sync->running();
    out[QStringLiteral("seen")] = m_syncSeen;
    out[QStringLiteral("imported")] = m_syncImported;
    out[QStringLiteral("message")] = m_syncMessage.isEmpty() ? m_sync->message() : m_syncMessage;
    out[QStringLiteral("stored")] = m_database->isOpen()
            ? m_database->gameCountOfSource(QStringLiteral("lichess")) : 0;
    out[QStringLiteral("hasLastGame")] = m_syncedGameId > 0;
    return out;
}

QVariantMap TeacherEngine::onlineGame() const
{
    QVariantMap out;
    const LichessGame& game = m_lichess->game();
    out[QStringLiteral("live")] = liveGame();
    out[QStringLiteral("id")] = game.id;
    out[QStringLiteral("valid")] = game.valid;
    out[QStringLiteral("rated")] = game.rated;
    out[QStringLiteral("speed")] = game.speed;
    out[QStringLiteral("status")] = game.status;
    out[QStringLiteral("finished")] = game.finished();
    out[QStringLiteral("weAreWhite")] = game.weAreWhite;
    out[QStringLiteral("ourTurn")] = game.ourTurn();
    out[QStringLiteral("opponent")] = game.opponent().display();
    out[QStringLiteral("us")] = game.us().display();
    out[QStringLiteral("drawOffered")] = game.opponentOffersDraw();
    out[QStringLiteral("drawOfferedByUs")] = game.weOfferDraw();
    out[QStringLiteral("takebackAsked")] = game.opponentWantsTakeback();
    out[QStringLiteral("opponentGone")] = game.opponentGone;
    out[QStringLiteral("claimWinInSeconds")] = game.claimWinInSeconds;
    out[QStringLiteral("moveCount")] = game.moves.size();
    // §3.5: the first move has a deadline, or the game is aborted.
    out[QStringLiteral("millisToMove")] = static_cast<double>(game.millisToMove);
    out[QStringLiteral("url")] = game.id.isEmpty()
            ? QString()
            : QStringLiteral("https://lichess.org/") + game.id;
    return out;
}

namespace {
// m:ss, and below ten seconds one decimal — the usual chess clock.
QString clockText(qint64 milliseconds)
{
    if (milliseconds < 0)
        milliseconds = 0;
    const qint64 seconds = milliseconds / 1000;
    if (seconds < 10 && milliseconds > 0) {
        return QStringLiteral("%1,%2").arg(seconds).arg((milliseconds % 1000) / 100);
    }
    return QStringLiteral("%1:%2")
            .arg(seconds / 60)
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

QVariantMap TeacherEngine::clocks() const
{
    QVariantMap out;
    const LichessGame& game = m_lichess->game();
    const bool have = game.valid && (game.initialMs > 0 || m_clockWhiteMs > 0 || m_clockBlackMs > 0);
    out[QStringLiteral("visible")] = have;
    out[QStringLiteral("whiteMs")] = static_cast<double>(m_clockWhiteMs);
    out[QStringLiteral("blackMs")] = static_cast<double>(m_clockBlackMs);
    out[QStringLiteral("white")] = clockText(m_clockWhiteMs);
    out[QStringLiteral("black")] = clockText(m_clockBlackMs);
    out[QStringLiteral("ours")] = clockText(game.weAreWhite ? m_clockWhiteMs : m_clockBlackMs);
    out[QStringLiteral("theirs")] = clockText(game.weAreWhite ? m_clockBlackMs : m_clockWhiteMs);
    out[QStringLiteral("running")] = m_clockRunning;
    out[QStringLiteral("whiteToMove")] = m_clockWhiteToMove;
    out[QStringLiteral("lowOnTime")] = (game.weAreWhite ? m_clockWhiteMs : m_clockBlackMs) < 30000;
    return out;
}

void TeacherEngine::onClockTick()
{
    if (!m_clockRunning)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_clockStampMs;
    m_clockStampMs = now;
    if (m_clockWhiteToMove)
        m_clockWhiteMs = qMax<qint64>(0, m_clockWhiteMs - elapsed);
    else
        m_clockBlackMs = qMax<qint64>(0, m_clockBlackMs - elapsed);
    emit clocksChanged();
}

void TeacherEngine::onLichessChanged()
{
    if (m_sync) {
        m_syncSeen = m_sync->seen();
        m_syncImported = m_sync->imported();
    }
    emit lichessChanged();
}

void TeacherEngine::onLichessFailed(const QString& sentence)
{
    m_syncMessage = sentence;
    setFeedback(sentence, QStringLiteral("lichess.error"));
    emit lichessChanged();
}

void TeacherEngine::onOnlineGameStarted(const QString& gameId)
{
    // ---- platform.md §3.7, the moment that matters ----
    // From here until the game is over there is no engine and no tablebase.
    beginLiveGame(gameId);

    setMode(Online);
    m_gameSource = QStringLiteral("lichess");
    m_gameExternalId = gameId;
    m_findings.clear();
    m_currentGameId = -1;
    m_position.reset();
    m_selected = -1;
    syncBoardToOnlineGame();
    setPrompt(tr("Deine Partie läuft. Spiel sie zu Ende — danach sehen wir sie "
                 "uns gemeinsam an."));
    setFeedback(fairPlayNotice(), QStringLiteral("lichess.fairplay"));
    emit positionChanged();
    emit selectionChanged();
    emit boardChanged();
}

void TeacherEngine::syncBoardToOnlineGame()
{
    const LichessGame& game = m_lichess->game();
    if (!game.valid)
        return;
    m_learnerIsWhite = game.weAreWhite;
    m_flipped = !game.weAreWhite;
    m_gameWhiteName = game.white.name.isEmpty() ? tr("Weiß") : game.white.name;
    m_gameBlackName = game.black.name.isEmpty() ? tr("Schwarz") : game.black.name;

    // §3.5, first pitfall: `moves` is the full list every time, never a delta,
    // and after a granted take-back it is shorter than what we have. So the
    // common case plays the difference and everything else rebuilds.
    const std::vector<std::string>& history = m_position.history();
    bool prefixMatches = static_cast<int>(history.size()) <= game.moves.size();
    if (prefixMatches) {
        core::Position probe(game.initialFen.isEmpty()
                                     ? std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
                                     : game.initialFen.toStdString());
        for (std::size_t i = 0; i < history.size(); ++i) {
            const QString expected = normaliseCastling(probe, game.moves.at(static_cast<int>(i)));
            if (QString::fromStdString(history[i]) != expected) {
                prefixMatches = false;
                break;
            }
            probe.play(expected.toStdString());
        }
    }

    if (!prefixMatches) {
        if (game.initialFen.isEmpty())
            m_position.reset();
        else
            m_position.setFen(game.initialFen.toStdString());
        for (int i = 0; i < game.moves.size(); ++i) {
            const QString uci = normaliseCastling(m_position, game.moves.at(i));
            if (!m_position.play(uci.toStdString()))
                break;
        }
    } else {
        for (int i = static_cast<int>(history.size()); i < game.moves.size(); ++i) {
            const QString uci = normaliseCastling(m_position, game.moves.at(i));
            if (!m_position.play(uci.toStdString()))
                break;
        }
    }

    // The clocks, authoritative from the last gameState (§3.5 step 5).
    m_clockWhiteMs = game.whiteMs;
    m_clockBlackMs = game.blackMs;
    m_clockWhiteToMove = (game.moves.size() % 2) == 0;
    m_clockStampMs = QDateTime::currentMSecsSinceEpoch();
    // No clock before the second move: Lichess does not start it either.
    m_clockRunning = game.started() && game.moves.size() >= 2 && game.daysPerTurn == 0;
    if (m_clockRunning && !m_clockTimer->isActive())
        m_clockTimer->start();
    else if (!m_clockRunning && m_clockTimer->isActive())
        m_clockTimer->stop();
}

void TeacherEngine::onOnlineGameUpdated()
{
    if (m_mode != Online)
        return;
    syncBoardToOnlineGame();
    const LichessGame& game = m_lichess->game();
    if (game.opponentOffersDraw()) {
        setFeedback(tr("Dein Gegner bietet Remis an. Du kannst annehmen oder weiterspielen."),
                    QStringLiteral("lichess.draw"));
    } else if (game.opponentWantsTakeback()) {
        setFeedback(tr("Dein Gegner möchte seinen Zug zurücknehmen. Du entscheidest."),
                    QStringLiteral("lichess.takeback"));
    } else if (game.opponentGone && game.claimWinInSeconds >= 0) {
        setFeedback(tr("Dein Gegner ist nicht mehr da. In %n Sekunde(n) kannst du "
                       "den Sieg beanspruchen.", "", game.claimWinInSeconds),
                    QStringLiteral("lichess.gone"));
    }
    emit positionChanged();
    emit onlineGameChanged();
    emit clocksChanged();
}

void TeacherEngine::onOnlineGameFinished(const QString& gameId, const QString& status,
                                         const QString& winner)
{
    Q_UNUSED(status)
    Q_UNUSED(winner)
    if (m_liveGameId != gameId && !m_liveGameId.isEmpty())
        return;
    m_clockRunning = false;
    m_clockTimer->stop();
    syncBoardToOnlineGame();

    // ---- platform.md §3.7, the other moment that matters ----
    // The game is over. The engine may come back, and the analysis of this
    // game is now allowed — that is where the learning happens (GameSync.h).
    endLiveGame();

    setMode(Review);
    setPrompt(m_engine->available()
                      ? tr("Die Partie ist zu Ende. Jetzt darf ich sie mir ansehen — "
                           "soll ich?")
                      : tr("Die Partie ist zu Ende."));
    setFeedback(tr("Die Engine ist wieder an. Während der Partie war sie es nicht, "
                   "und das war kein Versehen."),
                QStringLiteral("lichess.over"));
    emit positionChanged();
    emit clocksChanged();
    emit onlineGameChanged();
    emit engineChanged();
}

// --- what QML calls ----------------------------------------------------------

void TeacherEngine::lichessLogIn() { m_lichess->logIn(); }

void TeacherEngine::lichessLogOut()
{
    m_lichess->logOut();
    if (!m_liveGameId.isEmpty())
        endLiveGame();
}

void TeacherEngine::lichessRefresh()
{
    m_lichess->refreshAccount();
    m_lichess->refreshChallenges();
}

void TeacherEngine::lichessSeek(int minutes, int increment, bool rated)
{
    m_lichess->seek(minutes, increment, rated);
}

void TeacherEngine::lichessSeekCorrespondence(int days, bool rated)
{
    m_lichess->seekCorrespondence(days, rated);
}

void TeacherEngine::lichessCancelSeek() { m_lichess->cancelSeek(); }

void TeacherEngine::lichessChallengeAi(int level, int minutes, int increment)
{
    m_lichess->challengeAi(level, minutes, increment);
}

void TeacherEngine::lichessAcceptChallenge(const QString& id) { m_lichess->acceptChallenge(id); }
void TeacherEngine::lichessDeclineChallenge(const QString& id) { m_lichess->declineChallenge(id); }
void TeacherEngine::lichessOfferDraw() { m_lichess->offerDraw(true); }
void TeacherEngine::lichessAnswerDraw(bool accept) { m_lichess->offerDraw(accept); }
void TeacherEngine::lichessRequestTakeback() { m_lichess->requestTakeback(); }
void TeacherEngine::lichessAnswerTakeback(bool accept) { m_lichess->answerTakeback(accept); }
void TeacherEngine::lichessResign() { m_lichess->resign(); }
void TeacherEngine::lichessAbort() { m_lichess->abortGame(); }
void TeacherEngine::lichessClaimVictory() { m_lichess->claimVictory(); }

void TeacherEngine::lichessSyncGames()
{
    m_syncMessage.clear();
    m_sync->setUsername(m_lichess->account());
    m_sync->start();
    emit lichessChanged();
}

void TeacherEngine::onSyncStored(qint64 databaseId, const QString& initialFen,
                                 const QStringList& moves, bool learnerIsWhite)
{
    // Kept, not analysed on the spot: a hundred games times two engine calls
    // per half move would run for hours. The learner asks for the one he wants
    // to look at, and that is always a game that has already ended.
    m_syncedGameId = databaseId;
    m_syncedInitialFen = initialFen;
    m_syncedMoves = moves;
    m_syncedLearnerIsWhite = learnerIsWhite;
}

void TeacherEngine::onSyncFinished(int imported)
{
    m_syncImported = imported;
    m_syncSeen = m_sync->seen();
    m_syncMessage = m_sync->message();
    setPrompt(imported > 0 && m_syncedGameId > 0
                      ? tr("%n Partie(n) von Lichess geholt. Soll ich die letzte durchsehen?",
                           "", imported)
                      : m_syncMessage);
    emit lichessChanged();
    emit progressChanged();
}

void TeacherEngine::analyseSyncedGame()
{
    // platform.md §3.7 again, from the other side: this game is over — it came
    // out of the archive of finished games — so the engine is allowed to look
    // at it. The refusal below is for the case that a *different* game is
    // running right now.
    if (refusedWhileLive())
        return;
    if (m_syncedGameId <= 0 || m_syncedMoves.isEmpty())
        return;
    if (!m_engine->available()) {
        setFeedback(tr("Ohne Engine kann ich die Partie nicht durchsehen."),
                    QStringLiteral("engine.missing"));
        return;
    }

    if (m_syncedInitialFen.isEmpty())
        m_position.reset();
    else
        m_position.setFen(m_syncedInitialFen.toStdString());
    for (int i = 0; i < m_syncedMoves.size(); ++i) {
        if (!m_position.play(m_syncedMoves.at(i).toStdString()))
            break;
    }
    m_learnerIsWhite = m_syncedLearnerIsWhite;
    m_flipped = !m_learnerIsWhite;
    m_currentGameId = m_syncedGameId;
    m_findings.clear();

    GameInput input;
    input.initialFen = m_syncedInitialFen;
    input.moves = m_syncedMoves;
    input.learnerIsWhite = m_syncedLearnerIsWhite;
    input.learnerElo = learnerElo();
    input.gameId = m_syncedGameId;

    m_analysisDone = 0;
    m_analysisTotal = input.moves.size() / 2;
    setMode(Review);
    setPrompt(tr("Ich schaue mir die Partie an. Das dauert einen Moment."));
    emit positionChanged();
    emit boardChanged();
    m_analyser->start(input);
}

void TeacherEngine::appActivated()
{
    // Coming back from the background mid-game: the process was never killed,
    // but the mobile connection may have dropped both streams while nothing
    // was drawing (§3.5, the watchdog would find it in twenty seconds).
    m_lichess->resume();
    if (!m_liveGameId.isEmpty()) {
        syncBoardToOnlineGame();
        emit positionChanged();
        emit clocksChanged();
    }
    emit lichessChanged();
}

} // namespace schach
