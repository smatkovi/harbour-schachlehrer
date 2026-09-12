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
#ifndef SCHACH_LICHESS_H
#define SCHACH_LICHESS_H

// The Lichess client (chess-spec/platform.md §3, docs/design.md §8 M8).
//
// **Board API, never the Bot API.** The Board API works with normal Lichess
// accounts and forbids engine assistance; the Bot API allows engines and needs
// a bot account, and the upgrade to one is irreversible. `POST
// /api/bot/account/upgrade` does not appear anywhere in this source tree and
// must not be added (§3.1, Empfehlung 3).
//
// Four rules from §3.3–§3.5 shape this class and are worth stating once:
//
//   * **OAuth 2.0 PKCE without a server.** No client registration, any unique
//     client id, `S256` only, no refresh tokens, the token lives about a year.
//     The redirect is caught by a QTcpServer on 127.0.0.1 (RFC 8252 §7.3) and
//     the authorisation page opens in the *system* browser, never in an
//     embedded web view (RFC 8252 §8.12).
//   * **One request at a time**, 60 s of backoff on HTTP 429 (§3.4). Every
//     plain request goes through one queue; only the long-lived streams run
//     beside it.
//   * **One single event stream** per token — a second one closes the first.
//   * **The streams never end by themselves.** They are read incrementally,
//     empty lines are keep-alives, and a watchdog without any byte for 20 s
//     tears the connection down and reconnects with exponential backoff. No
//     reconnect loop without backoff (§3.4: `eventStream = 30/10 minutes`).
//
// The token is written to a file with 0600 under the app's own data directory,
// never into QSettings (§3.3), and logging out deletes it and revokes it.
//
// What this class deliberately does not do: decide anything about the board.
// It parses, it sends, it emits. TeacherEngine owns the position, and
// TeacherEngine is what holds `m_liveGameId` and kills the engine (§3.7).

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTimer;

namespace schach {

namespace core { class Position; }

// ---------------------------------------------------------------------------
// The parsing half: no network, no files, no clock. This is what the recorded
// ndjson test drives, and the reason the protocol can be checked at all
// without a Lichess account.
// ---------------------------------------------------------------------------

// Splits an ndjson byte stream into objects. Empty lines are keep-alives and
// are counted, not delivered (§3.5). A line that grows past a megabyte without
// a newline is rubbish and sets overflowed(); the caller then aborts the
// reply instead of eating memory.
class NdjsonSplitter
{
public:
    NdjsonSplitter();

    QVector<QJsonObject> feed(const QByteArray& chunk);
    void reset();

    int keepAlives() const { return m_keepAlives; }
    int malformed() const { return m_malformed; }
    bool overflowed() const { return m_overflow; }
    int pending() const { return m_buffer.size(); }

private:
    QByteArray m_buffer;
    int m_keepAlives;
    int m_malformed;
    bool m_overflow;
};

struct LichessPlayer {
    QString id;
    QString name;
    QString title;
    int rating = 0;
    bool provisional = false;
    bool ai = false;
    int aiLevel = 0;

    QString display() const;
};

// One game as the two streams describe it. `gameFull` fills the immutable
// half once, `gameState` replaces the mutable half on every move, draw offer
// and at the end (§3.5).
struct LichessGame {
    QString id;
    bool valid = false;
    bool weAreWhite = true;
    bool rated = false;
    QString variant = QStringLiteral("standard");
    QString speed;
    QString initialFen;            // empty or "startpos" = the start position
    LichessPlayer white, black;

    QStringList moves;             // UCI, always the full list from the start
    QString status = QStringLiteral("created");
    QString winner;                // "white", "black" or empty

    qint64 initialMs = 0, incrementMs = 0;
    qint64 whiteMs = 0, blackMs = 0;
    qint64 whiteIncMs = 0, blackIncMs = 0;
    qint64 daysPerTurn = 0;        // correspondence, 0 otherwise

    bool whiteOffersDraw = false, blackOffersDraw = false;
    bool whiteWantsTakeback = false, blackWantsTakeback = false;

    bool opponentGone = false;
    int claimWinInSeconds = -1;
    qint64 millisToMove = -1;      // §3.5: the first move has a deadline

    // Everything but "created" and "started" is a finished game — and a
    // finished game is a normal end, not an error (§3.5 step 6).
    bool finished() const;
    bool started() const { return status == QLatin1String("started"); }
    bool ourTurn() const;
    bool opponentOffersDraw() const { return weAreWhite ? blackOffersDraw : whiteOffersDraw; }
    bool weOfferDraw() const { return weAreWhite ? whiteOffersDraw : blackOffersDraw; }
    bool opponentWantsTakeback() const { return weAreWhite ? blackWantsTakeback : whiteWantsTakeback; }
    const LichessPlayer& opponent() const { return weAreWhite ? black : white; }
    const LichessPlayer& us() const { return weAreWhite ? white : black; }
    qint64 ourClockMs() const { return weAreWhite ? whiteMs : blackMs; }
    qint64 theirClockMs() const { return weAreWhite ? blackMs : whiteMs; }
};

struct LichessChallenge {
    QString id;
    QString url;
    QString status;
    QString challengerName;
    QString destName;
    int challengerRating = 0;
    bool rated = false;
    QString variant = QStringLiteral("standard");
    QString speed;
    QString timeControl;           // already a sentence fragment, e.g. "10+5"
    QString colour;
    bool incoming = true;
    // §3.1: a challenge whose `compat.board` is false cannot be played with
    // the Board API at all, and offering it would only produce an error.
    bool boardCompatible = true;
};

// "e2e4 e7e5 g1f3" -> three entries. Tolerates the empty string.
QStringList splitMoves(const QString& moves);

// The game stream writes castling king-takes-rook (`e1h1`) so that the
// notation also works for Chess960 (§3.5). Our board wants `e1g1` for standard
// chess, so a king move onto our own rook is translated back. Everything else
// is passed through untouched.
QString normaliseCastling(const core::Position& position, const QString& uci);

bool parseGameFull(const QJsonObject& line, LichessGame& game, const QString& ourId);
bool parseGameState(const QJsonObject& line, LichessGame& game);
bool parseOpponentGone(const QJsonObject& line, LichessGame& game);
LichessChallenge parseChallenge(const QJsonObject& challenge);

// ---------------------------------------------------------------------------

class Lichess : public QObject
{
    Q_OBJECT

public:
    enum State { LoggedOut = 0, Authorising = 1, LoggedIn = 2 };

    explicit Lichess(QObject* parent = 0);
    ~Lichess();

    // Where the token file goes. Under Sailjail this is
    // ~/.local/share/org.smatkovi/harbour-schachlehrer (platform.md §5.3).
    void setDataDirectory(const QString& path);
    QString tokenPath() const;

    // https://lichess.org by default. Configurable so that the tests can point
    // the client at an address that answers instantly and never leaves the
    // machine — no test may ever talk to the real server.
    void setEndpoint(const QString& baseUrl);
    QString endpoint() const { return m_endpoint; }

    State state() const { return m_state; }
    bool hasToken() const { return !m_token.isEmpty(); }
    // For the game download, which is a separate, long-running stream of its
    // own (GameSync). Empty when nobody is logged in — §3.6 works without a
    // token as well, a token only raises the throughput from 20 to 60 games
    // per second.
    QByteArray bearer() const;
    QString account() const { return m_account; }
    // Always a full German sentence, never a status code.
    QString message() const { return m_message; }
    // Opened by the UI with Qt.openUrlExternally() — the system browser, so
    // that the user can see the address bar (RFC 8252 §8.12).
    QString authorizationUrl() const { return m_authUrl; }

    bool eventStreamOpen() const;
    bool seeking() const { return m_seekReply != 0; }
    const LichessGame& game() const { return m_game; }
    QVector<LichessChallenge> challenges() const { return m_challenges; }

    // Reads the token from disk. Returns true when there was one; the account
    // name is then fetched in the background and the event stream opened.
    bool loadToken();
    void logIn();
    // Revokes the token on the server (DELETE /api/token), deletes the file
    // and closes every stream. Visible in the UI as "Abmelden".
    void logOut();
    // Abandon a login that is under way: shuts the local listener, forgets the
    // verifier and goes back to a state the user can act in. Without this a
    // login that never comes back leaves the app waiting for ever.
    void cancelLogIn();

    void refreshAccount();
    void refreshChallenges();

    // §3.2: only rapid, classical and correspondence can be reached through
    // the seek. Blitz would need a direct challenge — and is the wrong format
    // for learning anyway.
    void seek(int minutes, int increment, bool rated);
    void seekCorrespondence(int daysPerTurn, bool rated);
    void cancelSeek();
    void challengeAi(int level, int minutes, int increment);
    void acceptChallenge(const QString& id);
    void declineChallenge(const QString& id);

    void joinGame(const QString& gameId);
    void leaveGame();
    // §3.5 step 5: the board is *not* moved on the 200. The next `gameState`
    // line is what counts, because it also carries the authoritative clocks.
    void sendMove(const QString& uci, bool offeringDraw = false);
    void offerDraw(bool yes);
    void answerTakeback(bool yes);
    void requestTakeback();
    void resign();
    void abortGame();
    void claimVictory();
    void claimDraw();
    void addTime(int seconds);

    // The app was in the background and is back. Sailfish does not kill the
    // process, but the mobile connection may well have dropped the streams
    // while nothing was drawing; this checks and reopens them, and refetches
    // the game so that the board is right again.
    void resume();

    // The two doors the ndjson streams come in through. They are public on
    // purpose: tests/test_lichess.cpp feeds recorded lines through exactly
    // these, which is the only honest way to check the protocol without a
    // Lichess account and without touching the network.
    void handleEventLine(const QJsonObject& line);
    void handleGameLine(const QJsonObject& line);

signals:
    void stateChanged();
    void messageChanged();
    void challengesChanged();
    void seekingChanged();
    void gameStarted(const QString& gameId);
    void gameUpdated();
    void gameFinished(const QString& gameId, const QString& status, const QString& winner);
    void accountChanged();
    void failed(const QString& sentence);

private slots:
    void onRedirectConnection();
    void onRequestFinished();
    void onEventStreamReadyRead();
    void onEventStreamFinished();
    void onGameStreamReadyRead();
    void onGameStreamFinished();
    void onSeekFinished();
    void onEventWatchdog();
    void onGameWatchdog();
    void onEventReconnect();
    void onGameReconnect();
    void onBackoffOver();
    void onAuthTimeout();

private:
    struct PendingRequest {
        QString path;              // relative to the endpoint
        QByteArray body;           // empty and post == false means GET
        bool post = true;
        bool remove = false;       // HTTP DELETE
        QString what;              // how the answer is routed
        QString argument;
    };

    void setState(State state);
    void setMessage(const QString& sentence);
    void enqueue(const PendingRequest& request);
    void pump();
    void routeReply(const PendingRequest& request, int status, const QByteArray& body);

    void openEventStream();
    void closeEventStream();
    void openGameStream();
    void closeGameStream();
    void closeSeek();

    void saveToken(const QString& token);
    void clearToken();
    void exchangeCode(const QString& code);
    void startRedirectServer();
    void stopRedirectServer();

    QNetworkAccessManager* m_net;
    QTcpServer* m_server;
    QTimer* m_backoff;             // the 60 s after a 429
    QTimer* m_eventWatchdog;
    QTimer* m_gameWatchdog;
    QTimer* m_eventReconnect;
    QTimer* m_gameReconnect;
    QTimer* m_authTimeout;

    QNetworkReply* m_inFlight;
    QNetworkReply* m_eventReply;
    QNetworkReply* m_gameReply;
    QNetworkReply* m_seekReply;

    NdjsonSplitter m_eventSplitter;
    NdjsonSplitter m_gameSplitter;

    QQueue<PendingRequest> m_queue;
    PendingRequest m_current;
    bool m_busy;

    QString m_endpoint;
    QString m_dataDirectory;
    QString m_token;
    QString m_account;
    QString m_accountId;
    QString m_message;
    QString m_authUrl;
    QString m_verifier;
    QString m_oauthState;
    QString m_redirectUri;

    State m_state;
    LichessGame m_game;
    QString m_joinedGameId;
    QVector<LichessChallenge> m_challenges;

    int m_eventBackoffMs;
    int m_gameBackoffMs;
};

} // namespace schach

#endif // SCHACH_LICHESS_H
