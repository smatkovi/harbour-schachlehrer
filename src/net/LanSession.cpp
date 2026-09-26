#include "LanSession.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUdpSocket>

// Qt for WebAssembly builds QtNetwork without QNetworkInterface, because a
// browser cannot enumerate the machine's interfaces. Every use of the class
// below already has a fallback, so there it is simply left out. QHostAddress
// above has pulled in the feature flags; Qt 4 defines none of them.
#if !defined(QT_FEATURE_networkinterface) || QT_FEATURE_networkinterface == 1
#  define SCHACH_HAVE_NETWORKINTERFACE 1
#  include <QNetworkInterface>
#endif

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace {

// Version 1 is the two-player protocol of the first LAN release; hosts of a
// two-player game still answer it so older apps keep finding them.
const QByteArray kProbeV1 = QByteArrayLiteral("SCHACH-DISCOVER 1");
const QByteArray kProbeV2 = QByteArrayLiteral("SCHACH-DISCOVER 2");
const QByteArray kReplyV1 = QByteArrayLiteral("SCHACH-HOST 1 ");
const QByteArray kReplyV2 = QByteArrayLiteral("SCHACH-HOST 2 ");
const int kMaxBuffer = 256 * 1024;
const int kPingInterval = 10000;
// A peer that has sent nothing (not even a ping) for this long is gone, e.g.
// it left the WLAN. Kept generous because phones briefly stall on wake-up.
const qint64 kIdleTimeout = 45000;
const int kConnectTimeout = 8000;
// Bluetooth first has to page the other device; eight seconds is regularly
// not enough for that, especially when the guest has been idle.
const int kBluetoothConnectTimeout = 25000;

#ifdef Q_OS_ANDROID
// Many Android Wi-Fi drivers drop broadcast packets unless an app holds a
// multicast lock, which would break automatic discovery.
QJniObject s_multicastLock;
int s_multicastUsers = 0;

void acquireMulticastLock()
{
    if (s_multicastUsers++ > 0)
        return;
    QJniObject context(QNativeInterface::QAndroidApplication::context().object());
    QJniObject service = QJniObject::fromString(QStringLiteral("wifi"));
    QJniObject manager = context.callObjectMethod("getSystemService",
            "(Ljava/lang/String;)Ljava/lang/Object;", service.object<jstring>());
    if (!manager.isValid())
        return;
    QJniObject lock = manager.callObjectMethod("createMulticastLock",
            "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
            QJniObject::fromString(QStringLiteral("schachlehrer")).object<jstring>());
    if (!lock.isValid())
        return;
    lock.callMethod<void>("setReferenceCounted", "(Z)V", jboolean(false));
    lock.callMethod<void>("acquire");
    s_multicastLock = lock;
}

void releaseMulticastLock()
{
    if (s_multicastUsers <= 0 || --s_multicastUsers > 0)
        return;
    if (s_multicastLock.isValid())
        s_multicastLock.callMethod<void>("release");
    s_multicastLock = QJniObject();
}
#else
void acquireMulticastLock() {}
void releaseMulticastLock() {}
#endif

// Newer Android versions hide most interface details from apps. Asking the
// kernel which source address it would use for an outside route still works
// and sends no packet.
QHostAddress routedAddress(const QString& outside)
{
    QUdpSocket probe;
    probe.connectToHost(QHostAddress(outside), 53);
    const QHostAddress address = probe.localAddress();
    probe.abort();
    return address;
}

QHostAddress routedLocalAddress()
{
    const QHostAddress address = routedAddress(QStringLiteral("8.8.8.8"));
    if (address.protocol() != QAbstractSocket::IPv4Protocol || address == QHostAddress(QHostAddress::LocalHost))
        return QHostAddress();
    return address;
}

// Global unicast IPv6 (2000::/3), i.e. not link-local, unique-local or loopback.
bool isGlobalIPv6(const QHostAddress& address)
{
    if (address.protocol() != QAbstractSocket::IPv6Protocol)
        return false;
    const Q_IPV6ADDR bytes = address.toIPv6Address();
    return (bytes[0] & 0xe0) == 0x20;
}

QString plainAddress(const QHostAddress& address)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    bool ok = false;
    const quint32 ipv4 = address.toIPv4Address(&ok);
    return ok ? QHostAddress(ipv4).toString() : address.toString();
#else
    return address.toString(); // Qt 4 sockets here are IPv4-only
#endif
}

// Qt 4 has no AnyIPv4; its Any is IPv4-only.
QHostAddress anyIPv4()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    return QHostAddress(QHostAddress::AnyIPv4);
#else
    return QHostAddress(QHostAddress::Any);
#endif
}

// A peer is either a TCP socket or an RFCOMM one. The four calls below are
// the only places where that still shows; everything else writes and reads
// through QIODevice.
void linkAbort(QIODevice* device)
{
    if (QTcpSocket* tcp = qobject_cast<QTcpSocket*>(device))
        tcp->abort();
    else if (RfcommSocket* rfcomm = qobject_cast<RfcommSocket*>(device))
        rfcomm->abort();
}

void linkFlush(QIODevice* device)
{
    if (QTcpSocket* tcp = qobject_cast<QTcpSocket*>(device))
        tcp->flush();
    else if (RfcommSocket* rfcomm = qobject_cast<RfcommSocket*>(device))
        rfcomm->flush();
}

void linkDisconnect(QIODevice* device)
{
    if (QTcpSocket* tcp = qobject_cast<QTcpSocket*>(device))
        tcp->disconnectFromHost();
    else if (RfcommSocket* rfcomm = qobject_cast<RfcommSocket*>(device))
        rfcomm->disconnectFromHost();
}

bool linkIsUnconnected(QIODevice* device)
{
    if (QTcpSocket* tcp = qobject_cast<QTcpSocket*>(device))
        return tcp->state() == QAbstractSocket::UnconnectedState;
    if (RfcommSocket* rfcomm = qobject_cast<RfcommSocket*>(device))
        return rfcomm->isUnconnected();
    return true;
}

} // namespace

// --- LanSession ------------------------------------------------------------------

LanSession::LanSession(QObject* parent)
    : QObject(parent)
{
    m_pingTimer.setInterval(kPingInterval);
    m_connectTimer.setSingleShot(true);

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    connect(&m_server, &QTcpServer::newConnection, this, &LanSession::acceptConnections);
    connect(&m_pingTimer, &QTimer::timeout, this, &LanSession::onPingTimeout);
    connect(&m_connectTimer, &QTimer::timeout, this, &LanSession::onConnectTimeout);
#else
    connect(&m_server, SIGNAL(newConnection()), this, SLOT(acceptConnections()));
    connect(&m_pingTimer, SIGNAL(timeout()), this, SLOT(onPingTimeout()));
    connect(&m_connectTimer, SIGNAL(timeout()), this, SLOT(onConnectTimeout()));
#endif
}

void LanSession::onPingTimeout()
{
    const QByteArray ping = QByteArrayLiteral("{\"t\":\"ping\"}\n");
    for (const Peer& peer : m_peers)
        peer.socket->write(ping);
    checkIdlePeers();
}

void LanSession::onConnectTimeout()
{
    if (m_role != Guest || peerConnected())
        return;
    stop();
    emit connectionFailed(tr("No answer from that address"));
}

LanSession::~LanSession()
{
    // Receivers may already be half-destroyed during application shutdown.
    blockSignals(true);
    stop();
}

bool LanSession::startHosting(const QString& hostName, int players, int maxPeers, QString* error)
{
    stop();
    m_hostName = hostName;
    m_players = players;
    m_maxPeers = maxPeers;
    m_accepting = true;
    const bool onLan = m_server.listen(QHostAddress::Any, GamePort);
    // Bluetooth as well, so a guest without a common network can still sit
    // down. A device without an adapter simply hosts on the LAN only.
    m_btError.clear();
    m_btServer = new RfcommServer(this);
    if (m_btServer->listen(Bt::Channel, &m_btError)) {
        connect(m_btServer, SIGNAL(newConnection(RfcommSocket*)),
                this, SLOT(onBtConnection(RfcommSocket*)));
    } else {
        m_btServer->deleteLater();
        m_btServer = nullptr;
    }
    if (!onLan && !m_btServer) {
        if (error)
            *error = m_server.errorString();
        return false;
    }
    m_role = Host;
    acquireMulticastLock();
    m_pingTimer.start();

    m_responder = new QUdpSocket(this);
    if (m_responder->bind(anyIPv4(), DiscoveryPort,
                          QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
        connect(m_responder, &QUdpSocket::readyRead, this, &LanSession::answerDiscovery);
#else
        connect(m_responder, SIGNAL(readyRead()), this, SLOT(answerDiscovery()));
#endif
    }
    // A failed responder only disables automatic discovery; joining by
    // address still works, so hosting continues.
    return true;
}

void LanSession::setAcceptingGuests(bool accepting)
{
    m_accepting = accepting;
}

bool LanSession::bluetoothHosting() const
{
    return m_btServer && m_btServer->isListening();
}

void LanSession::joinBluetooth(const QString& address)
{
    stop();
    const QString device = Bt::normalizeAddress(address);
    if (device.isEmpty()) {
        emit connectionFailed(tr("Not a Bluetooth address"));
        return;
    }
    m_role = Guest;
    RfcommSocket* socket = new RfcommSocket(this);
    m_pendingBt = socket;
    connect(socket, SIGNAL(connected()), this, SLOT(onBtConnected()));
    connect(socket, SIGNAL(errorOccurred(QString)), this, SLOT(onBtFailed(QString)));
    // Bluetooth takes longer than TCP: the radio may first have to find the
    // other device and set up the link.
    m_connectTimer.start(kBluetoothConnectTimeout);
    socket->connectToDevice(device, Bt::Channel);
}

void LanSession::onBtConnected()
{
    RfcommSocket* socket = m_pendingBt;
    if (!socket || sender() != socket)
        return;
    m_connectTimer.stop();
    m_pendingBt = nullptr;
    m_nextPeerId = 0;
    addPeer(socket);
    m_pingTimer.start();
    emit peerJoined(0);
    emit peerConnectedChanged();
}

void LanSession::onBtFailed(const QString& reason)
{
    if (sender() != m_pendingBt || peerConnected())
        return;
    stop();
    emit connectionFailed(reason);
}

void LanSession::onBtConnection(RfcommSocket* socket)
{
    if (!m_accepting || m_peers.size() >= m_maxPeers) {
        QVariantMap busy;
        busy.insert(QStringLiteral("t"), QStringLiteral("busy"));
        writeLine(socket, busy);
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }
    socket->setParent(this);
    const Peer* peer = addPeer(socket);
    emit peerJoined(peer->id);
    emit peerConnectedChanged();
}

void LanSession::joinHost(const QString& address)
{
    stop();
    m_role = Guest;
    QTcpSocket* socket = new QTcpSocket(this);
    m_pendingSocket = socket;
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket, &QTcpSocket::connected, this, &LanSession::onSocketConnected);
    connect(socket, &QAbstractSocket::errorOccurred, this, &LanSession::onSocketError);
#elif QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    connect(socket, &QTcpSocket::connected, this, &LanSession::onSocketConnected);
    connect(socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
            this, &LanSession::onSocketError);
#else
    connect(socket, SIGNAL(connected()), this, SLOT(onSocketConnected()));
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onSocketError(QAbstractSocket::SocketError)));
#endif
    m_connectTimer.start(kConnectTimeout);
    socket->connectToHost(normalizeAddress(address), GamePort);
}

void LanSession::onSocketConnected()
{
    QTcpSocket* socket = m_pendingSocket;
    if (!socket || sender() != socket)
        return;
    m_connectTimer.stop();
    m_pendingSocket = nullptr;
    m_nextPeerId = 0;
    addPeer(socket);
    m_pingTimer.start();
    emit peerJoined(0);
    emit peerConnectedChanged();
}

void LanSession::onSocketError(QAbstractSocket::SocketError)
{
    if (!m_connectTimer.isActive() || peerConnected())
        return;
    const QAbstractSocket* socket = qobject_cast<QAbstractSocket*>(sender());
    const QString reason = socket ? socket->errorString() : tr("Connection failed");
    stop();
    emit connectionFailed(reason);
}

void LanSession::stop()
{
    const bool hadPeers = peerConnected();
    const bool wasActive = m_role != None;
    m_connectTimer.stop();
    m_pingTimer.stop();
    if (m_pendingSocket) {
        m_pendingSocket->disconnect(this);
        m_pendingSocket->abort();
        m_pendingSocket->deleteLater();
        m_pendingSocket = nullptr;
    }
    if (m_pendingBt) {
        m_pendingBt->disconnect(this);
        m_pendingBt->abort();
        m_pendingBt->deleteLater();
        m_pendingBt = nullptr;
    }
    while (!m_peers.isEmpty()) {
        // Detach first: a synchronous disconnected() during flush() must not
        // re-enter removePeer() or emit peerLost while shutting down.
        const Peer peer = m_peers.takeFirst();
        peer.socket->disconnect(this);
        linkFlush(peer.socket);
        linkAbort(peer.socket);
        peer.socket->deleteLater();
    }
    m_server.close();
    if (m_btServer) {
        m_btServer->close();
        m_btServer->deleteLater();
        m_btServer = nullptr;
    }
    if (m_responder) {
        m_responder->close();
        m_responder->deleteLater();
        m_responder = nullptr;
    }
    if (m_role == Host && wasActive)
        releaseMulticastLock();
    m_role = None;
    if (hadPeers)
        emit peerConnectedChanged();
}

void LanSession::writeLine(QIODevice* socket, const QVariantMap& message)
{
    QByteArray line = QJsonDocument(QJsonObject::fromVariantMap(message)).toJson(QJsonDocument::Compact);
    line.append('\n');
    socket->write(line);
}

void LanSession::send(const QVariantMap& message)
{
    for (const Peer& peer : m_peers)
        writeLine(peer.socket, message);
}

void LanSession::sendTo(int peer, const QVariantMap& message)
{
    if (Peer* target = findPeer(peer))
        writeLine(target->socket, message);
}

void LanSession::dropPeer(int peer)
{
    for (int i = 0; i < m_peers.size(); ++i) {
        if (m_peers[i].id != peer)
            continue;
        // Close gracefully so a last message (e.g. why the peer is dropped)
        // still reaches it.
        QIODevice* socket = m_peers[i].socket;
        m_peers.removeAt(i);
        socket->disconnect(this);
        linkDisconnect(socket);
        deleteWhenDisconnected(socket);
        emit peerConnectedChanged();
        return;
    }
}

QStringList LanSession::localAddresses()
{
    QStringList result;
#ifdef SCHACH_HAVE_NETWORKINTERFACE
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                result.append(entry.ip().toString());
        }
    }
#endif
    if (result.isEmpty()) {
        const QHostAddress routed = routedLocalAddress();
        if (!routed.isNull())
            result.append(routed.toString());
    }
    return result;
}

QStringList LanSession::internetAddresses()
{
    QStringList result;
#ifdef SCHACH_HAVE_NETWORKINTERFACE
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            if (isGlobalIPv6(entry.ip()))
                result.append(entry.ip().toString());
        }
    }
#endif
    const QHostAddress routed = routedAddress(QStringLiteral("2001:4860:4860::8888"));
    if (isGlobalIPv6(routed) && !result.contains(routed.toString()))
        result.prepend(routed.toString()); // the address actually used for outgoing traffic
    while (result.size() > 3)
        result.removeLast();
    return result;
}

QString LanSession::normalizeAddress(const QString& address)
{
    QString result = address.trimmed();
    if (result.startsWith(QLatin1Char('[')) && result.endsWith(QLatin1Char(']')))
        result = result.mid(1, result.size() - 2);
    return result;
}

void LanSession::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (!m_accepting || m_peers.size() >= m_maxPeers) {
            QVariantMap busy;
            busy.insert(QStringLiteral("t"), QStringLiteral("busy"));
            writeLine(socket, busy);
            socket->disconnectFromHost();
            deleteWhenDisconnected(socket);
            continue;
        }
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        const Peer* peer = addPeer(socket);
        emit peerJoined(peer->id);
        emit peerConnectedChanged();
    }
}

LanSession::Peer* LanSession::addPeer(QIODevice* socket)
{
    Peer peer;
    peer.id = m_nextPeerId++;
    peer.socket = socket;
    peer.lastSeen = QDateTime::currentMSecsSinceEpoch();
    if (QTcpSocket* tcp = qobject_cast<QTcpSocket*>(socket))
        tcp->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    // By name, not by member pointer: an RfcommSocket has disconnected() too,
    // and this way the same two lines fit both transports (and Qt 4).
    connect(socket, SIGNAL(readyRead()), this, SLOT(onPeerReadyRead()));
    connect(socket, SIGNAL(disconnected()), this, SLOT(onPeerDisconnected()));
    m_peers.append(peer);
    return &m_peers.last();
}

void LanSession::deleteWhenDisconnected(QIODevice* socket)
{
    connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
    if (linkIsUnconnected(socket))
        socket->deleteLater();
}

int LanSession::peerIdFor(const QObject* socket) const
{
    for (const Peer& peer : m_peers) {
        if (peer.socket == socket)
            return peer.id;
    }
    return -1;
}

void LanSession::onPeerReadyRead()
{
    const int id = peerIdFor(sender());
    if (id >= 0)
        readPeer(id);
}

void LanSession::onPeerDisconnected()
{
    const int id = peerIdFor(sender());
    if (id >= 0)
        removePeer(id, true);
}

LanSession::Peer* LanSession::findPeer(int id)
{
    for (Peer& peer : m_peers) {
        if (peer.id == id)
            return &peer;
    }
    return nullptr;
}

void LanSession::removePeer(int id, bool notify)
{
    for (int i = 0; i < m_peers.size(); ++i) {
        if (m_peers[i].id != id)
            continue;
        QIODevice* socket = m_peers[i].socket;
        m_peers.removeAt(i);
        socket->disconnect(this);
        linkAbort(socket);
        socket->deleteLater();
        if (notify) {
            emit peerLost(id);
            emit peerConnectedChanged();
        }
        return;
    }
}

void LanSession::checkIdlePeers()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<int> idle;
    for (const Peer& peer : m_peers) {
        if (now - peer.lastSeen > kIdleTimeout)
            idle.append(peer.id);
    }
    for (int id : idle)
        removePeer(id, true);
}

void LanSession::readPeer(int id)
{
    Peer* peer = findPeer(id);
    if (!peer)
        return;
    peer->lastSeen = QDateTime::currentMSecsSinceEpoch();
    peer->buffer.append(peer->socket->readAll());
    if (peer->buffer.size() > kMaxBuffer) {
        removePeer(id, true);
        return;
    }
    while (true) {
        peer = findPeer(id); // a handler may have removed it or changed the list
        if (!peer)
            return;
        const int newline = peer->buffer.indexOf('\n');
        if (newline < 0)
            return;
        const QByteArray line = peer->buffer.left(newline);
        peer->buffer.remove(0, newline + 1);
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject())
            continue;
        const QVariantMap message = document.object().toVariantMap();
        if (message.value(QStringLiteral("t")).toString() == QLatin1String("ping"))
            continue;
        emit messageReceived(id, message);
    }
}

void LanSession::answerDiscovery()
{
    while (m_responder && m_responder->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_responder->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_responder->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        const int openSeats = m_accepting ? m_maxPeers - m_peers.size() : 0;
        if (m_role != Host || openSeats <= 0)
            continue;
        datagram = datagram.trimmed();
        if (datagram == kProbeV2) {
            m_responder->writeDatagram(kReplyV2 + QByteArray::number(m_players) + ' '
                                       + QByteArray::number(openSeats) + ' ' + m_hostName.toUtf8(),
                                       sender, senderPort);
        } else if (datagram == kProbeV1 && m_players == 2) {
            m_responder->writeDatagram(kReplyV1 + m_hostName.toUtf8(), sender, senderPort);
        }
    }
}

// --- LanBrowser ------------------------------------------------------------------

LanBrowser::LanBrowser(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(600);
    m_finishTimer.setSingleShot(true);
    m_finishTimer.setInterval(900);
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    connect(&m_timer, &QTimer::timeout, this, &LanBrowser::sendProbes);
    connect(&m_finishTimer, &QTimer::timeout, this, &LanBrowser::finish);
#else
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(sendProbes()));
    connect(&m_finishTimer, SIGNAL(timeout()), this, SLOT(finish()));
#endif
}

LanBrowser::~LanBrowser()
{
    if (m_lockHeld)
        releaseMulticastLock();
}

bool LanBrowser::ensureSocket()
{
    if (m_socket)
        return true;
    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(anyIPv4(), 0)) {
        m_socket->deleteLater();
        m_socket = nullptr;
        return false;
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    connect(m_socket, &QUdpSocket::readyRead, this, &LanBrowser::readReplies);
#else
    connect(m_socket, SIGNAL(readyRead()), this, SLOT(readReplies()));
#endif
    return true;
}

void LanBrowser::copyToClipboard(const QString& text)
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void LanBrowser::search()
{
    m_hosts.clear();
    emit hostsChanged();
    if (!ensureSocket())
        return;
    if (!m_lockHeld) {
        acquireMulticastLock();
        m_lockHeld = true;
    }
    m_finishTimer.stop();
    m_probesLeft = 5;
    if (!m_searching) {
        m_searching = true;
        emit searchingChanged();
    }
    sendProbes();
    m_timer.start();
}

void LanBrowser::sendProbes()
{
    if (!m_socket || m_probesLeft <= 0)
        return;
    --m_probesLeft;
    auto sendTo = [this](const QHostAddress& target) {
        m_socket->writeDatagram(kProbeV2, target, LanSession::DiscoveryPort);
        m_socket->writeDatagram(kProbeV1, target, LanSession::DiscoveryPort);
    };
    {
        sendTo(QHostAddress(QHostAddress::Broadcast));
        bool directed = false;
#ifdef SCHACH_HAVE_NETWORKINTERFACE
        const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface& iface : interfaces) {
            const QNetworkInterface::InterfaceFlags flags = iface.flags();
            if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::CanBroadcast)
                || (flags & QNetworkInterface::IsLoopBack))
                continue;
            const QList<QNetworkAddressEntry> entries = iface.addressEntries();
            for (const QNetworkAddressEntry& entry : entries) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !entry.broadcast().isNull()) {
                    sendTo(entry.broadcast());
                    directed = true;
                }
            }
        }
#endif
        if (!directed) {
            // No interface details available: assume the usual /24 home network.
            const QHostAddress routed = routedLocalAddress();
            if (!routed.isNull())
                sendTo(QHostAddress((routed.toIPv4Address() & 0xffffff00U) | 0xffU));
        }
    }
    if (m_probesLeft <= 0) {
        m_timer.stop();
        m_finishTimer.start(); // leave time for the last answers
    }
}

void LanBrowser::finish()
{
    if (m_lockHeld) {
        releaseMulticastLock();
        m_lockHeld = false;
    }
    if (m_searching) {
        m_searching = false;
        emit searchingChanged();
    }
}

void LanBrowser::readReplies()
{
    const QStringList own = LanSession::localAddresses();
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        int players = 2;
        int openSeats = 1;
        QString name;
        if (datagram.startsWith(kReplyV2)) {
            const QList<QByteArray> parts = datagram.mid(kReplyV2.size()).split(' ');
            if (parts.size() < 3)
                continue;
            players = parts[0].toInt();
            openSeats = parts[1].toInt();
            name = QString::fromUtf8(datagram.mid(kReplyV2.size() + parts[0].size() + parts[1].size() + 2)).trimmed();
        } else if (datagram.startsWith(kReplyV1)) {
            name = QString::fromUtf8(datagram.mid(kReplyV1.size())).trimmed();
        } else {
            continue;
        }
        const QString address = plainAddress(sender);
        if (own.contains(address))
            continue;
        bool known = false;
        for (int i = 0; i < m_hosts.size(); ++i) {
            QVariantMap host = m_hosts[i].toMap();
            if (host.value(QStringLiteral("address")).toString() != address)
                continue;
            known = true;
            // A version 2 answer carries more detail than a version 1 one.
            if (datagram.startsWith(kReplyV2)) {
                host.insert(QStringLiteral("players"), players);
                host.insert(QStringLiteral("openSeats"), openSeats);
                m_hosts[i] = host;
            }
        }
        if (!known) {
            QVariantMap host;
            host.insert(QStringLiteral("address"), address);
            host.insert(QStringLiteral("name"), name.left(32));
            host.insert(QStringLiteral("players"), players);
            host.insert(QStringLiteral("openSeats"), openSeats);
            m_hosts.append(host);
        }
        emit hostsChanged();
    }
}
