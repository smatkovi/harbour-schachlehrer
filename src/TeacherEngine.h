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
#ifndef SCHACH_TEACHERENGINE_H
#define SCHACH_TEACHERENGINE_H

// The single QObject that QML sees (docs/design.md §4, context property
// `teacher`). Everything else — the Qt-free core, the engine process, the
// database, the analyser, the sparring opponent — hangs off this and is
// invisible from QML, exactly as in harbour-tarock.
//
// The one rule that shapes this whole class: **no evaluation number without a
// sentence** (teacher.md §6.6). `feedback` always carries `text`; `cp` and
// `wp` are optional and exist for diagnostics, not for display, and `key`
// names the rule the sentence came from so the UI can link to the explanation.

#include "Analyser.h"
#include "ItemBank.h"
#include "Database.h"
#include "EngineProcess.h"
#include "Sparring.h"
#include "core/Card.h"
#include "core/Placement.h"
#include "core/Position.h"
#include "core/Routine.h"
#include "core/Skill.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace schach {

class TeacherEngine : public QObject
{
    Q_OBJECT

    // --- board --------------------------------------------------------------
    Q_PROPERTY(QString fen READ fen NOTIFY positionChanged)
    Q_PROPERTY(QVariantList squares READ squares NOTIFY positionChanged)
    Q_PROPERTY(QVariantList legalTargets READ legalTargets NOTIFY selectionChanged)
    Q_PROPERTY(int selectedSquare READ selectedSquare WRITE setSelectedSquare NOTIFY selectionChanged)
    Q_PROPERTY(bool flipped READ flipped WRITE setFlipped NOTIFY boardChanged)
    Q_PROPERTY(QVariantList moveList READ moveList NOTIFY positionChanged)
    Q_PROPERTY(QString lastMove READ lastMove NOTIFY positionChanged)
    Q_PROPERTY(bool whiteToMove READ whiteToMove NOTIFY positionChanged)
    Q_PROPERTY(QString gameResult READ gameResult NOTIFY positionChanged)

    // --- mode ---------------------------------------------------------------
    Q_PROPERTY(int mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString prompt READ prompt NOTIFY taskChanged)
    Q_PROPERTY(QVariantMap task READ task NOTIFY taskChanged)
    Q_PROPERTY(QVariantMap feedback READ feedback NOTIFY feedbackChanged)
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineChanged)
    Q_PROPERTY(bool thinking READ thinking NOTIFY engineChanged)

    // --- Was frage ich mich? (teacher.md §6.6, §7.5) -------------------------
    // The thinking routine: the ordered list of questions, personalised from
    // the learner's own error record, and the blunder-check drill that trains
    // the two of them he needs most. `routine` is what the panel on the board
    // page shows; `blunderCheck` is empty unless the drill is holding a move.
    Q_PROPERTY(QVariantList routine READ routine NOTIFY routineChanged)
    Q_PROPERTY(QVariantMap blunderCheck READ blunderCheck NOTIFY blunderCheckChanged)
    Q_PROPERTY(int drillMode READ drillMode WRITE setDrillMode NOTIFY drillModeChanged)

    // --- progress -----------------------------------------------------------
    Q_PROPERTY(QVariantList skills READ skills NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap session READ session NOTIFY progressChanged)
    Q_PROPERTY(int dueCards READ dueCards NOTIFY progressChanged)

    // --- Lösungen durchsehen ------------------------------------------------
    // A measurement you cannot look back at teaches nothing. Every answered
    // item is kept, and the board can step back through them (teacher.md §4.1
    // allows the solution, only the running right/wrong tally is withheld).
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewChanged)
    Q_PROPERTY(int reviewIndex READ reviewIndex NOTIFY reviewChanged)
    Q_PROPERTY(int reviewCount READ reviewCount NOTIFY reviewChanged)
    Q_PROPERTY(QVariantMap review READ review NOTIFY reviewChanged)

public:
    enum Mode { Idle = 0, Placement = 1, Drill = 2, Sparring = 3, Review = 4 };
    Q_ENUMS(Mode)

    // teacher.md §7.5 defaults to Auto; the learner may force it either way.
    enum DrillSetting { DrillAuto = 0, DrillAlways = 1, DrillNever = 2 };
    Q_ENUMS(DrillSetting)

    explicit TeacherEngine(QObject* parent = 0);
    ~TeacherEngine();

    // Called once from main() before QML is loaded; both may be empty, and the
    // app then runs without engine and with an in-memory database.
    void setPaths(const QString& enginePath, const QString& syzygyPath,
                  const QString& evalFile, const QString& databasePath);
    // The placement items (teacher.md §4.2); without them the test says so
    // instead of asking about the starting position.
    void setItemBankPath(const QString& path);

    QString fen() const;
    QVariantList squares() const;
    QVariantList legalTargets() const;
    int selectedSquare() const { return m_selected; }
    bool reviewing() const { return m_reviewIndex >= 0; }
    int reviewIndex() const { return m_reviewIndex; }
    int reviewCount() const { return m_answered.size(); }
    QVariantMap review() const;
    void setSelectedSquare(int square);
    bool flipped() const { return m_flipped; }
    void setFlipped(bool flipped);
    QVariantList moveList() const;
    QString lastMove() const;
    bool whiteToMove() const;
    QString gameResult() const;

    int mode() const { return m_mode; }
    QString prompt() const { return m_prompt; }
    QVariantMap task() const { return m_task; }
    QVariantMap feedback() const { return m_feedback; }
    bool engineReady() const;
    bool thinking() const;

    QVariantList routine() const;
    QVariantMap blunderCheck() const { return m_blunderCheck; }
    int drillMode() const;
    void setDrillMode(int mode);

    QVariantList skills() const;
    QVariantMap session() const;
    int dueCards() const;

    Q_INVOKABLE void startPlacement();
    Q_INVOKABLE void startSession();
    Q_INVOKABLE void startSparring(int handicap);
    Q_INVOKABLE bool play(int fromSquare, int toSquare, const QString& promotion = QString());
    Q_INVOKABLE void takeBack();
    Q_INVOKABLE void requestHint();
    Q_INVOKABLE void skipTask();
    Q_INVOKABLE void analyseCurrentGame();
    Q_INVOKABLE QVariantList lastFindings() const;

    // The drill (§7.5). `dangerous` holds the indices into
    // blunderCheck["moves"] that the learner marked; the held move is released
    // afterwards either way — the drill teaches, it does not punish.
    Q_INVOKABLE void answerBlunderCheck(const QVariantList& dangerous);
    // Let the held move go without answering (the learner may always play).
    Q_INVOKABLE void skipBlunderCheck();

    // Step through the answered items. reviewPrevious() from the live test
    // enters the review at the item just answered; endReview() returns to it.
    Q_INVOKABLE void reviewPrevious();
    Q_INVOKABLE void reviewNext();
    Q_INVOKABLE void reviewItem(int index);
    Q_INVOKABLE void endReview();

signals:
    void positionChanged();
    void selectionChanged();
    void boardChanged();
    void modeChanged();
    void taskChanged();
    void feedbackChanged();
    void engineChanged();
    void progressChanged();
    void reviewChanged();
    void routineChanged();
    void blunderCheckChanged();
    void drillModeChanged();

private slots:
    void onEngineResult(const schach::EngineResult& result);
    void onEngineFailed(const QString& reason);
    void onAnalysisFinished(const QVector<core::Finding>& findings);
    void onAnalysisProgress(int done, int total);
    void onChanceMissed(const QString& fen, int errorClass);
    void onOpponentTurn();

private:
    void setMode(Mode mode);
    void setPrompt(const QString& prompt);
    // `cls` adds the link back of §6.6: which question of the routine would
    // have caught this. ErrorClass::None leaves the field empty.
    void setFeedback(const QString& text, const QString& key, const core::Score* score = 0,
                     core::ErrorClass cls = core::ErrorClass::None);
    // The learner's error record, read from the database (§3.2 b window).
    void reloadHistory();
    core::RoutineView currentRoutine() const;
    // §7.5: hold the move, ask, release.
    bool beginBlunderCheck(const QString& uci);
    void finishBlunderCheck(bool correct);
    void playInSparring(const std::string& uci);
    void clearFeedback();
    void startNewGame(bool learnerPlaysWhite);
    void askOpponent();
    void loadNextTask();
    void finishDrillTask(bool correct, int milliseconds);
    void presentCard(const core::Card& card);
    qint64 today() const;
    int learnerElo() const;

    core::Position m_position;
    EngineProcess* m_engine;
    Database* m_database;
    Analyser* m_analyser;
    // Qualified: the enumerator `Sparring` above would otherwise shadow the class.
    ::schach::Sparring* m_sparring;
    QTimer* m_opponentTimer;
    QElapsedTimer m_taskClock;

    core::Placement* m_placement;
    ItemBank m_items;

    // One answered placement item, kept so the solution can be looked at.
    struct AnsweredItem {
        QString itemId;
        QString fen;          // the position as it was asked
        QString solution;     // UCI
        QString solutionSan;
        QString played;       // what the learner did, empty when skipped
        QString playedSan;
        QString explanation;
        bool correct;
        int number;           // 1-based position in the test
        double difficulty;
    };
    QVector<AnsweredItem> m_answered;
    int m_reviewIndex;        // -1 while the test is running
    QString m_taskFen;        // the position of the running task, for the record
    QString m_lastAnswer;     // the move just played, empty when skipped
    // What was on the board when the review was entered, so leaving it again
    // returns to the same task instead of burning a fresh one.
    QString m_liveFen;
    QString m_liveSolution;
    QString m_livePrompt;
    QVariantMap m_liveTask;
    bool m_liveSaved;

    void rememberAnswer(bool correct);
    void showAnswered(int index);
    QStringList m_usedItems;   // one position is never asked twice in a run
    core::SkillState m_skill;
    core::SessionPlan m_sessionPlan;
    QVector<core::Card> m_sessionCards;
    int m_sessionIndex;
    int m_analysisDone;
    int m_analysisTotal;

    QVector<core::Finding> m_findings;
    QString m_solutionUci;
    QString m_currentCardId;
    int m_hintLevel;
    bool m_learnerIsWhite;
    bool m_flipped;
    int m_selected;
    Mode m_mode;
    QString m_prompt;
    QVariantMap m_task;
    QVariantMap m_feedback;
    // §7.5: the move the drill is holding, and what it asked about.
    QString m_heldMove;
    QVariantMap m_blunderCheck;
    QVector<schach::BlunderCheckItem> m_checkItems;
    core::ErrorHistory m_history;
    QString m_engineMessage;
    qint64 m_currentGameId;
};

} // namespace schach

#endif // SCHACH_TEACHERENGINE_H
