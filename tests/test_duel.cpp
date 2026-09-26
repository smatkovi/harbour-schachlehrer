// Die Partie gegen ein zweites Gerät (src/net/DuelSession.h), beide Seiten in
// einem Prozess über 127.0.0.1. Geprüft wird das Protokoll, nicht der
// Transport: Farbvergabe, Zugweitergabe, das Zurückholen nach einem Zug, der
// nicht passt, und Aufgeben.
//
// Der Bluetooth-Weg benutzt dieselbe LanSession und dieselben Nachrichten; er
// braucht zwei Geräte und steht deshalb nicht hier.
#include "core/Position.h"
#include "net/DuelSession.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>

using schach::core::Position;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FEHLT: %s\n", what);
        ++failures;
    }
}

// Wartet, bis die Bedingung gilt oder die Zeit abgelaufen ist.
template <typename Predicate>
bool waitFor(QCoreApplication& app, Predicate done, int ms = 4000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        app.processEvents(QEventLoop::AllEvents, 10);
        if (done())
            return true;
    }
    return done();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    DuelSession host;
    DuelSession guest;
    Position hostBoard;
    Position guestBoard;

    bool hostStarted = false, guestStarted = false;
    bool hostWhite = false, guestWhite = false;
    QString hostResult, guestResult;
    QObject::connect(&host, &DuelSession::gameStarted, [&](bool white, const QString&) {
        hostStarted = true;
        hostWhite = white;
    });
    QObject::connect(&guest, &DuelSession::gameStarted, [&](bool white, const QString&) {
        guestStarted = true;
        guestWhite = white;
    });
    QObject::connect(&host, &DuelSession::movePlayed, [&](const QString& uci, int) {
        if (!hostBoard.play(uci.toStdString()))
            host.requestSync();
    });
    QObject::connect(&guest, &DuelSession::movePlayed, [&](const QString& uci, int ply) {
        if (ply != int(guestBoard.history().size()) || !guestBoard.play(uci.toStdString()))
            guest.requestSync();
    });
    QObject::connect(&guest, &DuelSession::syncReceived, [&](const QStringList& moves) {
        guestBoard.reset();
        for (const QString& uci : moves)
            guestBoard.play(uci.toStdString());
    });
    QObject::connect(&host, &DuelSession::gameEnded, [&](const QString& result, const QString&) {
        hostResult = result;
    });
    QObject::connect(&guest, &DuelSession::gameEnded, [&](const QString& result, const QString&) {
        guestResult = result;
    });

    QString error;
    check(host.startHosting(QStringLiteral("Gastgeber"), DuelSession::White, &error),
          qPrintable(QStringLiteral("Tisch eröffnen: %1").arg(error)));
    guest.join(QStringLiteral("127.0.0.1"), QStringLiteral("Gast"));

    check(waitFor(app, [&] { return hostStarted && guestStarted; }), "beide Seiten beginnen");
    check(hostWhite, "der Gastgeber hat Weiß genommen");
    check(!guestWhite, "der Gast bekommt Schwarz");

    // Ein paar Züge hin und her, jede Seite spielt ihre eigenen.
    const char* opening[] = { "e2e4", "e7e5", "g1f3", "b8c6" };
    for (int i = 0; i < 4; ++i) {
        DuelSession& mover = (i % 2) == 0 ? host : guest;
        Position& board = (i % 2) == 0 ? hostBoard : guestBoard;
        const int ply = int(board.history().size());
        check(board.play(opening[i]), "der Zug ist regelgerecht");
        mover.sendMove(QString::fromLatin1(opening[i]), ply);
        check(waitFor(app, [&] {
            return int(hostBoard.history().size()) == i + 1
                    && int(guestBoard.history().size()) == i + 1;
        }), "der Zug kommt drüben an");
    }
    check(hostBoard.fen() == guestBoard.fen(), "beide Bretter stehen gleich");

    // Ein Zug mit falscher Nummer: der Gastgeber merkt, dass der Gast aus dem
    // Takt ist, und schickt seine Liste; der Gast baut sie nach.
    guestBoard.reset();                        // verstelltes Brett beim Gast
    guest.sendMove(QStringLiteral("f8c5"), 99); // Nummer passt nicht
    check(waitFor(app, [&] { return int(guestBoard.history().size()) >= 4; }),
          "der Gast holt sich die Liste des Gastgebers");
    check(guestBoard.history().size() == 4, "die nachgebaute Liste ist die des Gastgebers");
    check(guestBoard.fen() == hostBoard.fen(), "danach stehen beide Bretter wieder gleich");

    host.sendResign();
    check(waitFor(app, [&] { return !hostResult.isEmpty() && !guestResult.isEmpty(); }),
          "beide erfahren vom Aufgeben");
    check(hostResult == QLatin1String("0-1"), "wer aufgibt, hat verloren");
    check(guestResult == QLatin1String("0-1"), "und der andere gewonnen");

    host.leave(QString());
    guest.leave(QString());

    std::printf(failures == 0 ? "test_duel: alles in Ordnung\n" : "test_duel: %d Fehler\n", failures);
    return failures == 0 ? 0 : 1;
}
