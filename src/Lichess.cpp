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
#include "Lichess.h"

#include "core/Position.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

// The MeeGo Harmattan build force-includes meego/compat/qt4compat.h, which
// defines these for Qt 4.7: that Qt has no SHA-256 at all and no component
// formatting on QUrl. Everywhere else they are the plain Qt 5 spellings, so
// this file compiles unchanged for Sailfish OS.
#ifndef SCHACH_SHA256
#define SCHACH_SHA256(data) QCryptographicHash::hash((data), QCryptographicHash::Sha256)
#endif
#ifndef SCHACH_URL_DECODED
#define SCHACH_URL_DECODED QUrl::FullyDecoded
#define SCHACH_URL_ENCODED QUrl::FullyEncoded
#endif
#include <QtGlobal>

namespace schach {

namespace {

// platform.md §3.3: no client registration, any unique id, reverse domain
// notation. This one belongs to this app and to nothing else.
const char* const kClientId = "org.smatkovi.harbour-schachlehrer";
// §3.3: exactly these four. `email:read` is deliberately not asked for — it is
// not needed and it is a visible warning sign for the user.
const char* const kScopes = "board:play challenge:read challenge:write preference:read";

// §3.4: "In most cases, waiting one minute before retrying will be sufficient".
const int kRateLimitBackoffMs = 60000;
// §3.5: a watchdog of 15–20 s without *any* byte, keep-alive included.
const int kStreamWatchdogMs = 20000;
const int kFirstReconnectMs = 2000;
const int kMaxReconnectMs = 60000;
// A megabyte of one line is not ndjson any more.
const int kMaxLineBytes = 1 << 20;
// The browser has five minutes to come back with the code.
const int kAuthTimeoutMs = 300000;

// Qt 5.6 has no QRandomGenerator, and a PKCE verifier out of qrand() would be
// worse than none: an attacker who can guess the verifier can redeem the code.
// So this reads real entropy or it returns nothing, and the caller refuses to
// start the login with a sentence rather than with a weak secret.
QByteArray randomBytes(int count)
{
    QByteArray out;
    QFile urandom(QStringLiteral("/dev/urandom"));
    if (urandom.open(QIODevice::ReadOnly)) {
        out = urandom.read(count);
        urandom.close();
    }
    if (out.size() != count)
        return QByteArray();
    return out;
}

QString base64Url(const QByteArray& raw)
{
    QByteArray encoded = raw.toBase64();
    encoded.replace('+', '-');
    encoded.replace('/', '_');
    while (encoded.endsWith('='))
        encoded.chop(1);
    return QString::fromLatin1(encoded);
}

// §3.3: the code verifier must be at least 43 characters. 48 random bytes give
// 64 base64url characters, comfortably inside the 43…128 of RFC 7636.
QString makeVerifier()
{
    return base64Url(randomBytes(48));
}

QString challengeFor(const QString& verifier)
{
    // S256 is the only method Lichess accepts (§3.3).
    return base64Url(SCHACH_SHA256(verifier.toLatin1()));
}

QString percent(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

LichessPlayer parsePlayer(const QJsonObject& object)
{
    LichessPlayer player;
    if (object.contains(QStringLiteral("aiLevel"))) {
        player.ai = true;
        player.aiLevel = object.value(QStringLiteral("aiLevel")).toInt();
        player.name = QObject::tr("Lichess-Computer Stufe %1").arg(player.aiLevel);
        return player;
    }
    player.id = object.value(QStringLiteral("id")).toString();
    player.name = object.value(QStringLiteral("name")).toString();
    if (player.name.isEmpty())
        player.name = object.value(QStringLiteral("username")).toString();
    player.title = object.value(QStringLiteral("title")).toString();
    player.rating = object.value(QStringLiteral("rating")).toInt();
    player.provisional = object.value(QStringLiteral("provisional")).toBool();
    return player;
}

} // namespace

QString LichessPlayer::display() const
{
    if (ai)
        return name;
    QString out = name.isEmpty() ? id : name;
    if (!title.isEmpty())
        out = title + QLatin1Char(' ') + out;
    if (rating > 0)
        out += QStringLiteral(" (%1%2)").arg(rating).arg(provisional ? QStringLiteral("?") : QString());
    return out;
}

bool LichessGame::finished() const
{
    if (status.isEmpty())
        return false;
    return status != QLatin1String("started") && status != QLatin1String("created");
}

bool LichessGame::ourTurn() const
{
    // Whoever has an even number of half moves behind them is White.
    const bool whiteToMove = (moves.size() % 2) == 0;
    return whiteToMove == weAreWhite;
}

// ---------------------------------------------------------------------------
// ndjson
// ---------------------------------------------------------------------------

NdjsonSplitter::NdjsonSplitter()
    : m_keepAlives(0)
    , m_malformed(0)
    , m_overflow(false)
{
}

void NdjsonSplitter::reset()
{
    m_buffer.clear();
    m_keepAlives = 0;
    m_malformed = 0;
    m_overflow = false;
}

QVector<QJsonObject> NdjsonSplitter::feed(const QByteArray& chunk)
{
    QVector<QJsonObject> out;
    m_buffer += chunk;
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            // §3.5: an empty line every seven seconds is the keep-alive. It is
            // not an event, but it *is* a sign of life for the watchdog.
            ++m_keepAlives;
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            ++m_malformed;
            continue;
        }
        out.append(document.object());
    }
    if (m_buffer.size() > kMaxLineBytes) {
        m_overflow = true;
        m_buffer.clear();
    }
    return out;
}

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------

QStringList splitMoves(const QString& moves)
{
    // Hand-rolled rather than QString::split(): the enum for "skip empty
    // parts" moved namespaces between Qt 5.6 (the device) and Qt 5.14 (the
    // build host), and a version macro here would be three lines to save two.
    QStringList out;
    QString current;
    for (int i = 0; i < moves.size(); ++i) {
        const QChar character = moves.at(i);
        if (character.isSpace()) {
            if (!current.isEmpty()) {
                out.append(current);
                current.clear();
            }
        } else {
            current.append(character);
        }
    }
    if (!current.isEmpty())
        out.append(current);
    return out;
}

QString normaliseCastling(const core::Position& position, const QString& uci)
{
    if (uci.size() < 4)
        return uci;
    const int from = core::squareFromName(uci.left(2).toStdString());
    const int to = core::squareFromName(uci.mid(2, 2).toStdString());
    if (from < 0 || to < 0)
        return uci;
    const char mover = position.pieceAt(from);
    if (mover != 'K' && mover != 'k')
        return uci;
    const char target = position.pieceAt(to);
    const bool whiteKing = mover == 'K';
    // King onto our own rook: that is the Chess960 notation for castling.
    if (target != (whiteKing ? 'R' : 'r'))
        return uci;
    const int rank = whiteKing ? 0 : 7;
    const bool kingSide = core::fileOf(to) > core::fileOf(from);
    const int destination = rank * 8 + (kingSide ? 6 : 2);
    return QString::fromStdString(core::squareName(from) + core::squareName(destination));
}

bool parseGameFull(const QJsonObject& line, LichessGame& game, const QString& ourId)
{
    if (line.value(QStringLiteral("type")).toString() != QLatin1String("gameFull"))
        return false;
    game = LichessGame();
    game.valid = true;
    game.id = line.value(QStringLiteral("id")).toString();
    game.rated = line.value(QStringLiteral("rated")).toBool();
    game.speed = line.value(QStringLiteral("speed")).toString();
    const QJsonObject variant = line.value(QStringLiteral("variant")).toObject();
    if (!variant.isEmpty())
        game.variant = variant.value(QStringLiteral("key")).toString();
    const QString fen = line.value(QStringLiteral("initialFen")).toString();
    game.initialFen = (fen.isEmpty() || fen == QLatin1String("startpos")) ? QString() : fen;
    game.white = parsePlayer(line.value(QStringLiteral("white")).toObject());
    game.black = parsePlayer(line.value(QStringLiteral("black")).toObject());
    const QJsonObject clock = line.value(QStringLiteral("clock")).toObject();
    game.initialMs = static_cast<qint64>(clock.value(QStringLiteral("initial")).toDouble());
    game.incrementMs = static_cast<qint64>(clock.value(QStringLiteral("increment")).toDouble());
    game.daysPerTurn = static_cast<qint64>(line.value(QStringLiteral("daysPerTurn")).toDouble());

    // Which side we are. The id is the stable one; the display name is not.
    if (!ourId.isEmpty()) {
        if (game.white.id.compare(ourId, Qt::CaseInsensitive) == 0)
            game.weAreWhite = true;
        else if (game.black.id.compare(ourId, Qt::CaseInsensitive) == 0)
            game.weAreWhite = false;
    }
    parseGameState(line.value(QStringLiteral("state")).toObject(), game);
    return true;
}

bool parseGameState(const QJsonObject& line, LichessGame& game)
{
    if (line.value(QStringLiteral("type")).toString() != QLatin1String("gameState"))
        return false;
    // §3.5, first pitfall: `moves` is always the complete list from the start
    // of the game, never a delta. The caller diffs it against its own board.
    game.moves = splitMoves(line.value(QStringLiteral("moves")).toString());
    game.whiteMs = static_cast<qint64>(line.value(QStringLiteral("wtime")).toDouble());
    game.blackMs = static_cast<qint64>(line.value(QStringLiteral("btime")).toDouble());
    game.whiteIncMs = static_cast<qint64>(line.value(QStringLiteral("winc")).toDouble());
    game.blackIncMs = static_cast<qint64>(line.value(QStringLiteral("binc")).toDouble());
    game.status = line.value(QStringLiteral("status")).toString();
    game.winner = line.value(QStringLiteral("winner")).toString();
    game.whiteOffersDraw = line.value(QStringLiteral("wdraw")).toBool();
    game.blackOffersDraw = line.value(QStringLiteral("bdraw")).toBool();
    game.whiteWantsTakeback = line.value(QStringLiteral("wtakeback")).toBool();
    game.blackWantsTakeback = line.value(QStringLiteral("btakeback")).toBool();
    const QJsonObject expiration = line.value(QStringLiteral("expiration")).toObject();
    game.millisToMove = expiration.isEmpty()
            ? -1
            : static_cast<qint64>(expiration.value(QStringLiteral("millisToMove")).toDouble());
    return true;
}

bool parseOpponentGone(const QJsonObject& line, LichessGame& game)
{
    if (line.value(QStringLiteral("type")).toString() != QLatin1String("opponentGone"))
        return false;
    game.opponentGone = line.value(QStringLiteral("gone")).toBool();
    game.claimWinInSeconds = line.contains(QStringLiteral("claimWinInSeconds"))
            ? line.value(QStringLiteral("claimWinInSeconds")).toInt()
            : -1;
    return true;
}

LichessChallenge parseChallenge(const QJsonObject& challenge)
{
    LichessChallenge out;
    out.id = challenge.value(QStringLiteral("id")).toString();
    out.url = challenge.value(QStringLiteral("url")).toString();
    out.status = challenge.value(QStringLiteral("status")).toString();
    out.rated = challenge.value(QStringLiteral("rated")).toBool();
    out.speed = challenge.value(QStringLiteral("speed")).toString();
    out.colour = challenge.value(QStringLiteral("color")).toString();
    const QJsonObject variant = challenge.value(QStringLiteral("variant")).toObject();
    if (!variant.isEmpty())
        out.variant = variant.value(QStringLiteral("key")).toString();
    const QJsonObject challenger = challenge.value(QStringLiteral("challenger")).toObject();
    out.challengerName = challenger.value(QStringLiteral("name")).toString();
    out.challengerRating = challenger.value(QStringLiteral("rating")).toInt();
    out.destName = challenge.value(QStringLiteral("destUser")).toObject()
                           .value(QStringLiteral("name")).toString();
    const QJsonObject control = challenge.value(QStringLiteral("timeControl")).toObject();
    const QString type = control.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("clock")) {
        out.timeControl = QStringLiteral("%1+%2")
                                  .arg(control.value(QStringLiteral("limit")).toInt() / 60)
                                  .arg(control.value(QStringLiteral("increment")).toInt());
    } else if (type == QLatin1String("correspondence")) {
        out.timeControl = QObject::tr("%n Tag(e) pro Zug", "",
                                      control.value(QStringLiteral("daysPerTurn")).toInt());
    } else {
        out.timeControl = QObject::tr("ohne Uhr");
    }
    if (challenge.contains(QStringLiteral("compat"))) {
        out.boardCompatible = challenge.value(QStringLiteral("compat")).toObject()
                                      .value(QStringLiteral("board")).toBool(true);
    }
    return out;
}

// ---------------------------------------------------------------------------
// the client
// ---------------------------------------------------------------------------

Lichess::Lichess(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_server(0)
    , m_backoff(new QTimer(this))
    , m_eventWatchdog(new QTimer(this))
    , m_gameWatchdog(new QTimer(this))
    , m_eventReconnect(new QTimer(this))
    , m_gameReconnect(new QTimer(this))
    , m_authTimeout(new QTimer(this))
    , m_inFlight(0)
    , m_eventReply(0)
    , m_gameReply(0)
    , m_seekReply(0)
    , m_busy(false)
    , m_endpoint(QStringLiteral("https://lichess.org"))
    , m_state(LoggedOut)
    , m_eventBackoffMs(kFirstReconnectMs)
    , m_gameBackoffMs(kFirstReconnectMs)
{
    m_backoff->setSingleShot(true);
    m_eventWatchdog->setSingleShot(true);
    m_gameWatchdog->setSingleShot(true);
    m_eventReconnect->setSingleShot(true);
    m_gameReconnect->setSingleShot(true);
    m_authTimeout->setSingleShot(true);
    connect(m_backoff, SIGNAL(timeout()), this, SLOT(onBackoffOver()));
    connect(m_eventWatchdog, SIGNAL(timeout()), this, SLOT(onEventWatchdog()));
    connect(m_gameWatchdog, SIGNAL(timeout()), this, SLOT(onGameWatchdog()));
    connect(m_eventReconnect, SIGNAL(timeout()), this, SLOT(onEventReconnect()));
    connect(m_gameReconnect, SIGNAL(timeout()), this, SLOT(onGameReconnect()));
    connect(m_authTimeout, SIGNAL(timeout()), this, SLOT(onAuthTimeout()));
}

Lichess::~Lichess()
{
    closeEventStream();
    closeGameStream();
    closeSeek();
    stopRedirectServer();
}

void Lichess::setDataDirectory(const QString& path)
{
    m_dataDirectory = path;
    // Opening the store here and not in the constructor: the directory is the
    // one thing it needs, and a test that sets its own store afterwards must
    // win. makeTokenStore() also carries an older installation's token file
    // into Secrets, so this is the one place that migration can happen.
    if (!m_store)
        m_store.reset(makeTokenStore(m_dataDirectory));
}

void Lichess::setTokenStore(TokenStore* store)
{
    m_store.reset(store);
}

QString Lichess::tokenStoreDescription() const
{
    return m_store ? m_store->describe() : QString();
}

bool Lichess::tokenStoreEncrypted() const
{
    return m_store && m_store->encrypted();
}

QString Lichess::tokenPath() const
{
    if (m_dataDirectory.isEmpty())
        return QString();
    return m_dataDirectory + QStringLiteral("/lichess.token");
}

void Lichess::setEndpoint(const QString& baseUrl)
{
    QString clean = baseUrl;
    while (clean.endsWith(QLatin1Char('/')))
        clean.chop(1);
    if (!clean.isEmpty())
        m_endpoint = clean;
}

QByteArray Lichess::bearer() const
{
    if (m_token.isEmpty())
        return QByteArray();
    return QByteArray("Bearer ") + m_token.toLatin1();
}

bool Lichess::eventStreamOpen() const
{
    return m_eventReply != 0;
}

void Lichess::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void Lichess::setMessage(const QString& sentence)
{
    if (m_message == sentence)
        return;
    m_message = sentence;
    emit messageChanged();
}

// --- the token ---------------------------------------------------------------

bool Lichess::loadToken()
{
    if (!m_store)
        return false;
    const QString token = m_store->load();
    if (token.isEmpty())
        return false;
    m_token = token;
    setState(LoggedIn);
    setMessage(tr("Angemeldet. Ich hole deinen Kontonamen."));
    refreshAccount();
    openEventStream();
    return true;
}

void Lichess::saveToken(const QString& token)
{
    m_token = token;
    if (m_store && m_store->save(token))
        return;
    // The login itself worked; only keeping it did not. Saying so is the
    // honest answer — the session is real until the app ends.
    setMessage(tr("Der Zugangsschlüssel lässt sich nicht speichern. "
                  "Die Anmeldung gilt nur für diesen Start der App."));
}

void Lichess::clearToken()
{
    m_token.clear();
    if (m_store)
        m_store->clear();
    // An installation that used the file before this code may still have one
    // lying about if the migration never ran. Logging out must leave nothing.
    const QString path = tokenPath();
    if (!path.isEmpty())
        QFile::remove(path);
}

// --- OAuth 2.0 PKCE ----------------------------------------------------------

void Lichess::logIn()
{
    if (m_state == Authorising)
        return;
    const QString verifier = makeVerifier();
    if (verifier.size() < 43) {
        // §3.3 requires at least 43 characters, and we would rather have none
        // than a guessable one.
        setMessage(tr("Auf diesem Gerät lässt sich gerade kein sicherer "
                      "Anmeldeschlüssel erzeugen. Ohne Konto funktioniert die App "
                      "vollständig weiter."));
        emit failed(m_message);
        return;
    }
    startRedirectServer();
    if (m_redirectUri.isEmpty()) {
        setMessage(tr("Die Anmeldung braucht einen kurzzeitigen lokalen Zugang, "
                      "und der lässt sich nicht öffnen. Versuch es später noch einmal."));
        emit failed(m_message);
        return;
    }

    m_verifier = verifier;
    m_oauthState = base64Url(randomBytes(16));

    QString url = m_endpoint + QStringLiteral("/oauth?response_type=code");
    url += QStringLiteral("&client_id=") + percent(QString::fromLatin1(kClientId));
    url += QStringLiteral("&redirect_uri=") + percent(m_redirectUri);
    url += QStringLiteral("&code_challenge_method=S256");
    url += QStringLiteral("&code_challenge=") + percent(challengeFor(m_verifier));
    url += QStringLiteral("&scope=") + percent(QString::fromLatin1(kScopes));
    url += QStringLiteral("&state=") + percent(m_oauthState);
    m_authUrl = url;

    setState(Authorising);
    setMessage(tr("Die Anmeldeseite von Lichess öffnet sich im Browser. "
                  "Komm danach in diese App zurück."));
    m_authTimeout->start(kAuthTimeoutMs);
    emit stateChanged();
}

void Lichess::startRedirectServer()
{
    stopRedirectServer();
    m_server = new QTcpServer(this);
    // RFC 8252 §7.3, platform.md §3.3: the loopback address with a port the
    // system hands out. Lichess accepts http on 127.0.0.1 explicitly, so no
    // URL scheme has to be registered with the Sailfish dispatcher and nothing
    // depends on xdg-open working.
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        delete m_server;
        m_server = 0;
        m_redirectUri.clear();
        return;
    }
    connect(m_server, SIGNAL(newConnection()), this, SLOT(onRedirectConnection()));
    m_redirectUri = QStringLiteral("http://127.0.0.1:%1/cb").arg(m_server->serverPort());
}

void Lichess::stopRedirectServer()
{
    if (!m_server)
        return;
    m_server->close();
    m_server->deleteLater();
    m_server = 0;
}

void Lichess::onRedirectConnection()
{
    if (!m_server)
        return;
    QTcpSocket* socket = m_server->nextPendingConnection();
    if (!socket)
        return;
    socket->waitForReadyRead(3000);
    const QByteArray request = socket->readAll();
    const int endOfLine = request.indexOf('\n');
    const QByteArray requestLine = endOfLine < 0 ? request : request.left(endOfLine);
    const QList<QByteArray> parts = requestLine.simplified().split(' ');

    QString code, returnedState, error;
    if (parts.size() >= 2) {
        const QUrl url(QStringLiteral("http://127.0.0.1") + QString::fromLatin1(parts.at(1)));
        if (url.path() == QLatin1String("/cb")) {
            const QUrlQuery query(url);
            code = query.queryItemValue(QStringLiteral("code"), SCHACH_URL_DECODED);
            returnedState = query.queryItemValue(QStringLiteral("state"), SCHACH_URL_DECODED);
            error = query.queryItemValue(QStringLiteral("error"), SCHACH_URL_DECODED);
        }
    }

    const QByteArray page =
            QStringLiteral("<!doctype html><html lang=\"de\"><head>"
                           "<meta charset=\"utf-8\"><title>Schachlehrer</title></head>"
                           "<body style=\"font-family:sans-serif;padding:2em\">"
                           "<h1>Fertig</h1><p>Sie k\xc3\xb6nnen zur App zur\xc3\xbc""ckkehren.</p>"
                           "</body></html>").toUtf8();
    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n";
    response += "Content-Length: " + QByteArray::number(page.size()) + "\r\nConnection: close\r\n\r\n";
    response += page;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
    socket->deleteLater();

    if (!error.isEmpty() || code.isEmpty()) {
        stopRedirectServer();
        m_authTimeout->stop();
        setState(LoggedOut);
        setMessage(tr("Die Anmeldung wurde abgebrochen. Ohne Konto funktioniert "
                      "die App vollständig weiter — nur online spielen kannst du nicht."));
        return;
    }
    // The state is the only thing that ties the answer to our own request.
    if (returnedState != m_oauthState) {
        stopRedirectServer();
        m_authTimeout->stop();
        setState(LoggedOut);
        setMessage(tr("Die Antwort der Anmeldeseite passt nicht zu dieser Anfrage. "
                      "Ich habe sie verworfen. Versuch die Anmeldung noch einmal."));
        return;
    }
    stopRedirectServer();
    m_authTimeout->stop();
    exchangeCode(code);
}

void Lichess::onAuthTimeout()
{
    if (m_state != Authorising)
        return;
    stopRedirectServer();
    setState(hasToken() ? LoggedIn : LoggedOut);
    setMessage(tr("Die Anmeldung hat zu lange gedauert und ist abgelaufen. "
                  "Du kannst sie jederzeit neu starten."));
}

void Lichess::exchangeCode(const QString& code)
{
    PendingRequest request;
    request.path = QStringLiteral("/api/token");
    request.post = true;
    request.what = QStringLiteral("token");
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("code_verifier"), m_verifier);
    form.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    form.addQueryItem(QStringLiteral("client_id"), QString::fromLatin1(kClientId));
    request.body = form.toString(SCHACH_URL_ENCODED).toUtf8();
    enqueue(request);
}

void Lichess::cancelLogIn()
{
    if (m_state != Authorising)
        return;
    m_authTimeout->stop();
    stopRedirectServer();
    m_verifier.clear();
    m_oauthState.clear();
    m_authUrl.clear();
    setState(m_token.isEmpty() ? LoggedOut : LoggedIn);
    setMessage(tr("Anmeldung abgebrochen. Alles andere in dieser App funktioniert ohne Konto."));
    emit stateChanged();
}

void Lichess::logOut()
{
    if (!m_token.isEmpty()) {
        // Best effort: a token that is revoked on the server cannot be misused
        // if the file survives somewhere in a backup.
        PendingRequest request;
        request.path = QStringLiteral("/api/token");
        request.remove = true;
        request.post = false;
        request.what = QStringLiteral("revoke");
        enqueue(request);
    }
    closeEventStream();
    closeGameStream();
    closeSeek();
    m_eventReconnect->stop();
    m_gameReconnect->stop();
    clearToken();
    m_account.clear();
    m_accountId.clear();
    m_challenges.clear();
    m_game = LichessGame();
    m_joinedGameId.clear();
    setState(LoggedOut);
    setMessage(tr("Abgemeldet. Der Zugangsschlüssel auf diesem Gerät ist gelöscht."));
    emit accountChanged();
    emit challengesChanged();
}

// --- the serial request queue -------------------------------------------------

void Lichess::enqueue(const PendingRequest& request)
{
    m_queue.enqueue(request);
    pump();
}

void Lichess::pump()
{
    // §3.4, verbatim: "Only make one request at a time."
    if (m_busy || m_queue.isEmpty() || m_backoff->isActive())
        return;
    m_current = m_queue.dequeue();
    m_busy = true;

    QNetworkRequest request(QUrl(m_endpoint + m_current.path));
    if (!m_token.isEmpty()) {
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + m_token.toLatin1());
    }
    request.setRawHeader("Accept", "application/json");
    if (m_current.post && !m_current.body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/x-www-form-urlencoded"));
    }

    if (m_current.remove)
        m_inFlight = m_net->deleteResource(request);
    else if (m_current.post)
        m_inFlight = m_net->post(request, m_current.body);
    else
        m_inFlight = m_net->get(request);
    connect(m_inFlight, SIGNAL(finished()), this, SLOT(onRequestFinished()));
}

void Lichess::onBackoffOver()
{
    pump();
}

void Lichess::onRequestFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    if (reply != m_inFlight)
        return;
    m_inFlight = 0;
    m_busy = false;

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const PendingRequest request = m_current;
    m_current = PendingRequest();

    if (status == 429) {
        // §3.4: wait a minute, then try the same request again. Never a retry
        // loop without backoff.
        m_queue.prepend(request);
        m_backoff->start(kRateLimitBackoffMs);
        setMessage(tr("Lichess bremst uns gerade aus. Ich warte eine Minute und "
                      "versuche es dann noch einmal."));
        return;
    }
    routeReply(request, status, body);
    pump();
}

void Lichess::fetchJson(const QString& path, const QString& tag)
{
    PendingRequest request;
    request.path = path;
    request.post = false;
    request.what = QStringLiteral("json:") + tag;
    enqueue(request);
}

void Lichess::routeReply(const PendingRequest& request, int status, const QByteArray& body)
{
    // Handed straight back out: this client knows about sessions, games and
    // challenges, and nothing about what else the caller asked for.
    if (request.what.startsWith(QLatin1String("json:"))) {
        emit jsonArrived(request.what.mid(5), status, body);
        return;
    }

    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const bool ok = status >= 200 && status < 300;
    const QString serverError = object.value(QStringLiteral("error")).toString();

    if (request.what == QLatin1String("token")) {
        const QString token = object.value(QStringLiteral("access_token")).toString();
        if (!ok || token.isEmpty()) {
            setState(LoggedOut);
            setMessage(tr("Die Anmeldung bei Lichess ist fehlgeschlagen. "
                          "Die App funktioniert ohne Konto vollständig weiter."));
            emit failed(m_message);
            return;
        }
        saveToken(token);
        setState(LoggedIn);
        setMessage(tr("Angemeldet bei Lichess."));
        refreshAccount();
        refreshChallenges();
        openEventStream();
        return;
    }

    if (request.what == QLatin1String("account")) {
        if (!ok) {
            setMessage(tr("Dein Konto lässt sich gerade nicht abfragen."));
            return;
        }
        m_accountId = object.value(QStringLiteral("id")).toString();
        m_account = object.value(QStringLiteral("username")).toString();
        if (m_account.isEmpty())
            m_account = m_accountId;
        setMessage(tr("Angemeldet als %1.").arg(m_account));
        emit accountChanged();
        return;
    }

    if (request.what == QLatin1String("challenges")) {
        if (!ok)
            return;
        m_challenges.clear();
        const QJsonArray incoming = object.value(QStringLiteral("in")).toArray();
        for (int i = 0; i < incoming.size(); ++i) {
            LichessChallenge challenge = parseChallenge(incoming.at(i).toObject());
            challenge.incoming = true;
            m_challenges.append(challenge);
        }
        const QJsonArray outgoing = object.value(QStringLiteral("out")).toArray();
        for (int i = 0; i < outgoing.size(); ++i) {
            LichessChallenge challenge = parseChallenge(outgoing.at(i).toObject());
            challenge.incoming = false;
            m_challenges.append(challenge);
        }
        emit challengesChanged();
        return;
    }

    if (request.what == QLatin1String("move")) {
        if (!ok) {
            // §3.5 step 5: the board was not moved on our side either, so
            // there is nothing to roll back — only something to say.
            setMessage(serverError.isEmpty()
                               ? tr("Der Zug ist nicht angekommen. Versuch ihn noch einmal.")
                               : tr("Lichess hat den Zug abgelehnt: %1").arg(serverError));
            emit failed(m_message);
        }
        return;
    }

    if (request.what == QLatin1String("accept") || request.what == QLatin1String("decline")) {
        refreshChallenges();
        if (!ok && !serverError.isEmpty())
            setMessage(serverError);
        return;
    }

    if (request.what == QLatin1String("revoke"))
        return;

    if (!ok && !serverError.isEmpty()) {
        setMessage(serverError);
        emit failed(serverError);
    }
}

void Lichess::refreshAccount()
{
    if (m_token.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/account");
    request.post = false;
    request.what = QStringLiteral("account");
    enqueue(request);
}

void Lichess::refreshChallenges()
{
    if (m_token.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/challenge");
    request.post = false;
    request.what = QStringLiteral("challenges");
    enqueue(request);
}

// --- the event stream ---------------------------------------------------------

void Lichess::openEventStream()
{
    if (m_token.isEmpty() || m_eventReply)
        return;
    // §3.4: only one global event stream per token — a second one closes the
    // first. So there is exactly one here, ever.
    QNetworkRequest request(QUrl(m_endpoint + QStringLiteral("/api/stream/event")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toLatin1());
    request.setRawHeader("Accept", "application/x-ndjson");
    m_eventSplitter.reset();
    m_eventReply = m_net->get(request);
    connect(m_eventReply, SIGNAL(readyRead()), this, SLOT(onEventStreamReadyRead()));
    connect(m_eventReply, SIGNAL(finished()), this, SLOT(onEventStreamFinished()));
    m_eventWatchdog->start(kStreamWatchdogMs);
}

void Lichess::closeEventStream()
{
    m_eventWatchdog->stop();
    if (!m_eventReply)
        return;
    QNetworkReply* reply = m_eventReply;
    m_eventReply = 0;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void Lichess::onEventStreamReadyRead()
{
    if (!m_eventReply)
        return;
    // §3.5: read incrementally, never wait for finished() — these answers
    // never end by themselves.
    const QVector<QJsonObject> lines = m_eventSplitter.feed(m_eventReply->readAll());
    m_eventWatchdog->start(kStreamWatchdogMs);
    m_eventBackoffMs = kFirstReconnectMs;
    if (m_eventSplitter.overflowed()) {
        closeEventStream();
        m_eventReconnect->start(m_eventBackoffMs);
        return;
    }
    for (int i = 0; i < lines.size(); ++i)
        handleEventLine(lines.at(i));
}

void Lichess::onEventStreamFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
        reply->deleteLater();
    if (reply != m_eventReply)
        return;
    m_eventReply = 0;
    m_eventWatchdog->stop();
    if (m_token.isEmpty())
        return;
    // §3.4: a 429 means a minute, not two seconds. The undocumented limit on
    // this endpoint is 30 streams per 10 minutes per token, so getting this
    // wrong locks the user out of his own game.
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429) {
        m_eventBackoffMs = kRateLimitBackoffMs;
        setMessage(tr("Lichess bremst uns gerade aus. Die Verbindung kommt in "
                      "einer Minute wieder."));
    }
    // Exponential backoff, capped at a minute. The server counts event streams
    // (§3.4: 30 per 10 minutes per token); a tight reconnect loop would get us
    // shut out.
    m_eventReconnect->start(m_eventBackoffMs);
    m_eventBackoffMs = qMin(m_eventBackoffMs * 2, kMaxReconnectMs);
}

void Lichess::onEventWatchdog()
{
    // Not one byte for twenty seconds, keep-alive included: the connection is
    // dead even if the socket does not know it yet.
    closeEventStream();
    if (m_token.isEmpty())
        return;
    m_eventReconnect->start(m_eventBackoffMs);
    m_eventBackoffMs = qMin(m_eventBackoffMs * 2, kMaxReconnectMs);
}

void Lichess::onEventReconnect()
{
    openEventStream();
}

void Lichess::handleEventLine(const QJsonObject& line)
{
    const QString type = line.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("gameStart")) {
        const QJsonObject game = line.value(QStringLiteral("game")).toObject();
        const QString id = game.value(QStringLiteral("gameId")).toString().isEmpty()
                ? game.value(QStringLiteral("id")).toString()
                : game.value(QStringLiteral("gameId")).toString();
        if (id.isEmpty())
            return;
        // The seek stream is closed by the server at this point anyway (§3.5).
        closeSeek();
        m_game = LichessGame();
        m_game.valid = true;
        m_game.id = id;
        m_game.weAreWhite = game.value(QStringLiteral("color")).toString() != QLatin1String("black");
        m_game.rated = game.value(QStringLiteral("rated")).toBool();
        m_game.speed = game.value(QStringLiteral("speed")).toString();
        m_game.status = QStringLiteral("started");
        const QJsonObject opponent = game.value(QStringLiteral("opponent")).toObject();
        LichessPlayer other = parsePlayer(opponent);
        if (m_game.weAreWhite)
            m_game.black = other;
        else
            m_game.white = other;
        m_joinedGameId = id;
        // The fair-play lock is TeacherEngine's business, and it hangs off
        // this signal: from here until gameFinished there is no engine
        // (platform.md §3.7).
        emit gameStarted(id);
        openGameStream();
        return;
    }

    if (type == QLatin1String("gameFinish")) {
        const QJsonObject game = line.value(QStringLiteral("game")).toObject();
        QString id = game.value(QStringLiteral("gameId")).toString();
        if (id.isEmpty())
            id = game.value(QStringLiteral("id")).toString();
        if (!m_joinedGameId.isEmpty() && id != m_joinedGameId)
            return;
        const QJsonObject statusObject = game.value(QStringLiteral("status")).toObject();
        const QString status = statusObject.value(QStringLiteral("name")).toString();
        if (!status.isEmpty())
            m_game.status = status;
        const QString winner = game.value(QStringLiteral("winner")).toString();
        if (!winner.isEmpty())
            m_game.winner = winner;
        closeGameStream();
        m_joinedGameId.clear();
        emit gameUpdated();
        emit gameFinished(id, m_game.status, m_game.winner);
        return;
    }

    if (type == QLatin1String("challenge")) {
        LichessChallenge challenge = parseChallenge(line.value(QStringLiteral("challenge")).toObject());
        if (line.contains(QStringLiteral("compat"))) {
            challenge.boardCompatible = line.value(QStringLiteral("compat")).toObject()
                                                .value(QStringLiteral("board")).toBool(true);
        }
        challenge.incoming = m_account.isEmpty()
                || challenge.challengerName.compare(m_account, Qt::CaseInsensitive) != 0;
        for (int i = 0; i < m_challenges.size(); ++i) {
            if (m_challenges.at(i).id == challenge.id) {
                m_challenges[i] = challenge;
                emit challengesChanged();
                return;
            }
        }
        m_challenges.append(challenge);
        emit challengesChanged();
        return;
    }

    if (type == QLatin1String("challengeCanceled") || type == QLatin1String("challengeDeclined")) {
        const QString id = line.value(QStringLiteral("challenge")).toObject()
                                   .value(QStringLiteral("id")).toString();
        for (int i = 0; i < m_challenges.size(); ++i) {
            if (m_challenges.at(i).id == id) {
                m_challenges.remove(i);
                emit challengesChanged();
                return;
            }
        }
        return;
    }
}

// --- the game stream ----------------------------------------------------------

void Lichess::joinGame(const QString& gameId)
{
    if (gameId.isEmpty())
        return;
    m_joinedGameId = gameId;
    if (m_game.id != gameId) {
        m_game = LichessGame();
        m_game.valid = true;
        m_game.id = gameId;
        m_game.status = QStringLiteral("started");
    }
    openGameStream();
}

void Lichess::leaveGame()
{
    closeGameStream();
    m_gameReconnect->stop();
    m_joinedGameId.clear();
}

void Lichess::openGameStream()
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty() || m_gameReply)
        return;
    QNetworkRequest request(QUrl(m_endpoint + QStringLiteral("/api/board/game/stream/")
                                 + m_joinedGameId));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toLatin1());
    request.setRawHeader("Accept", "application/x-ndjson");
    m_gameSplitter.reset();
    m_gameReply = m_net->get(request);
    connect(m_gameReply, SIGNAL(readyRead()), this, SLOT(onGameStreamReadyRead()));
    connect(m_gameReply, SIGNAL(finished()), this, SLOT(onGameStreamFinished()));
    m_gameWatchdog->start(kStreamWatchdogMs);
}

void Lichess::closeGameStream()
{
    m_gameWatchdog->stop();
    if (!m_gameReply)
        return;
    QNetworkReply* reply = m_gameReply;
    m_gameReply = 0;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void Lichess::onGameStreamReadyRead()
{
    if (!m_gameReply)
        return;
    const QVector<QJsonObject> lines = m_gameSplitter.feed(m_gameReply->readAll());
    m_gameWatchdog->start(kStreamWatchdogMs);
    m_gameBackoffMs = kFirstReconnectMs;
    if (m_gameSplitter.overflowed()) {
        closeGameStream();
        m_gameReconnect->start(m_gameBackoffMs);
        return;
    }
    for (int i = 0; i < lines.size(); ++i)
        handleGameLine(lines.at(i));
}

void Lichess::onGameStreamFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
        reply->deleteLater();
    if (reply != m_gameReply)
        return;
    m_gameReply = 0;
    m_gameWatchdog->stop();
    // §3.5 step 6: the server closes this stream when the game ends. That is a
    // normal end, not an error, and there is nothing to reconnect to.
    if (m_game.finished() || m_joinedGameId.isEmpty())
        return;
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429)
        m_gameBackoffMs = kRateLimitBackoffMs;
    m_gameReconnect->start(m_gameBackoffMs);
    m_gameBackoffMs = qMin(m_gameBackoffMs * 2, kMaxReconnectMs);
}

void Lichess::onGameWatchdog()
{
    closeGameStream();
    if (m_game.finished() || m_joinedGameId.isEmpty())
        return;
    m_gameReconnect->start(m_gameBackoffMs);
    m_gameBackoffMs = qMin(m_gameBackoffMs * 2, kMaxReconnectMs);
}

void Lichess::onGameReconnect()
{
    openGameStream();
}

void Lichess::handleGameLine(const QJsonObject& line)
{
    const QString type = line.value(QStringLiteral("type")).toString();

    if (type == QLatin1String("gameFull")) {
        const bool wasStarted = !m_game.id.isEmpty();
        const bool weWereWhite = m_game.weAreWhite;
        LichessGame game;
        // Our side is decided by the account id; when it is not known yet, the
        // colour from the gameStart event stands.
        parseGameFull(line, game, m_accountId);
        if (m_accountId.isEmpty() && wasStarted)
            game.weAreWhite = weWereWhite;
        if (game.id.isEmpty())
            game.id = m_joinedGameId;
        m_game = game;
        if (m_joinedGameId.isEmpty())
            m_joinedGameId = m_game.id;
        emit gameUpdated();
        if (m_game.finished())
            emit gameFinished(m_game.id, m_game.status, m_game.winner);
        return;
    }

    if (type == QLatin1String("gameState")) {
        parseGameState(line, m_game);
        emit gameUpdated();
        if (m_game.finished()) {
            closeGameStream();
            const QString id = m_game.id;
            m_joinedGameId.clear();
            emit gameFinished(id, m_game.status, m_game.winner);
        }
        return;
    }

    if (type == QLatin1String("opponentGone")) {
        parseOpponentGone(line, m_game);
        emit gameUpdated();
        return;
    }

    // chatLine is read but not shown: a chat window during a rated game is one
    // more place where advice could arrive, and §3.7 is about where the move
    // comes from. Nothing to do here.
}

// --- playing ------------------------------------------------------------------

void Lichess::seek(int minutes, int increment, bool rated)
{
    if (m_token.isEmpty() || m_seekReply)
        return;
    // §3.5 step 1: the event stream goes first, so that an immediately
    // accepted seek does not lose its gameStart.
    openEventStream();

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("rated"), rated ? QStringLiteral("true") : QStringLiteral("false"));
    form.addQueryItem(QStringLiteral("time"), QString::number(minutes));
    form.addQueryItem(QStringLiteral("increment"), QString::number(increment));
    form.addQueryItem(QStringLiteral("variant"), QStringLiteral("standard"));

    QNetworkRequest request(QUrl(m_endpoint + QStringLiteral("/api/board/seek")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toLatin1());
    request.setRawHeader("Accept", "application/x-ndjson");
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    // §3.5 step 2: this request blocks by design. The body is only blank
    // lines, and closing the connection cancels the seek — so it is held open
    // and never waited on.
    m_seekReply = m_net->post(request, form.toString(SCHACH_URL_ENCODED).toUtf8());
    connect(m_seekReply, SIGNAL(finished()), this, SLOT(onSeekFinished()));
    setMessage(tr("Ich suche einen Gegner. Das kann einen Moment dauern."));
    emit seekingChanged();
}

void Lichess::seekCorrespondence(int daysPerTurn, bool rated)
{
    if (m_token.isEmpty() || m_seekReply)
        return;
    openEventStream();
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("rated"), rated ? QStringLiteral("true") : QStringLiteral("false"));
    form.addQueryItem(QStringLiteral("days"), QString::number(daysPerTurn));
    form.addQueryItem(QStringLiteral("variant"), QStringLiteral("standard"));
    QNetworkRequest request(QUrl(m_endpoint + QStringLiteral("/api/board/seek")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_token.toLatin1());
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    // Correspondence returns at once with {"id": …} and the seek stays on the
    // server (§3.5 step 2).
    m_seekReply = m_net->post(request, form.toString(SCHACH_URL_ENCODED).toUtf8());
    connect(m_seekReply, SIGNAL(finished()), this, SLOT(onSeekFinished()));
    setMessage(tr("Die Fernschach-Anzeige ist aufgegeben. Du bekommst Bescheid, "
                  "sobald jemand sie annimmt."));
    emit seekingChanged();
}

void Lichess::cancelSeek()
{
    if (!m_seekReply)
        return;
    // Closing the connection cancels the seek — that is the documented way.
    closeSeek();
    setMessage(tr("Die Suche ist beendet."));
}

void Lichess::closeSeek()
{
    if (!m_seekReply)
        return;
    QNetworkReply* reply = m_seekReply;
    m_seekReply = 0;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    emit seekingChanged();
}

void Lichess::onSeekFinished()
{
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (reply)
        reply->deleteLater();
    if (reply != m_seekReply)
        return;
    m_seekReply = 0;
    // Either the seek was accepted (the gameStart event is already on its way
    // through the event stream) or it expired. Neither is an error.
    emit seekingChanged();
}

void Lichess::challengeAi(int level, int minutes, int increment)
{
    if (m_token.isEmpty())
        return;
    openEventStream();
    PendingRequest request;
    request.path = QStringLiteral("/api/challenge/ai");
    request.post = true;
    request.what = QStringLiteral("challengeAi");
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("level"), QString::number(qBound(1, level, 8)));
    form.addQueryItem(QStringLiteral("clock.limit"), QString::number(minutes * 60));
    form.addQueryItem(QStringLiteral("clock.increment"), QString::number(increment));
    request.body = form.toString(SCHACH_URL_ENCODED).toUtf8();
    enqueue(request);
}

void Lichess::acceptChallenge(const QString& id)
{
    if (m_token.isEmpty() || id.isEmpty())
        return;
    openEventStream();
    PendingRequest request;
    request.path = QStringLiteral("/api/challenge/%1/accept").arg(id);
    request.post = true;
    request.what = QStringLiteral("accept");
    request.argument = id;
    enqueue(request);
}

void Lichess::declineChallenge(const QString& id)
{
    if (m_token.isEmpty() || id.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/challenge/%1/decline").arg(id);
    request.post = true;
    request.what = QStringLiteral("decline");
    request.argument = id;
    enqueue(request);
}

void Lichess::sendMove(const QString& uci, bool offeringDraw)
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty() || uci.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/move/%2").arg(m_joinedGameId, uci);
    if (offeringDraw)
        request.path += QStringLiteral("?offeringDraw=true");
    request.post = true;
    request.what = QStringLiteral("move");
    request.argument = uci;
    enqueue(request);
}

void Lichess::offerDraw(bool yes)
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/draw/%2")
                           .arg(m_joinedGameId, yes ? QStringLiteral("yes") : QStringLiteral("no"));
    request.post = true;
    request.what = QStringLiteral("draw");
    enqueue(request);
}

void Lichess::requestTakeback()
{
    answerTakeback(true);
}

void Lichess::answerTakeback(bool yes)
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/takeback/%2")
                           .arg(m_joinedGameId, yes ? QStringLiteral("yes") : QStringLiteral("no"));
    request.post = true;
    request.what = QStringLiteral("takeback");
    enqueue(request);
}

void Lichess::resign()
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/resign").arg(m_joinedGameId);
    request.post = true;
    request.what = QStringLiteral("resign");
    enqueue(request);
}

void Lichess::abortGame()
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/abort").arg(m_joinedGameId);
    request.post = true;
    request.what = QStringLiteral("abort");
    enqueue(request);
}

void Lichess::claimVictory()
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/claim-victory").arg(m_joinedGameId);
    request.post = true;
    request.what = QStringLiteral("claimVictory");
    enqueue(request);
}

void Lichess::claimDraw()
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/board/game/%1/claim-draw").arg(m_joinedGameId);
    request.post = true;
    request.what = QStringLiteral("claimDraw");
    enqueue(request);
}

void Lichess::addTime(int seconds)
{
    if (m_token.isEmpty() || m_joinedGameId.isEmpty())
        return;
    PendingRequest request;
    request.path = QStringLiteral("/api/round/%1/add-time/%2")
                           .arg(m_joinedGameId).arg(seconds);
    request.post = true;
    request.what = QStringLiteral("addTime");
    enqueue(request);
}

void Lichess::resume()
{
    if (m_token.isEmpty())
        return;
    // Coming back from the background. The process was never killed, but a
    // mobile connection that went away while nothing was drawing leaves both
    // streams half-open; the watchdogs would find that in twenty seconds, and
    // this finds it now.
    m_eventReconnect->stop();
    m_gameReconnect->stop();
    if (!m_eventReply) {
        m_eventBackoffMs = kFirstReconnectMs;
        openEventStream();
    }
    if (!m_joinedGameId.isEmpty() && !m_gameReply) {
        m_gameBackoffMs = kFirstReconnectMs;
        openGameStream();
    }
    refreshChallenges();
}

} // namespace schach
