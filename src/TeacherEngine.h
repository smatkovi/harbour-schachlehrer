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
#include "GameSync.h"
#include "Lichess.h"
#include "PuzzleFeed.h"
#include "core/SolutionLine.h"
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
    // Why it is not running, in the engine's own words where there are any.
    // Without this on screen nobody can tell a missing file from a refused
    // exec from a network it could not load.
    Q_PROPERTY(QString engineMessage READ engineMessage NOTIFY engineChanged)
    Q_PROPERTY(QString enginePath READ enginePath NOTIFY engineChanged)
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
    // Whether a placement test was ever finished, and when. Without this the
    // start page cannot tell "never measured" from "measured, nothing stood out".
    Q_PROPERTY(bool measured READ measured NOTIFY progressChanged)
    Q_PROPERTY(QString measuredOn READ measuredOn NOTIFY progressChanged)
    Q_PROPERTY(int startDifficulty READ startDifficulty NOTIFY progressChanged)

    // --- Lichess (platform.md §3, docs/design.md §8 M8) ----------------------
    // Everything the online screens need. The app is fully usable with no
    // account at all — placement test, routine, blunder-check drill and
    // offline sparring do not touch any of this — and that is deliberate: an
    // app that needs a login to be useful has missed its own purpose.
    Q_PROPERTY(int lichessState READ lichessState NOTIFY lichessChanged)
    Q_PROPERTY(bool lichessLoggedIn READ lichessLoggedIn NOTIFY lichessChanged)
    Q_PROPERTY(QString lichessAccount READ lichessAccount NOTIFY lichessChanged)
    Q_PROPERTY(QString lichessMessage READ lichessMessage NOTIFY lichessChanged)
    Q_PROPERTY(QString lichessAuthUrl READ lichessAuthUrl NOTIFY lichessChanged)
    // Where the access key is kept, and whether that place is encrypted. The
    // settings page shows both: an app that keeps a key in the weaker of two
    // places must not let the user believe it is in the stronger one.
    // --- Nachschub an Aufgaben (teacher.md §5.2) ----------------------------
    // How many exercises were fetched from Lichess, how many are wanted, and
    // whether the learner has allowed it at all. Off until they say so: the
    // app is complete on what it ships (§0.2).
    Q_PROPERTY(bool feedAllowed READ feedAllowed WRITE setFeedAllowed NOTIFY feedChanged)
    Q_PROPERTY(int feedCount READ feedCount NOTIFY feedChanged)
    Q_PROPERTY(int feedTarget READ feedTarget WRITE setFeedTarget NOTIFY feedChanged)
    Q_PROPERTY(bool feedBusy READ feedBusy NOTIFY feedChanged)
    Q_PROPERTY(QString feedMessage READ feedMessage NOTIFY feedChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY feedChanged)

    Q_PROPERTY(QString lichessKeyStore READ lichessKeyStore NOTIFY lichessChanged)
    Q_PROPERTY(bool lichessKeyEncrypted READ lichessKeyEncrypted NOTIFY lichessChanged)
    Q_PROPERTY(bool lichessConnected READ lichessConnected NOTIFY lichessChanged)
    Q_PROPERTY(bool lichessSeeking READ lichessSeeking NOTIFY lichessChanged)
    Q_PROPERTY(QVariantList lichessChallenges READ lichessChallenges NOTIFY lichessChanged)
    Q_PROPERTY(QVariantMap gameSync READ gameSync NOTIFY lichessChanged)

    // --- the live Lichess game and the fair-play lock (platform.md §3.7) -----
    // `liveGame` is true exactly while `m_liveGameId` is set. While it is,
    // there is no engine: the process is terminated, every analysis request is
    // refused and `analysisAvailable` is false, which is what the pages read
    // to **hide** the analysis entry rather than grey it out.
    Q_PROPERTY(QVariantMap onlineGame READ onlineGame NOTIFY onlineGameChanged)
    Q_PROPERTY(QVariantMap clocks READ clocks NOTIFY clocksChanged)
    Q_PROPERTY(bool liveGame READ liveGame NOTIFY onlineGameChanged)
    Q_PROPERTY(QString liveGameId READ liveGameId NOTIFY onlineGameChanged)
    Q_PROPERTY(bool analysisAvailable READ analysisAvailable NOTIFY engineChanged)
    Q_PROPERTY(QString fairPlayNotice READ fairPlayNotice CONSTANT)
    Q_PROPERTY(bool canAnalyseFinishedGame READ canAnalyseFinishedGame NOTIFY onlineGameChanged)

    // --- Lösungen durchsehen ------------------------------------------------
    // A measurement you cannot look back at teaches nothing. Every answered
    // item is kept, and the board can step back through them (teacher.md §4.1
    // allows the solution, only the running right/wrong tally is withheld).
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewChanged)
    Q_PROPERTY(int reviewIndex READ reviewIndex NOTIFY reviewChanged)
    Q_PROPERTY(int reviewCount READ reviewCount NOTIFY reviewChanged)
    Q_PROPERTY(QVariantMap review READ review NOTIFY reviewChanged)
    // Stepping through the solution on the board (teacher.md §6.5: the app
    // plays the line back; §6.6 hint level 4: the full solution with a
    // sentence). Empty `active` means nothing is being shown.
    Q_PROPERTY(QVariantMap solutionView READ solutionView NOTIFY reviewChanged)
    // Eine falsch beantwortete Uebungsaufgabe bleibt stehen, bis der Lernende
    // weitergeht. Solange das so ist, meint "Loesung ansehen" **diese**
    // Aufgabe und nicht die naechste, und das Brett nimmt keinen Zug mehr an.
    Q_PROPERTY(bool awaitingNext READ awaitingNext NOTIFY taskChanged)
    // Der wievielte Versuch gerade laeuft: 1 der erste, 2 der zweite. Ein
    // falscher erster Zug beendet eine Uebungsaufgabe nicht mehr sofort --
    // wer den Fehler gerade gesehen hat, ist genau dann in der Lage, es
    // richtig zu machen, und erst der zweite Fehlversuch wertet die Aufgabe.
    // 0 heisst: keine offene Aufgabe.
    Q_PROPERTY(int attempt READ attempt NOTIFY taskChanged)
    // Ein freiwilliger Durchgang nach der Wertung ("Nochmal"). Er zaehlt
    // nicht mehr: die Karte ist bewertet, hier wird nur noch geuebt.
    Q_PROPERTY(bool retrying READ retrying NOTIFY taskChanged)

    // --- die unterbrochene Partie -------------------------------------------
    // Eine Sparringpartie ueberlebt das Schliessen der App: sie wird nach
    // jedem Zug weggeschrieben und kann beim naechsten Start fortgesetzt
    // werden.
    Q_PROPERTY(bool canResume READ canResume NOTIFY progressChanged)

public:
    enum Mode { Idle = 0, Placement = 1, Drill = 2, Sparring = 3, Review = 4, Online = 5 };
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
    // A second bank on top of the first (ItemBank::merge). The hand-written
    // items and the ones imported from Lichess (tools/import_lichess_puzzles.py)
    // are two files, and a missing second one is a normal state.
    void addItemBankPath(const QString& path);
    // assets/items/themes.json, which PuzzleFeed needs to give a fetched
    // puzzle a dimension. Sets the feed going if the learner has allowed it.
    void setThemesPath(const QString& path);

    QString fen() const;
    QVariantList squares() const;
    QVariantList legalTargets() const;
    int selectedSquare() const { return m_selected; }
    bool reviewing() const { return m_reviewIndex >= 0; }
    int reviewIndex() const { return m_reviewIndex; }
    int reviewCount() const { return m_answered.size(); }
    QVariantMap review() const;
    QVariantMap solutionView() const;
    bool awaitingNext() const { return m_awaitingNext; }
    int attempt() const { return m_task.isEmpty() ? 0 : m_attempt; }
    bool retrying() const { return m_freeRetry; }
    bool canResume() const { return !m_savedMoves.trimmed().isEmpty(); }
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
    QString engineMessage() const { return m_engineMessage; }
    QString enginePath() const;
    bool thinking() const;

    QVariantList routine() const;
    QVariantMap blunderCheck() const { return m_blunderCheck; }
    int drillMode() const;
    void setDrillMode(int mode);

    QVariantList skills() const;
    QVariantMap session() const;
    int dueCards() const;
    bool measured() const { return m_measuredAt > 0; }
    QString measuredOn() const;
    int startDifficulty() const;

    int lichessState() const;
    bool lichessLoggedIn() const;
    QString lichessAccount() const;
    QString lichessMessage() const;
    QString lichessAuthUrl() const;
    QString lichessKeyStore() const;
    bool lichessKeyEncrypted() const;

    bool feedAllowed() const;
    void setFeedAllowed(bool allowed);
    int feedCount() const;
    int feedTarget() const;
    void setFeedTarget(int items);
    bool feedBusy() const;
    QString feedMessage() const;
    int itemCount() const { return m_items.count(); }
    bool lichessConnected() const;
    bool lichessSeeking() const;
    QVariantList lichessChallenges() const;
    QVariantMap gameSync() const;
    QVariantMap onlineGame() const;
    QVariantMap clocks() const;
    // The one instance variable of platform.md §3.7. Everything else about the
    // fair-play lock is derived from it.
    bool liveGame() const { return !m_liveGameId.isEmpty(); }
    QString liveGameId() const { return m_liveGameId; }
    bool analysisAvailable() const;
    QString fairPlayNotice() const;
    bool canAnalyseFinishedGame() const;

    // The client, for the regression tests. tests/test_fairplay.cpp feeds
    // recorded ndjson through it to make a game start and end without a
    // network and without an account.
    Lichess* lichess() const { return m_lichess; }
    EngineProcess* engine() const { return m_engine; }

    Q_INVOKABLE void startPlacement();
    // Take the last entered move of a line back (teacher.md §7.6: taking back
    // is free). Composing a line is not answering; a slip of the finger must
    // not count as a wrong answer.
    Q_INVOKABLE bool undoEntry();
    Q_INVOKABLE void startSession();
    Q_INVOKABLE void startSparring(int handicap);
    // Die gespeicherte Partie wieder aufs Brett holen, und sie vergessen.
    Q_INVOKABLE void resumeGame();
    Q_INVOKABLE void discardSavedGame();
    // Nach einer falsch beantworteten Aufgabe zur naechsten weitergehen.
    Q_INVOKABLE void continueDrill();
    // Dieselbe Aufgabe noch einmal stellen, nachdem sie gewertet ist. Der
    // Durchgang aendert an der Karte nichts mehr -- weder Termin noch Zaehler
    // --, sonst wuerde dieselbe Aufgabe mehrfach in die Statistik laufen.
    Q_INVOKABLE void retryTask();
    Q_INVOKABLE bool play(int fromSquare, int toSquare, const QString& promotion = QString());
    Q_INVOKABLE void takeBack();
    Q_INVOKABLE void requestHint();
    Q_INVOKABLE void skipTask();
    Q_INVOKABLE void analyseCurrentGame();
    Q_INVOKABLE QVariantList lastFindings() const;

    // --- Lichess -------------------------------------------------------------
    Q_INVOKABLE void lichessLogIn();
    Q_INVOKABLE void lichessLogOut();
    Q_INVOKABLE void lichessCancelLogIn();
    Q_INVOKABLE void lichessRefresh();
    // §3.2: the seek only reaches rapid, classical and correspondence. Blitz
    // would need a direct challenge and is the wrong format for learning.
    Q_INVOKABLE void lichessSeek(int minutes, int increment, bool rated);
    Q_INVOKABLE void lichessSeekCorrespondence(int days, bool rated);
    Q_INVOKABLE void lichessCancelSeek();
    Q_INVOKABLE void lichessChallengeAi(int level, int minutes, int increment);
    Q_INVOKABLE void lichessAcceptChallenge(const QString& id);
    Q_INVOKABLE void lichessDeclineChallenge(const QString& id);
    Q_INVOKABLE void lichessOfferDraw();
    Q_INVOKABLE void lichessAnswerDraw(bool accept);
    Q_INVOKABLE void lichessRequestTakeback();
    Q_INVOKABLE void lichessAnswerTakeback(bool accept);
    Q_INVOKABLE void lichessResign();
    Q_INVOKABLE void lichessAbort();
    Q_INVOKABLE void lichessClaimVictory();
    Q_INVOKABLE void lichessSyncGames();
    // Fetch now, rather than waiting for the next start.
    Q_INVOKABLE void fetchPuzzles();
    // Throw away everything that was fetched; the shipped bank stays.
    Q_INVOKABLE void clearFetchedPuzzles();
    // The game the sync brought in last, analysed **after** it ended — which
    // is the allowed and the whole point (platform.md §3.7, GameSync.h).
    Q_INVOKABLE void analyseSyncedGame();
    // Called from QML when Qt.application.state goes back to active: the
    // streams may have died while the app was in the background.
    Q_INVOKABLE void appActivated();

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

    // --- die Lösung ansehen ---------------------------------------------
    // Looking at the solution of a task that is still open **ends it, unsolved**
    // — there is nothing left to produce once it has been seen, and §4.1 will
    // not have the measurement made on an item the learner was shown. The
    // button says so before it does it.
    Q_INVOKABLE void showSolution();
    Q_INVOKABLE void solutionForward();
    Q_INVOKABLE void solutionBack();
    Q_INVOKABLE void hideSolution();
    // Leave the online state no matter what the client thinks: clears the live
    // game, unlocks the engine and goes back to Idle. The way out of a half
    // finished conversation with the server, and reachable from the UI.
    Q_INVOKABLE void leaveOnline();

    // Test hook: the move the current task is graded against.
    QString solutionForTest() const { return m_solutionUci; }
    // The whole line of the running task, UCI, space-separated. The tests play
    // through multi-move items with it; nothing in the app reads it.
    QString solutionLineForTest() const;
    // The item bank draws at random now (ItemBank::pick), which is the point —
    // but a test that cannot repeat a draw cannot check anything.
    void seedItemBankForTest(unsigned int seed) { m_items.setSeed(seed); }

public slots:
    // Den Stand wegschreiben. Wird nach jedem Zug gerufen und zusaetzlich,
    // wenn die Anwendung zumacht -- ein Programm, das abgeschossen wird,
    // bekommt kein aboutToQuit mehr zu sehen.
    //
    // Ein echter Schlitz und kein Q_INVOKABLE: die MeeGo-Fassung haengt ihn
    // unter Qt 4 mit der Zeichenketten-Schreibweise an aboutToQuit, und
    // SLOT() findet nur Schlitze.
    void saveState();

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
    void lichessChanged();
    void feedChanged();
    void onlineGameChanged();
    void clocksChanged();

private slots:
    void onEngineResult(const schach::EngineResult& result);
    void onEngineFailed(const QString& reason);
    void onAnalysisFinished(const QVector<schach::core::Finding>& findings);
    void onAnalysisProgress(int done, int total);
    void onChanceMissed(const QString& fen, int errorClass);
    void onOpponentTurn();
    void onLichessChanged();
    void onLichessFailed(const QString& sentence);
    void onOnlineGameStarted(const QString& gameId);
    void onOnlineGameUpdated();
    void onOnlineGameFinished(const QString& gameId, const QString& status, const QString& winner);
    void onClockTick();
    void onSyncStored(qint64 databaseId, const QString& initialFen,
                      const QStringList& moves, bool learnerIsWhite);
    void onSyncFinished(int imported);

private:
    void setMode(Mode mode);
    // platform.md §3.7, the only two places `m_liveGameId` ever changes.
    // beginLiveGame() terminates the engine process — not pauses it — and
    // endLiveGame() brings it back once the game is over.
    void beginLiveGame(const QString& gameId);
    void endLiveGame();
    // True when a request into the engine or the tablebase has to be refused,
    // and it says so in a sentence instead of doing nothing.
    bool refusedWhileLive();
    void syncBoardToOnlineGame();
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
    // New exercises from Lichess, kept for offline use, and merged into the
    // bank above. Optional and additive, never a requirement (§0.2). Built in
    // setPaths(), because it needs the data directory.
    PuzzleFeed* m_feed;
    QString m_feedDirectory;

    // One answered placement item, kept so the solution can be looked at.
    struct AnsweredItem {
        QString itemId;
        QString cardId;       // set for a drill task, empty for a placement item
        QString fen;          // the position as it was asked
        QString solution;     // UCI, the first move of the line
        QString solutionLine; // the whole line, UCI, space-separated
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
    qint64 m_measuredAt;      // when the placement test last finished, 0 = never
    QString m_taskFen;        // the position of the running task, for the record
    QString m_lastAnswer;     // the move just played, empty when skipped
    // What was on the board when the review was entered, so leaving it again
    // returns to the same task instead of burning a fresh one.
    QString m_liveFen;
    QString m_liveSolution;
    QString m_livePrompt;
    QVariantMap m_liveTask;
    // The orientation too: the review item may be the other colour's move, and
    // coming back to a board that is upside down puts the learner's pieces at
    // the wrong end.
    bool m_liveFlipped = false;
    bool m_liveSaved;

    void rememberAnswer(bool correct);
    void showAnswered(int index);
    // Put `index` on the board at ply `step` of its line and tell the UI.
    void playbackTo(int step);

    // What is being stepped through. -1 in `m_showStep` means nothing is.
    QStringList m_showLine;
    int m_showStep = -1;
    QVector<core::Card> starterCards(core::Dimension dimension) const;
    // The answer being composed (teacher.md §6.5). A one-move task is a line
    // of length one, so there is only one code path for both.
    core::SolutionLine m_line;
    // The board the learner sees is replayed from the line after every entry,
    // and the task map carries how many moves are still to come.
    void syncBoardToLine();
    void updateLineProgress();
    static QString lineSan(const QString& fen, const QStringList& line);

    QStringList m_usedItems;   // one position is never asked twice in a run
    QStringList m_seenItems;   // and, if it can be helped, never twice at all
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
    // Die Aufgabe ist beantwortet und falsch, die naechste ist noch nicht
    // geladen. Frueher lud finishDrillTask() sofort nach, und "Loesung
    // ansehen" traf dadurch die frische Aufgabe statt der eben verlorenen.
    bool m_awaitingNext;
    // Der laufende Versuch (1 oder 2) und der Modus, mit dem die Linie
    // gestellt wurde -- der zweite Versuch muss dieselbe Aufgabe unter
    // denselben Bedingungen sein, also auch mit demselben Linienmodus.
    int m_attempt;
    core::SolutionLine::Mode m_lineMode;
    // Ein Durchgang nach der Wertung: die Aufgabe steht wieder offen, aber
    // ihr Ergebnis ist schon verbucht.
    bool m_freeRetry;
    // Die Aufgabe noch einmal von ihrer Anfangsstellung her stellen.
    void restartTaskLine();
    // Die Partie wird gerade wiederhergestellt: startSparring() darf den
    // Gegner dann nicht schon ziehen lassen, die Zuege kommen erst noch.
    bool m_resuming;
    // Womit das Sparring begonnen wurde, damit es genauso fortgesetzt wird.
    int m_handicap;
    // Der gespeicherte Stand, beim Start einmal gelesen.
    QString m_savedMoves;
    int m_savedHandicap;
    bool m_savedLearnerIsWhite;
    bool m_savedFlipped;
    void loadSavedGame();
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
    QString m_enginePath;
    qint64 m_currentGameId;

    // --- Lichess -------------------------------------------------------------
    Lichess* m_lichess;
    GameSync* m_sync;
    QTimer* m_clockTimer;
    // **The** instance variable of platform.md §3.7. Set while a Lichess game
    // is running, empty otherwise. Nothing else decides whether the engine may
    // run.
    QString m_liveGameId;
    qint64 m_clockWhiteMs;
    qint64 m_clockBlackMs;
    qint64 m_clockStampMs;
    bool m_clockRunning;
    bool m_clockWhiteToMove;
    // Where the game on the board came from, so that the record it is stored
    // under is the truth and not "Trainingsgegner" for every game.
    QString m_gameSource;
    QString m_gameExternalId;
    QString m_gameWhiteName;
    QString m_gameBlackName;
    // The last game the download brought in (GameSync), kept so it can be
    // analysed after the fact.
    qint64 m_syncedGameId;
    QString m_syncedInitialFen;
    QStringList m_syncedMoves;
    bool m_syncedLearnerIsWhite;
    int m_syncSeen;
    int m_syncImported;
    QString m_syncMessage;
};

} // namespace schach

#endif // SCHACH_TEACHERENGINE_H
