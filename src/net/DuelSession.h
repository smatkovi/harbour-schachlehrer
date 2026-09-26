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
#ifndef SCHACH_DUELSESSION_H
#define SCHACH_DUELSESSION_H

// A game against a second device, over the local network or over Bluetooth.
//
// Chess has no hidden information, so both sides can simply keep the same
// list of moves: each device plays its own moves and sends them, and both
// check every move against the same rules. Only when the lists disagree does
// the host's list win -- it sends `sync` and the guest rebuilds. That is the
// whole protocol; LanSession does the carrying and does not care whether the
// line came over TCP or over RFCOMM.

#include "LanSession.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class DuelSession : public QObject
{
    Q_OBJECT
    // Read by the QML of the duel page; the actions go through TeacherEngine,
    // which also has to set up the board.
    Q_PROPERTY(int role READ roleValue NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool weAreWhite READ weAreWhite NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString opponent READ opponent NOTIFY changed)
    Q_PROPERTY(QObject* browser READ browserObject CONSTANT)
    Q_PROPERTY(QObject* bluetooth READ bluetoothObject CONSTANT)
    Q_PROPERTY(bool bluetoothHosting READ bluetoothHosting NOTIFY changed)
    Q_PROPERTY(QString bluetoothError READ bluetoothError NOTIFY changed)

public:
    enum Role { None = 0, Host = 1, Guest = 2 };
    // What the host takes; the guest gets the other one.
    enum Colour { White = 0, Black = 1, Random = 2 };

    explicit DuelSession(QObject* parent = 0);

    Role role() const { return m_role; }
    bool connected() const { return m_session.peerConnected(); }
    bool playing() const { return m_playing; }
    bool weAreWhite() const { return m_weAreWhite; }
    QString status() const { return m_status; }
    QString opponent() const { return m_opponent; }
    // The devices that can be joined: LAN hosts found by broadcast, and the
    // paired Bluetooth devices.
    LanBrowser* browser() { return &m_browser; }
    BtDevices* bluetooth() { return &m_bluetooth; }
    int roleValue() const { return int(m_role); }
    QObject* browserObject() { return &m_browser; }
    QObject* bluetoothObject() { return &m_bluetooth; }
    bool bluetoothHosting() const { return m_session.bluetoothHosting(); }
    QString bluetoothError() const { return m_session.bluetoothError(); }

    bool startHosting(const QString& name, int colour, QString* error);
    void join(const QString& address, const QString& name);
    void joinBluetooth(const QString& address, const QString& name);
    void leave(const QString& reason);

    // Called by the engine after it has played the move on its own board.
    void sendMove(const QString& uci, int ply);
    void sendResign();
    void offerDraw();
    void answerDraw(bool accept);
    // The guest asks for the host's list when a move of its own would not fit.
    void requestSync();

signals:
    void changed();
    // The board is set up; `weAreWhite` says from which side.
    void gameStarted(bool weAreWhite, const QString& opponent);
    // The other side played this move; `ply` is how many moves came before it.
    void movePlayed(const QString& uci, int ply);
    // The host's list of moves, to be taken over as it is.
    void syncReceived(const QStringList& moves);
    void drawOffered();
    void gameEnded(const QString& result, const QString& reason);

private slots:
    void onPeerJoined(int peer);
    void onPeerLost(int peer);
    void onMessage(int peer, const QVariantMap& message);
    void onConnectionFailed(const QString& reason);

private:
    void setStatus(const QString& text);
    void send(const QVariantMap& message);
    void startGame(bool weAreWhite, const QString& opponent);

    LanSession m_session;
    LanBrowser m_browser;
    BtDevices m_bluetooth;
    Role m_role;
    bool m_playing;
    bool m_weAreWhite;
    bool m_hostTakesWhite;
    QString m_status;
    QString m_name;
    QString m_opponent;
    // Every move both devices have agreed on, in UCI. The host's copy is the
    // one that counts.
    QStringList m_moves;
};

#endif // SCHACH_DUELSESSION_H
