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
#ifndef SCHACH_ENGINEPROCESS_H
#define SCHACH_ENGINEPROCESS_H

// One engine, one process, strictly serial requests, always with a time
// budget (platform.md §1.9, docs/design.md §5).
//
// A separate process rather than a linked-in engine because a crash or a hang
// must not take the UI with it, and because UCI is the stable interface while
// Stockfish's internals change with every release. The process is started once
// when a board screen opens and kept for the session: a fork plus 3.4 MB of
// network weights is too expensive to pay per move.
//
// **Never `go infinite`.** Every request carries either a movetime or a node
// count, and a watchdog stops and then kills an engine that does not answer.
// A missing binary is a normal state, not an error: board, repetitions and
// rules work without an engine, only sparring and analysis do not.

#include "core/Uci.h"

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QMetaType>
#include <QTimer>
#include <QVariant>
#include <QVector>

namespace schach {

struct EngineRequest {
    int id = 0;
    QString fen;              // empty = start position
    QStringList moves;        // UCI moves applied to `fen`
    int multipv = 1;
    int movetimeMs = 0;       // exactly one of these two is set
    qint64 nodes = 0;
    QVariant tag;             // passed back untouched, so callers can route results
};

struct EngineResult {
    int id = 0;
    QString fen;
    QVector<core::uciproto::Info> lines;   // one per multipv, index 0 = best
    QString bestMove;
    QVariant tag;
    bool ok = false;
    QString error;

    core::Score score() const
    {
        return lines.isEmpty() ? core::Score() : lines.first().score;
    }
};

class EngineProcess : public QObject
{
    Q_OBJECT

public:
    explicit EngineProcess(QObject* parent = 0);
    ~EngineProcess();

    // All of these must be set before start(); changing them restarts nothing.
    void setEnginePath(const QString& path);
    void setSyzygyPath(const QString& path);
    void setEvalFile(const QString& path);
    void setHashMb(int megabytes);       // platform.md: 16 on a phone
    void setThreads(int threads);        // one; the phone has no spare core

    QString enginePath() const { return m_enginePath; }
    // False when the binary is missing or not executable — the app says so in
    // plain words and stays usable.
    bool available() const;
    bool ready() const { return m_ready; }
    bool busy() const { return m_current.id != 0; }
    int pending() const { return m_queue.size(); }
    QString engineName() const { return m_engineName; }
    QString lastError() const { return m_lastError; }

    bool start();
    void stop();          // sends `quit`, then kills after a grace period
    void cancelAll();     // drops the queue; the request in flight still finishes

    // Enqueue an analysis. Returns the request id, or 0 when no engine is
    // available — callers treat 0 as "no engine", never as a failure to report.
    int analyseMovetime(const QString& fen, const QStringList& moves, int movetimeMs,
                        int multipv = 1, const QVariant& tag = QVariant());
    // teacher.md §2.2 [EMPFEHLUNG]: a fixed node count, so that the diagnosis
    // gives the same answer on a fast and on a slow device.
    int analyseNodes(const QString& fen, const QStringList& moves, qint64 nodes,
                     int multipv = 1, const QVariant& tag = QVariant());

signals:
    void readyChanged();
    void busyChanged();
    void failed(const QString& reason);
    void result(const schach::EngineResult& result);

private slots:
    void onReadyRead();
    void onErrorOccurred(QProcess::ProcessError error);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onWatchdog();

private:
    void send(const QString& line);
    void handleLine(const QString& line);
    void startNext();
    void finishCurrent(bool ok, const QString& error = QString());
    void setReady(bool ready);

    QProcess* m_process;
    QTimer* m_watchdog;
    QString m_enginePath;
    QString m_syzygyPath;
    QString m_evalFile;
    QString m_engineName;
    QString m_lastError;
    QString m_buffer;
    int m_hashMb;
    int m_threads;
    bool m_ready;
    bool m_handshakeDone;
    int m_nextId;

    EngineRequest m_current;
    EngineResult m_partial;
    QQueue<EngineRequest> m_queue;
};

} // namespace schach

Q_DECLARE_METATYPE(schach::EngineResult)

#endif // SCHACH_ENGINEPROCESS_H
