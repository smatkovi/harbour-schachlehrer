#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QVariantList>

class QSocketNotifier;

// Bluetooth for a game against a second device, taken over from
// harbour-snapszer. An RFCOMM link carries exactly the same
// newline-delimited JSON as the TCP link, so LanSession can hold either one
// and nothing above it changes.
//
// No BlueZ library and no SDP: an RFCOMM socket is AF_BLUETOOTH/SOCK_STREAM
// on a fixed channel, which is all that Harmattan's BlueZ 4 and Sailfish's
// BlueZ 5 have in common (harbour-briar's kern/src/bt.rs takes the same way).
// Pairing happens in the system settings; the app only offers what is paired
// already, which is also why it needs no inquiry and no agent.
//
// Android and the browser edition have no AF_BLUETOOTH: there everything
// below compiles to a refusal, and the LAN transport stays the only one.

namespace Bt {

// The RFCOMM channel of this game. Fixed, because without SDP the guest has
// no way to look one up -- so every game picks its own above the channels
// Harmattan hands out to headsets and OBEX. Snapszer holds 21, Seuche 25.
const int Channel = 27;

// True when an adapter exists and is switched on.
bool available();
// This device's own address, empty when there is no adapter.
QString localAddress();
// "40:98:4E:AD:BD:42" -> "40:98:4E:AD:BD:42", tolerating spaces and dashes;
// empty when the text is not an address.
QString normalizeAddress(const QString& address);

} // namespace Bt

// One RFCOMM connection. A QIODevice, so LanSession can write lines into it
// and read them back exactly as from a QTcpSocket.
class RfcommSocket : public QIODevice
{
    Q_OBJECT

public:
    explicit RfcommSocket(QObject* parent = 0);
    ~RfcommSocket();

    // Never blocks: connected() or errorOccurred() follows. RFCOMM can take
    // seconds to answer, and on a phone that would freeze the table.
    void connectToDevice(const QString& address, int channel);
    // Takes over a descriptor from RfcommServer.
    void adopt(int descriptor, const QString& peerAddress);

    QString peerAddress() const { return m_peer; }
    bool isConnected() const { return m_state == Connected; }
    bool isUnconnected() const { return m_state == Unconnected; }

    // QIODevice
    bool isSequential() const { return true; }
    qint64 bytesAvailable() const;
    void close();

    // The handful of QAbstractSocket members LanSession uses, under the same
    // names so both transports can be treated alike.
    void abort();
    void flush();
    void disconnectFromHost();

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& reason);

protected:
    qint64 readData(char* data, qint64 maxSize);
    qint64 writeData(const char* data, qint64 size);

private slots:
    // Named slots rather than lambdas: the MeeGo build (Qt 4.7) connects by
    // signature.
    void onReadable();
    void onWritable();

private:
    enum State { Unconnected, Connecting, Connected };

    void fail(const QString& reason);
    void shutdown(bool notify);
    void writePending();

    int m_fd;
    State m_state;
    QString m_peer;
    QSocketNotifier* m_readNotifier;
    QSocketNotifier* m_writeNotifier;
    QByteArray m_in;
    QByteArray m_out;
};

// Listens on one RFCOMM channel on every local adapter.
class RfcommServer : public QObject
{
    Q_OBJECT

public:
    explicit RfcommServer(QObject* parent = 0);
    ~RfcommServer();

    bool listen(int channel, QString* error);
    void close();
    bool isListening() const { return m_fd >= 0; }

signals:
    void newConnection(RfcommSocket* socket);

private slots:
    void onIncoming();

private:
    int m_fd;
    QSocketNotifier* m_notifier;
};

// The paired devices, for the join list. BlueZ 5 (Sailfish) is asked over
// its object manager, BlueZ 4 (Harmattan) over the older Manager/Adapter
// interfaces; both answer a plain user process.
class BtDevices : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString localAddress READ localAddress NOTIFY changed)
    Q_PROPERTY(QString localName READ localName NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)

public:
    explicit BtDevices(QObject* parent = 0);

    bool available() const { return m_available; }
    QString localAddress() const { return m_localAddress; }
    QString localName() const { return m_localName; }
    QVariantList devices() const { return m_devices; }

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    bool readBluez5();
    bool readBluez4();

    bool m_available;
    QString m_localAddress;
    QString m_localName;
    QVariantList m_devices;
};
