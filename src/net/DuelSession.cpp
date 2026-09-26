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
#include "DuelSession.h"

#include <QDateTime>
#include <QVariantList>

namespace {

// Raised when the wire format changes in a way an older app cannot follow.
const int kProtocolVersion = 1;

QString key(const char* name)
{
    return QString::fromLatin1(name);
}

} // namespace

DuelSession::DuelSession(QObject* parent)
    : QObject(parent)
    , m_role(None)
    , m_playing(false)
    , m_weAreWhite(true)
    , m_hostTakesWhite(true)
{
    connect(&m_session, SIGNAL(peerJoined(int)), this, SLOT(onPeerJoined(int)));
    connect(&m_session, SIGNAL(peerLost(int)), this, SLOT(onPeerLost(int)));
    connect(&m_session, SIGNAL(messageReceived(int,QVariantMap)),
            this, SLOT(onMessage(int,QVariantMap)));
    connect(&m_session, SIGNAL(connectionFailed(QString)),
            this, SLOT(onConnectionFailed(QString)));
}

void DuelSession::setStatus(const QString& text)
{
    m_status = text;
    emit changed();
}

void DuelSession::send(const QVariantMap& message)
{
    m_session.send(message);
}

bool DuelSession::startHosting(const QString& name, int colour, QString* error)
{
    leave(QString());
    m_name = name.trimmed().isEmpty() ? tr("Gastgeber") : name.trimmed();
    if (colour == Random)
        m_hostTakesWhite = (QDateTime::currentMSecsSinceEpoch() & 1) == 0;
    else
        m_hostTakesWhite = colour == White;
    if (!m_session.startHosting(m_name, 2, 1, error))
        return false;
    m_role = Host;
    m_moves.clear();
    setStatus(tr("Warte auf den Gegner …"));
    return true;
}

void DuelSession::join(const QString& address, const QString& name)
{
    leave(QString());
    m_name = name.trimmed().isEmpty() ? tr("Gast") : name.trimmed();
    m_role = Guest;
    m_moves.clear();
    setStatus(tr("Verbinde mit %1 …").arg(address));
    m_session.joinHost(address);
}

void DuelSession::joinBluetooth(const QString& address, const QString& name)
{
    leave(QString());
    m_name = name.trimmed().isEmpty() ? tr("Gast") : name.trimmed();
    m_role = Guest;
    m_moves.clear();
    setStatus(tr("Verbinde über Bluetooth …"));
    m_session.joinBluetooth(address);
}

void DuelSession::leave(const QString& reason)
{
    if (m_session.peerConnected()) {
        QVariantMap bye;
        bye.insert(key("t"), key("bye"));
        bye.insert(key("reason"), reason);
        send(bye);
    }
    m_session.stop();
    m_role = None;
    m_playing = false;
    m_opponent.clear();
    m_moves.clear();
    if (!reason.isEmpty())
        setStatus(reason);
    else
        setStatus(QString());
}

void DuelSession::startGame(bool weAreWhite, const QString& opponent)
{
    m_weAreWhite = weAreWhite;
    m_opponent = opponent;
    m_playing = true;
    setStatus(weAreWhite ? tr("Du spielst Weiß gegen %1").arg(opponent)
                         : tr("Du spielst Schwarz gegen %1").arg(opponent));
    emit gameStarted(weAreWhite, opponent);
}

void DuelSession::onPeerJoined(int)
{
    if (m_role == Guest) {
        // The guest introduces itself; the host answers with the colours.
        QVariantMap hello;
        hello.insert(key("t"), key("hello"));
        hello.insert(key("kind"), key("schach"));
        hello.insert(key("v"), kProtocolVersion);
        hello.insert(key("name"), m_name);
        send(hello);
        setStatus(tr("Verbunden, warte auf die Farbwahl …"));
        return;
    }
    setStatus(tr("Gegner da, Partie beginnt …"));
}

void DuelSession::onPeerLost(int)
{
    const bool wasPlaying = m_playing;
    m_playing = false;
    m_session.stop();
    m_role = None;
    setStatus(wasPlaying ? tr("Die Verbindung zum Gegner ist weg")
                         : tr("Der Gegner hat abgebrochen"));
    if (wasPlaying)
        emit gameEnded(QString(), tr("Die Verbindung zum Gegner ist weg"));
}

void DuelSession::onConnectionFailed(const QString& reason)
{
    m_role = None;
    m_playing = false;
    setStatus(reason);
}

void DuelSession::onMessage(int peer, const QVariantMap& message)
{
    const QString type = message.value(key("t")).toString();

    if (m_role == Host && type == QLatin1String("hello")) {
        if (message.value(key("kind")).toString() != QLatin1String("schach")
                || message.value(key("v")).toInt() != kProtocolVersion) {
            QVariantMap bad;
            bad.insert(key("t"), key("version"));
            m_session.sendTo(peer, bad);
            m_session.dropPeer(peer);
            setStatus(tr("Das andere Gerät hat eine andere Fassung der App"));
            return;
        }
        const QString name = message.value(key("name")).toString();
        QVariantMap welcome;
        welcome.insert(key("t"), key("welcome"));
        welcome.insert(key("v"), kProtocolVersion);
        welcome.insert(key("white"), !m_hostTakesWhite);  // the guest's colour
        welcome.insert(key("name"), m_name);
        m_session.sendTo(peer, welcome);
        // No guest may join a running game.
        m_session.setAcceptingGuests(false);
        startGame(m_hostTakesWhite, name.isEmpty() ? tr("Gast") : name);
        return;
    }

    if (m_role == Guest && type == QLatin1String("welcome")) {
        startGame(message.value(key("white")).toBool(),
                  message.value(key("name")).toString().isEmpty()
                      ? tr("Gastgeber") : message.value(key("name")).toString());
        return;
    }

    if (type == QLatin1String("version")) {
        m_session.stop();
        m_role = None;
        setStatus(tr("Das andere Gerät hat eine andere Fassung der App"));
        return;
    }

    if (type == QLatin1String("busy")) {
        m_session.stop();
        m_role = None;
        setStatus(tr("Dort läuft schon eine Partie"));
        return;
    }

    if (type == QLatin1String("move")) {
        const QString uci = message.value(key("uci")).toString();
        const int ply = message.value(key("ply")).toInt();
        if (uci.isEmpty())
            return;
        if (ply != m_moves.size()) {
            // Out of step. The host puts it right, the guest asks it to.
            if (m_role == Host) {
                QVariantMap sync;
                sync.insert(key("t"), key("sync"));
                sync.insert(key("moves"), QVariant(m_moves));
                send(sync);
            } else {
                requestSync();
            }
            return;
        }
        m_moves.append(uci);
        emit movePlayed(uci, ply);
        return;
    }

    if (type == QLatin1String("req") && m_role == Host
            && message.value(key("op")).toString() == QLatin1String("sync")) {
        QVariantMap sync;
        sync.insert(key("t"), key("sync"));
        sync.insert(key("moves"), QVariant(m_moves));
        send(sync);
        return;
    }

    if (type == QLatin1String("sync") && m_role == Guest) {
        m_moves = message.value(key("moves")).toStringList();
        emit syncReceived(m_moves);
        return;
    }

    if (type == QLatin1String("draw")) {
        const QString what = message.value(key("op")).toString();
        if (what == QLatin1String("offer")) {
            emit drawOffered();
        } else if (what == QLatin1String("accept")) {
            m_playing = false;
            setStatus(tr("Remis vereinbart"));
            emit gameEnded(QString::fromLatin1("1/2-1/2"), tr("Remis vereinbart"));
        } else {
            setStatus(tr("Das Remisangebot wurde abgelehnt"));
        }
        return;
    }

    if (type == QLatin1String("resign")) {
        m_playing = false;
        setStatus(tr("Der Gegner hat aufgegeben"));
        emit gameEnded(m_weAreWhite ? QString::fromLatin1("1-0") : QString::fromLatin1("0-1"),
                       tr("Der Gegner hat aufgegeben"));
        return;
    }

    if (type == QLatin1String("bye")) {
        const QString reason = message.value(key("reason")).toString();
        m_playing = false;
        m_session.stop();
        m_role = None;
        setStatus(reason.isEmpty() ? tr("Der Gegner hat die Partie verlassen") : reason);
        emit gameEnded(QString(), m_status);
        return;
    }
}

void DuelSession::sendMove(const QString& uci, int ply)
{
    if (!m_playing)
        return;
    m_moves.append(uci);
    QVariantMap move;
    move.insert(key("t"), key("move"));
    move.insert(key("uci"), uci);
    move.insert(key("ply"), ply);
    send(move);
}

void DuelSession::sendResign()
{
    if (!m_playing)
        return;
    QVariantMap resign;
    resign.insert(key("t"), key("resign"));
    send(resign);
    m_playing = false;
    setStatus(tr("Du hast aufgegeben"));
    emit gameEnded(m_weAreWhite ? QString::fromLatin1("0-1") : QString::fromLatin1("1-0"),
                   tr("Du hast aufgegeben"));
}

void DuelSession::offerDraw()
{
    if (!m_playing)
        return;
    QVariantMap draw;
    draw.insert(key("t"), key("draw"));
    draw.insert(key("op"), key("offer"));
    send(draw);
    setStatus(tr("Remis angeboten"));
}

void DuelSession::answerDraw(bool accept)
{
    QVariantMap draw;
    draw.insert(key("t"), key("draw"));
    draw.insert(key("op"), accept ? key("accept") : key("decline"));
    send(draw);
    if (!accept)
        return;
    m_playing = false;
    setStatus(tr("Remis vereinbart"));
    emit gameEnded(QString::fromLatin1("1/2-1/2"), tr("Remis vereinbart"));
}

void DuelSession::requestSync()
{
    QVariantMap request;
    request.insert(key("t"), key("req"));
    request.insert(key("op"), key("sync"));
    send(request);
}
