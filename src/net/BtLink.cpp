#include "BtLink.h"

#include <QSocketNotifier>
#include <QStringList>
#include <QVariantMap>

#if defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
#  define SCHACH_NO_BLUETOOTH 1
#endif

#ifndef SCHACH_NO_BLUETOOTH
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifndef SCHACH_NO_BLUETOOTH

// From <bits/socket.h> and <bluetooth/bluetooth.h>. Written out so neither
// SDK needs the BlueZ headers, which the Harmattan sysroot does not have.
const int kAfBluetooth = 31;
const int kBtprotoHci = 1;
const int kBtprotoRfcomm = 3;
// HCIGETDEVINFO, _IOR('H', 211, int)
const unsigned long kHciGetDevInfo = 0x800448d3UL;
// The first flag bit of hci_dev_info is HCI_UP.
const unsigned int kHciUp = 1;

// struct sockaddr_rc. The kernel's version is not packed: one byte of padding
// follows the channel, and a connect() with the shorter length is refused
// with EINVAL.
struct SockaddrRc {
    unsigned short family;
    unsigned char bdaddr[6];
    unsigned char channel;
    unsigned char padding;
};

// Bluetooth addresses go over the wire in reverse byte order.
bool parseAddress(const QString& address, unsigned char* out)
{
    const QStringList parts = address.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 6)
        return false;
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const uint byte = parts.at(i).toUInt(&ok, 16);
        if (!ok || byte > 0xff)
            return false;
        out[5 - i] = static_cast<unsigned char>(byte);
    }
    return true;
}

QString formatAddress(const unsigned char* bdaddr)
{
    QString result;
    for (int i = 5; i >= 0; --i) {
        if (!result.isEmpty())
            result += QLatin1Char(':');
        result += QString::fromLatin1("%1").arg(bdaddr[i], 2, 16, QLatin1Char('0')).toUpper();
    }
    return result;
}

// The address and flags of one adapter, straight from the kernel: BlueZ 4 and
// BlueZ 5 answer the same ioctl, and Sailfish does not publish the address in
// sysfs at all.
bool adapterInfo(int device, unsigned char* bdaddr, unsigned int* flags)
{
    const int fd = ::socket(kAfBluetooth, SOCK_RAW, kBtprotoHci);
    if (fd < 0)
        return false;
    // hci_dev_info starts with the device number, an eight-byte name and the
    // address; the kernel writes the whole struct, so the buffer is roomy.
    unsigned char info[256];
    memset(info, 0, sizeof(info));
    info[0] = static_cast<unsigned char>(device & 0xff);
    info[1] = static_cast<unsigned char>((device >> 8) & 0xff);
    const int result = ::ioctl(fd, kHciGetDevInfo, info);
    ::close(fd);
    if (result < 0)
        return false;
    bool empty = true;
    for (int i = 0; i < 6; ++i) {
        if (info[10 + i])
            empty = false;
    }
    if (empty)
        return false;
    memcpy(bdaddr, info + 10, 6);
    unsigned int f = 0;
    memcpy(&f, info + 16, 4);
    if (flags)
        *flags = f;
    return true;
}

// The adapter that is actually up. A device may have more than one, and on
// this Jolla hci0 exists but is down while hci1 answers -- binding outgoing
// connections to the wrong one ends every attempt in "host is unreachable".
bool activeAdapter(unsigned char* bdaddr)
{
    for (int device = 0; device < 4; ++device) {
        unsigned int flags = 0;
        if (adapterInfo(device, bdaddr, &flags) && (flags & kHciUp))
            return true;
    }
    return false;
}

bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

QString errorText(int code)
{
    return QString::fromLocal8Bit(strerror(code));
}

#endif // !SCHACH_NO_BLUETOOTH

} // namespace

// --- Bt --------------------------------------------------------------------------

bool Bt::available()
{
#ifdef SCHACH_NO_BLUETOOTH
    return false;
#else
    unsigned char bdaddr[6];
    return activeAdapter(bdaddr);
#endif
}

QString Bt::localAddress()
{
#ifdef SCHACH_NO_BLUETOOTH
    return QString();
#else
    unsigned char bdaddr[6];
    if (activeAdapter(bdaddr))
        return formatAddress(bdaddr);
    for (int device = 0; device < 4; ++device) {
        if (adapterInfo(device, bdaddr, 0))
            return formatAddress(bdaddr);
    }
    return QString();
#endif
}

QString Bt::normalizeAddress(const QString& address)
{
    QString text = address.trimmed().toUpper();
    text.replace(QLatin1Char('-'), QLatin1Char(':'));
    text.replace(QLatin1Char(' '), QLatin1Char(':'));
    const QStringList parts = text.split(QLatin1Char(':'));
    if (parts.size() != 6)
        return QString();
    QStringList clean;
    for (int i = 0; i < 6; ++i) {
        bool ok = false;
        const uint byte = parts.at(i).toUInt(&ok, 16);
        if (!ok || byte > 0xff)
            return QString();
        clean.append(QString::fromLatin1("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper());
    }
    return clean.join(QString::fromLatin1(":"));
}

// --- RfcommSocket ----------------------------------------------------------------

RfcommSocket::RfcommSocket(QObject* parent)
    : QIODevice(parent)
    , m_fd(-1)
    , m_state(Unconnected)
    , m_readNotifier(0)
    , m_writeNotifier(0)
{
}

RfcommSocket::~RfcommSocket()
{
    blockSignals(true);
    shutdown(false);
}

void RfcommSocket::connectToDevice(const QString& address, int channel)
{
#ifdef SCHACH_NO_BLUETOOTH
    Q_UNUSED(address);
    Q_UNUSED(channel);
    fail(tr("This device has no Bluetooth"));
#else
    shutdown(false);
    unsigned char target[6];
    if (!parseAddress(address, target)) {
        fail(tr("Not a Bluetooth address"));
        return;
    }
    m_fd = ::socket(kAfBluetooth, SOCK_STREAM, kBtprotoRfcomm);
    if (m_fd < 0) {
        fail(errorText(errno));
        return;
    }
    if (!setNonBlocking(m_fd)) {
        const int code = errno;
        shutdown(false);
        fail(errorText(code));
        return;
    }
    // Bind the source to the adapter that is up; see activeAdapter().
    unsigned char local[6];
    if (activeAdapter(local)) {
        SockaddrRc source;
        memset(&source, 0, sizeof(source));
        source.family = static_cast<unsigned short>(kAfBluetooth);
        memcpy(source.bdaddr, local, 6);
        ::bind(m_fd, reinterpret_cast<struct sockaddr*>(&source), sizeof(source));
    }

    SockaddrRc destination;
    memset(&destination, 0, sizeof(destination));
    destination.family = static_cast<unsigned short>(kAfBluetooth);
    memcpy(destination.bdaddr, target, 6);
    destination.channel = static_cast<unsigned char>(channel);

    m_peer = formatAddress(target);
    m_state = Connecting;
    int result;
    do {
        result = ::connect(m_fd, reinterpret_cast<struct sockaddr*>(&destination), sizeof(destination));
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        m_state = Connected;
        setOpenMode(QIODevice::ReadWrite | QIODevice::Unbuffered);
        m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_readNotifier, SIGNAL(activated(int)), this, SLOT(onReadable()));
        emit connected();
        return;
    }
    if (errno != EINPROGRESS) {
        const int code = errno;
        shutdown(false);
        fail(errorText(code));
        return;
    }
    // The connection is being set up; the socket becomes writable when the
    // outcome is known.
    m_writeNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Write, this);
    connect(m_writeNotifier, SIGNAL(activated(int)), this, SLOT(onWritable()));
#endif
}

void RfcommSocket::adopt(int descriptor, const QString& peerAddress)
{
#ifdef SCHACH_NO_BLUETOOTH
    Q_UNUSED(descriptor);
    Q_UNUSED(peerAddress);
#else
    shutdown(false);
    m_fd = descriptor;
    m_peer = peerAddress;
    setNonBlocking(m_fd);
    m_state = Connected;
    setOpenMode(QIODevice::ReadWrite | QIODevice::Unbuffered);
    m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_readNotifier, SIGNAL(activated(int)), this, SLOT(onReadable()));
#endif
}

qint64 RfcommSocket::bytesAvailable() const
{
    return m_in.size() + QIODevice::bytesAvailable();
}

void RfcommSocket::close()
{
    shutdown(true);
    QIODevice::close();
}

void RfcommSocket::abort()
{
    m_out.clear();
    shutdown(false);
}

void RfcommSocket::flush()
{
    writePending();
}

void RfcommSocket::disconnectFromHost()
{
    // Best effort: push out what is queued, then drop the link. RFCOMM has
    // no half-close that the other side would notice differently.
    writePending();
    shutdown(true);
}

qint64 RfcommSocket::readData(char* data, qint64 maxSize)
{
    if (m_in.isEmpty())
        return m_state == Connected ? 0 : -1;
    const int count = static_cast<int>(qMin<qint64>(maxSize, m_in.size()));
    memcpy(data, m_in.constData(), count);
    m_in.remove(0, count);
    return count;
}

qint64 RfcommSocket::writeData(const char* data, qint64 size)
{
    if (m_state == Unconnected)
        return -1;
    m_out.append(data, static_cast<int>(size));
    if (m_state == Connected)
        writePending();
    return size;
}

void RfcommSocket::writePending()
{
#ifndef SCHACH_NO_BLUETOOTH
    if (m_fd < 0 || m_state != Connected)
        return;
    while (!m_out.isEmpty()) {
        const ssize_t written = ::write(m_fd, m_out.constData(), m_out.size());
        if (written > 0) {
            m_out.remove(0, static_cast<int>(written));
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Wait for room: the notifier calls back when there is some.
            if (!m_writeNotifier) {
                m_writeNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Write, this);
                connect(m_writeNotifier, SIGNAL(activated(int)), this, SLOT(onWritable()));
            }
            m_writeNotifier->setEnabled(true);
            return;
        }
        shutdown(true);
        return;
    }
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(false);
#endif
}

void RfcommSocket::onReadable()
{
#ifndef SCHACH_NO_BLUETOOTH
    if (m_fd < 0)
        return;
    bool got = false;
    while (true) {
        char buffer[4096];
        const ssize_t count = ::read(m_fd, buffer, sizeof(buffer));
        if (count > 0) {
            m_in.append(buffer, static_cast<int>(count));
            got = true;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        // 0 is the peer's close, anything else a broken link.
        if (got)
            emit readyRead();
        shutdown(true);
        return;
    }
    if (got)
        emit readyRead();
#endif
}

void RfcommSocket::onWritable()
{
#ifndef SCHACH_NO_BLUETOOTH
    if (m_fd < 0)
        return;
    if (m_state == Connecting) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
            error = errno;
        if (error != 0) {
            const QString reason = errorText(error);
            shutdown(false);
            fail(reason);
            return;
        }
        m_state = Connected;
        setOpenMode(QIODevice::ReadWrite | QIODevice::Unbuffered);
        if (m_writeNotifier)
            m_writeNotifier->setEnabled(false);
        m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
        connect(m_readNotifier, SIGNAL(activated(int)), this, SLOT(onReadable()));
        emit connected();
        writePending();
        return;
    }
    writePending();
#endif
}

void RfcommSocket::fail(const QString& reason)
{
    emit errorOccurred(reason);
}

void RfcommSocket::shutdown(bool notify)
{
    const bool wasConnected = m_state == Connected;
    m_state = Unconnected;
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = 0;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->deleteLater();
        m_writeNotifier = 0;
    }
#ifndef SCHACH_NO_BLUETOOTH
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    setOpenMode(QIODevice::NotOpen);
    if (notify && wasConnected)
        emit disconnected();
}

// --- RfcommServer ----------------------------------------------------------------

RfcommServer::RfcommServer(QObject* parent)
    : QObject(parent)
    , m_fd(-1)
    , m_notifier(0)
{
}

RfcommServer::~RfcommServer()
{
    close();
}

bool RfcommServer::listen(int channel, QString* error)
{
#ifdef SCHACH_NO_BLUETOOTH
    Q_UNUSED(channel);
    if (error)
        *error = tr("This device has no Bluetooth");
    return false;
#else
    close();
    if (!Bt::available()) {
        if (error)
            *error = tr("Bluetooth is switched off");
        return false;
    }
    m_fd = ::socket(kAfBluetooth, SOCK_STREAM, kBtprotoRfcomm);
    if (m_fd < 0) {
        if (error)
            *error = errorText(errno);
        return false;
    }
    setNonBlocking(m_fd);
    // BDADDR_ANY: the channel is bound on every adapter of this device.
    SockaddrRc address;
    memset(&address, 0, sizeof(address));
    address.family = static_cast<unsigned short>(kAfBluetooth);
    address.channel = static_cast<unsigned char>(channel);
    if (::bind(m_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0
        || ::listen(m_fd, 4) < 0) {
        if (error)
            *error = errorText(errno);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onIncoming()));
    return true;
#endif
}

void RfcommServer::close()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = 0;
    }
#ifndef SCHACH_NO_BLUETOOTH
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

void RfcommServer::onIncoming()
{
#ifndef SCHACH_NO_BLUETOOTH
    while (m_fd >= 0) {
        SockaddrRc peer;
        memset(&peer, 0, sizeof(peer));
        socklen_t length = sizeof(peer);
        const int descriptor = ::accept(m_fd, reinterpret_cast<struct sockaddr*>(&peer), &length);
        if (descriptor < 0) {
            if (errno == EINTR)
                continue;
            return; // EAGAIN: nothing more waiting
        }
        RfcommSocket* socket = new RfcommSocket(this);
        socket->adopt(descriptor, formatAddress(peer.bdaddr));
        emit newConnection(socket);
    }
#endif
}

// --- BtDevices -------------------------------------------------------------------

BtDevices::BtDevices(QObject* parent)
    : QObject(parent)
    , m_available(false)
{
    refresh();
}

void BtDevices::refresh()
{
    m_devices.clear();
    m_localName.clear();
    m_localAddress = Bt::localAddress();
    m_available = Bt::available();
#ifndef SCHACH_NO_BLUETOOTH
    if (!readBluez5())
        readBluez4();
#endif
    emit changed();
}

#ifdef SCHACH_NO_BLUETOOTH

bool BtDevices::readBluez5() { return false; }
bool BtDevices::readBluez4() { return false; }

#else

namespace {

void appendDevice(QVariantList* devices, const QString& address, const QString& name, bool connected)
{
    const QString clean = Bt::normalizeAddress(address);
    if (clean.isEmpty())
        return;
    QVariantMap entry;
    entry.insert(QString::fromLatin1("address"), clean);
    entry.insert(QString::fromLatin1("name"), name.isEmpty() ? clean : name);
    entry.insert(QString::fromLatin1("connected"), connected);
    devices->append(entry);
}

} // namespace

// BlueZ 5: one call to the object manager lists adapters and devices.
bool BtDevices::readBluez5()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return false;
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1("org.bluez"),
            QString::fromLatin1("/"), QString::fromLatin1("org.freedesktop.DBus.ObjectManager"),
            QString::fromLatin1("GetManagedObjects"));
    const QDBusMessage reply = bus.call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    const QDBusArgument objects = reply.arguments().first().value<QDBusArgument>();
    bool sawAdapter = false;
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QDBusObjectPath path;
        objects >> path;
        objects.beginMap();
        while (!objects.atEnd()) {
            objects.beginMapEntry();
            QString interface;
            QVariantMap properties;
            objects >> interface >> properties;
            if (interface == QLatin1String("org.bluez.Adapter1")) {
                sawAdapter = true;
                if (properties.value(QString::fromLatin1("Powered")).toBool()) {
                    m_available = true;
                    if (m_localName.isEmpty())
                        m_localName = properties.value(QString::fromLatin1("Alias")).toString();
                    if (m_localAddress.isEmpty())
                        m_localAddress = properties.value(QString::fromLatin1("Address")).toString();
                }
            } else if (interface == QLatin1String("org.bluez.Device1")) {
                sawAdapter = true;
                if (properties.value(QString::fromLatin1("Paired")).toBool()) {
                    appendDevice(&m_devices, properties.value(QString::fromLatin1("Address")).toString(),
                                 properties.value(QString::fromLatin1("Alias")).toString(),
                                 properties.value(QString::fromLatin1("Connected")).toBool());
                }
            }
            objects.endMapEntry();
        }
        objects.endMap();
        objects.endMapEntry();
    }
    objects.endMap();
    return sawAdapter;
}

// BlueZ 4 (Harmattan): Manager -> Adapter -> Device, each with its own call.
bool BtDevices::readBluez4()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return false;
    QDBusMessage call = QDBusMessage::createMethodCall(QString::fromLatin1("org.bluez"),
            QString::fromLatin1("/"), QString::fromLatin1("org.bluez.Manager"),
            QString::fromLatin1("DefaultAdapter"));
    QDBusMessage reply = bus.call(call, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return false;
    const QString adapter = reply.arguments().first().value<QDBusObjectPath>().path();
    if (adapter.isEmpty())
        return false;

    QDBusMessage properties = bus.call(QDBusMessage::createMethodCall(QString::fromLatin1("org.bluez"),
            adapter, QString::fromLatin1("org.bluez.Adapter"), QString::fromLatin1("GetProperties")),
            QDBus::Block, 3000);
    if (properties.type() == QDBusMessage::ReplyMessage && !properties.arguments().isEmpty()) {
        QVariantMap map;
        const QDBusArgument argument = properties.arguments().first().value<QDBusArgument>();
        argument >> map;
        if (m_localName.isEmpty())
            m_localName = map.value(QString::fromLatin1("Name")).toString();
        if (m_localAddress.isEmpty())
            m_localAddress = map.value(QString::fromLatin1("Address")).toString();
        if (map.value(QString::fromLatin1("Powered")).toBool())
            m_available = true;
    }

    QDBusMessage devices = bus.call(QDBusMessage::createMethodCall(QString::fromLatin1("org.bluez"),
            adapter, QString::fromLatin1("org.bluez.Adapter"), QString::fromLatin1("ListDevices")),
            QDBus::Block, 3000);
    if (devices.type() != QDBusMessage::ReplyMessage || devices.arguments().isEmpty())
        return true; // the adapter answered, it simply knows nobody
    QList<QDBusObjectPath> paths;
    const QDBusArgument list = devices.arguments().first().value<QDBusArgument>();
    list >> paths;
    for (int i = 0; i < paths.size(); ++i) {
        QDBusMessage device = bus.call(QDBusMessage::createMethodCall(QString::fromLatin1("org.bluez"),
                paths.at(i).path(), QString::fromLatin1("org.bluez.Device"),
                QString::fromLatin1("GetProperties")), QDBus::Block, 3000);
        if (device.type() != QDBusMessage::ReplyMessage || device.arguments().isEmpty())
            continue;
        QVariantMap map;
        const QDBusArgument argument = device.arguments().first().value<QDBusArgument>();
        argument >> map;
        if (!map.value(QString::fromLatin1("Paired")).toBool())
            continue;
        appendDevice(&m_devices, map.value(QString::fromLatin1("Address")).toString(),
                     map.value(QString::fromLatin1("Alias")).toString().isEmpty()
                         ? map.value(QString::fromLatin1("Name")).toString()
                         : map.value(QString::fromLatin1("Alias")).toString(),
                     map.value(QString::fromLatin1("Connected")).toBool());
    }
    return true;
}

#endif // !SCHACH_NO_BLUETOOTH
