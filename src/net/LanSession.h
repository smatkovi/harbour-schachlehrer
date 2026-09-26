#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTimer>

#include "BtLink.h"
#include <QVariantList>
#include <QVariantMap>

class QTcpSocket;
class QUdpSocket;

// Transport for a game against a second device, taken over from
// harbour-snapszer with its own ports, RFCOMM channel and discovery words. One device hosts (TCP server plus a
// UDP responder so other devices can find it, and an RFCOMM channel for
// guests that come over Bluetooth), the others join over whichever of the two
// they like. Messages are compact JSON objects, one per line, the same on
// both transports. The session knows nothing about the game rules. A host
// accepts up to `maxPeers` guests; a guest has exactly one peer, the host,
// with id 0.
class LanSession : public QObject
{
    Q_OBJECT

public:
    enum Role { None = 0, Host = 1, Guest = 2 };

    static const quint16 GamePort = 45485;
    static const quint16 DiscoveryPort = 45486;

    explicit LanSession(QObject* parent = nullptr);
    ~LanSession() override;

    Role role() const { return m_role; }
    bool peerConnected() const { return !m_peers.isEmpty(); }
    int peerCount() const { return m_peers.size(); }

    // `players` is the table size shown to searching devices (2 for the
    // classic game), `maxPeers` the number of guests accepted. Bluetooth is
    // opened alongside the LAN whenever the device has an adapter, so the
    // guests may pick either; a table is only refused when neither works.
    bool startHosting(const QString& hostName, int players, int maxPeers, QString* error);
    void setAcceptingGuests(bool accepting);
    void joinHost(const QString& address);
    // Joins a host over Bluetooth. `address` is a device address, not an IP.
    void joinBluetooth(const QString& address);
    // True while the RFCOMM channel is open for guests.
    bool bluetoothHosting() const;
    // Why it is not, when hosting started without it.
    QString bluetoothError() const { return m_btError; }
    void stop();
    void send(const QVariantMap& message);
    void sendTo(int peer, const QVariantMap& message);
    void dropPeer(int peer);

    // IPv4 addresses for the local network, and global IPv6 addresses under
    // which the device may be reachable from the internet.
    static QStringList localAddresses();
    static QStringList internetAddresses();
    // Trims spaces and the brackets people type around IPv6 addresses.
    static QString normalizeAddress(const QString& address);

signals:
    void peerConnectedChanged();
    void peerJoined(int peer);
    void peerLost(int peer);
    void messageReceived(int peer, const QVariantMap& message);
    void connectionFailed(const QString& reason);

private slots:
    // Named slots rather than lambdas: the MeeGo build (Qt 4.7) connects by
    // signature and cannot connect to lambdas.
    void acceptConnections();
    void answerDiscovery();
    void onPingTimeout();
    void onConnectTimeout();
    void onSocketConnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onPeerReadyRead();
    void onPeerDisconnected();
    void onBtConnection(RfcommSocket* socket);
    void onBtConnected();
    void onBtFailed(const QString& reason);

private:
    struct Peer {
        int id = -1;
        // A QTcpSocket or an RfcommSocket: both carry the same lines.
        QIODevice* socket = nullptr;
        QByteArray buffer;
        qint64 lastSeen = 0;
    };

    Peer* addPeer(QIODevice* socket);
    Peer* findPeer(int id);
    int peerIdFor(const QObject* socket) const;
    void removePeer(int id, bool notify);
    void readPeer(int id);
    void checkIdlePeers();
    static void writeLine(QIODevice* socket, const QVariantMap& message);
    static void deleteWhenDisconnected(QIODevice* socket);

    Role m_role = None;
    QString m_hostName;
    int m_players = 2;
    int m_maxPeers = 1;
    bool m_accepting = true;
    int m_nextPeerId = 0;
    QTcpServer m_server;
    QList<Peer> m_peers;
    QUdpSocket* m_responder = nullptr;
    QTcpSocket* m_pendingSocket = nullptr;
    RfcommServer* m_btServer = nullptr;
    QString m_btError;
    RfcommSocket* m_pendingBt = nullptr;
    QTimer m_pingTimer;
    QTimer m_connectTimer;
};

// Finds hosted games in the local network by UDP broadcast, or asks a single
// address directly.
class LanBrowser : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QString localAddresses READ localAddresses NOTIFY hostsChanged)
    Q_PROPERTY(QString internetAddresses READ internetAddresses NOTIFY hostsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)

public:
    explicit LanBrowser(QObject* parent = nullptr);
    ~LanBrowser() override;

    QVariantList hosts() const { return m_hosts; }
    QString localAddresses() const { return LanSession::localAddresses().join(QStringLiteral(", ")); }
    QString internetAddresses() const { return LanSession::internetAddresses().join(QStringLiteral("\n")); }
    bool searching() const { return m_searching; }

    Q_INVOKABLE void search();
    Q_INVOKABLE void copyToClipboard(const QString& text);

signals:
    void hostsChanged();
    void searchingChanged();

private slots:
    void sendProbes();
    void readReplies();
    void finish();

private:
    bool ensureSocket();

    QUdpSocket* m_socket = nullptr;
    QTimer m_timer;
    QTimer m_finishTimer;
    int m_probesLeft = 0;
    bool m_searching = false;
    bool m_lockHeld = false;
    QVariantList m_hosts;
};
