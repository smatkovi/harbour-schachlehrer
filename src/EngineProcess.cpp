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
#include "EngineProcess.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace schach {

namespace {
// How long past its own budget an engine may take before we stop it, and how
// long after `stop` before we kill it. Generous, because a phone under load is
// slow, but finite, because a hung engine must never block the UI.
const int kGraceMs = 4000;
const int kKillMs = 1500;
const int kHandshakeMs = 10000;
} // namespace

EngineProcess::EngineProcess(QObject* parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_watchdog(new QTimer(this))
    , m_hashMb(16)
    , m_threads(1)
    , m_ready(false)
    , m_handshakeDone(false)
    , m_nextId(0)
{
    qRegisterMetaType<schach::EngineResult>("schach::EngineResult");
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_watchdog->setSingleShot(true);

    connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(onReadyRead()));
    connect(m_process, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(onErrorOccurred(QProcess::ProcessError)));
    connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(onFinished(int, QProcess::ExitStatus)));
    connect(m_watchdog, SIGNAL(timeout()), this, SLOT(onWatchdog()));
}

EngineProcess::~EngineProcess()
{
    stop();
}

void EngineProcess::setEnginePath(const QString& path) { m_enginePath = path; }
void EngineProcess::setSyzygyPath(const QString& path) { m_syzygyPath = path; }
void EngineProcess::setEvalFile(const QString& path) { m_evalFile = path; }
void EngineProcess::setHashMb(int megabytes) { m_hashMb = megabytes > 0 ? megabytes : 16; }
void EngineProcess::setThreads(int threads) { m_threads = threads > 0 ? threads : 1; }

bool EngineProcess::available() const
{
    if (m_enginePath.isEmpty())
        return false;
    const QFileInfo info(m_enginePath);
    return info.exists() && info.isFile() && info.isExecutable();
}

void EngineProcess::setReady(bool ready)
{
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged();
}

bool EngineProcess::start()
{
    if (m_process->state() != QProcess::NotRunning)
        return true;
    if (!available()) {
        m_lastError = m_enginePath.isEmpty()
                ? tr("Es ist keine Schach-Engine eingerichtet.")
                : tr("Die Schach-Engine wurde nicht gefunden: %1").arg(m_enginePath);
        emit failed(m_lastError);
        return false;
    }

    m_buffer.clear();
    m_handshakeDone = false;
    m_process->start(m_enginePath, QStringList());
    if (!m_process->waitForStarted(3000)) {
        m_lastError = tr("Die Schach-Engine lässt sich nicht starten.");
        emit failed(m_lastError);
        return false;
    }

    send(QString::fromStdString(core::uciproto::cmdUci()));
    m_watchdog->start(kHandshakeMs);
    return true;
}

void EngineProcess::stop()
{
    m_queue.clear();
    m_watchdog->stop();
    if (m_process->state() == QProcess::NotRunning) {
        setReady(false);
        return;
    }
    send(QString::fromStdString(core::uciproto::cmdQuit()));
    if (!m_process->waitForFinished(kKillMs))
        m_process->kill();
    setReady(false);
}

void EngineProcess::cancelAll()
{
    m_queue.clear();
}

void EngineProcess::send(const QString& line)
{
    if (m_process->state() != QProcess::Running)
        return;
    m_process->write(line.toLatin1());
    m_process->write("\n");
}

int EngineProcess::analyseMovetime(const QString& fen, const QStringList& moves, int movetimeMs,
                                   int multipv, const QVariant& tag)
{
    if (!available())
        return 0;
    EngineRequest request;
    request.id = ++m_nextId;
    request.fen = fen;
    request.moves = moves;
    request.multipv = multipv < 1 ? 1 : multipv;
    request.movetimeMs = movetimeMs < 1 ? 1 : movetimeMs;
    request.tag = tag;
    m_queue.enqueue(request);
    startNext();
    return request.id;
}

int EngineProcess::analyseNodes(const QString& fen, const QStringList& moves, qint64 nodes,
                                int multipv, const QVariant& tag)
{
    if (!available())
        return 0;
    EngineRequest request;
    request.id = ++m_nextId;
    request.fen = fen;
    request.moves = moves;
    request.multipv = multipv < 1 ? 1 : multipv;
    request.nodes = nodes < 1 ? 1 : nodes;
    request.tag = tag;
    m_queue.enqueue(request);
    startNext();
    return request.id;
}

void EngineProcess::startNext()
{
    // Strictly serial: one `go` at a time. UCI has no request ids, so two
    // overlapping searches cannot be told apart.
    if (m_current.id != 0 || m_queue.isEmpty() || !m_ready)
        return;

    m_current = m_queue.dequeue();
    m_partial = EngineResult();
    m_partial.id = m_current.id;
    m_partial.fen = m_current.fen;
    m_partial.tag = m_current.tag;
    m_partial.lines.resize(m_current.multipv);

    send(QString::fromStdString(core::uciproto::cmdSetOption("MultiPV", m_current.multipv)));
    std::vector<std::string> moves;
    for (int i = 0; i < m_current.moves.size(); ++i)
        moves.push_back(m_current.moves.at(i).toStdString());
    send(QString::fromStdString(core::uciproto::cmdPosition(m_current.fen.toStdString(), moves)));

    int budget;
    if (m_current.nodes > 0) {
        send(QString::fromStdString(core::uciproto::cmdGoNodes(m_current.nodes)));
        // A node budget has no wall-clock bound of its own; assume the slowest
        // plausible phone (about 200 knps) and leave room on top.
        budget = static_cast<int>(m_current.nodes / 200) + 2000;
    } else {
        send(QString::fromStdString(core::uciproto::cmdGoMovetime(m_current.movetimeMs)));
        budget = m_current.movetimeMs;
    }
    m_watchdog->start(budget + kGraceMs);
    emit busyChanged();
}

void EngineProcess::onReadyRead()
{
    m_buffer += QString::fromLatin1(m_process->readAllStandardOutput());
    int newline;
    while ((newline = m_buffer.indexOf(QLatin1Char('\n'))) >= 0) {
        QString line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        handleLine(line);
    }
}

void EngineProcess::handleLine(const QString& line)
{
    using namespace core::uciproto;
    const std::string text = line.toStdString();

    switch (classify(text)) {
    case LineKind::Id: {
        std::string key, value;
        if (parseId(text, key, value) && key == "name")
            m_engineName = QString::fromStdString(value);
        break;
    }
    case LineKind::UciOk: {
        // Everything the app needs, once, and nothing that changes per move.
        if (!m_evalFile.isEmpty())
            send(QString::fromStdString(cmdSetOption("EvalFile", m_evalFile.toStdString())));
        if (!m_syzygyPath.isEmpty())
            send(QString::fromStdString(cmdSetOption("SyzygyPath", m_syzygyPath.toStdString())));
        send(QString::fromStdString(cmdSetOption("Threads", m_threads)));
        send(QString::fromStdString(cmdSetOption("Hash", m_hashMb)));
        send(QString::fromStdString(cmdNewGame()));
        send(QString::fromStdString(cmdIsReady()));
        break;
    }
    case LineKind::ReadyOk:
        m_watchdog->stop();
        m_handshakeDone = true;
        setReady(true);
        startNext();
        break;
    case LineKind::Info: {
        if (m_current.id == 0)
            break;
        Info info;
        if (!parseInfo(text, info))
            break;
        // Bound scores are intermediate aspiration-window results and would
        // make the diagnosis flicker; only complete lines count.
        if (info.lowerbound || info.upperbound || info.pv.empty())
            break;
        const int index = info.multipv - 1;
        if (index >= 0 && index < m_partial.lines.size())
            m_partial.lines[index] = info;
        break;
    }
    case LineKind::BestMove: {
        if (m_current.id == 0)
            break;
        BestMove best;
        if (parseBestMove(text, best))
            m_partial.bestMove = QString::fromStdString(best.move);
        if (m_partial.bestMove == QLatin1String("(none)"))
            m_partial.bestMove.clear();
        finishCurrent(true);
        break;
    }
    default:
        break;
    }
}

void EngineProcess::finishCurrent(bool ok, const QString& error)
{
    if (m_current.id == 0)
        return;
    m_watchdog->stop();
    EngineResult done = m_partial;
    done.ok = ok;
    done.error = error;
    // Trim the MultiPV slots the engine never filled (fewer legal moves than
    // requested lines, or a search that was cut short).
    while (!done.lines.isEmpty() && !done.lines.last().score.valid)
        done.lines.removeLast();
    m_current = EngineRequest();
    m_partial = EngineResult();
    emit busyChanged();
    emit result(done);
    startNext();
}

void EngineProcess::onWatchdog()
{
    if (!m_handshakeDone) {
        m_lastError = tr("Die Schach-Engine antwortet nicht.");
        m_process->kill();
        setReady(false);
        emit failed(m_lastError);
        return;
    }
    if (m_current.id == 0)
        return;
    // First ask politely; a well-behaved engine answers `stop` with a
    // `bestmove` within milliseconds.
    send(QString::fromStdString(core::uciproto::cmdStop()));
    if (m_process->waitForReadyRead(kKillMs)) {
        onReadyRead();
        if (m_current.id == 0)
            return;
    }
    m_lastError = tr("Die Schach-Engine hat das Zeitbudget überschritten.");
    finishCurrent(false, m_lastError);
    m_process->kill();
    setReady(false);
    emit failed(m_lastError);
}

void EngineProcess::onErrorOccurred(QProcess::ProcessError error)
{
    switch (error) {
    case QProcess::FailedToStart:
        m_lastError = tr("Die Schach-Engine lässt sich nicht starten.");
        break;
    case QProcess::Crashed:
        m_lastError = tr("Die Schach-Engine ist abgestürzt.");
        break;
    default:
        m_lastError = tr("Die Schach-Engine meldet einen Fehler.");
        break;
    }
    setReady(false);
    if (m_current.id != 0)
        finishCurrent(false, m_lastError);
    emit failed(m_lastError);
}

void EngineProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(status)
    m_watchdog->stop();
    setReady(false);
    if (m_current.id != 0)
        finishCurrent(false, tr("Die Schach-Engine wurde beendet."));
}

} // namespace schach
