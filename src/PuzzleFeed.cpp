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
#include "PuzzleFeed.h"

#include "Lichess.h"
#include "core/Position.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>

namespace schach {

namespace {

// The endpoint hands out at most fifty per request.
const int kBatchSize = 50;
// After this many answers in a row that brought almost nothing, stop asking:
// what the endpoint has to give at this level has been given, and hammering it
// for more is exactly the abuse the API terms ask us not to commit. "Almost
// nothing" and not "nothing", because the per-dimension cap below makes a
// batch that is 90 % discards look like progress while it is really churn.
const int kMaxEmptyRounds = 3;
const int kMinUsefulPerBatch = 5;

// And a pause between batches. Measured: asking for fifty repeatedly without
// one earns a 429 within a few requests. The Lichess client then waits a
// minute and repeats the request (§3.4), so nothing is lost either way — but
// filling a pool nobody is waiting for is not worth being throttled over, and
// the API terms ask for restraint in so many words. Three seconds turns ten
// batches into half a minute of background trickle.
const int kPauseBetweenBatchesMs = 3000;

// No dimension may take more than this share of the pool. Measured on 250
// fetched puzzles without the cap: END 52 %, TAK 25 %, REC 17 %, ERD 4 %,
// SRG 2 %, STL 0 %. The `endgame` theme is broad and wins early in the table,
// so it swallows the pool — and a pool that is half endgame starves exactly
// the selection the placement test does by dimension. The shipped bank is even
// (teacher.md §4.2 stratifies it); this keeps the fetched half from undoing
// that. A generous cap, because the feed cannot ask for a dimension: the batch
// endpoint only serves the `mix` angle.
const double kMaxSharePerDimension = 0.30;

// The five levels, and what they actually deliver. Measured against the live
// endpoint on 2026-09-18, twenty puzzles each, logged out:
//
//     easiest  696 … 998    (median  894)
//     easier  1207 … 1336   (median 1290)
//     normal  1354 … 1616   (median 1563)
//     harder  1551 … 1995   (median 1600)
//     hardest 1842 … 2348   (median 2191)
//
// Asking them in turn is therefore what spreads the pool over the range
// teacher.md §4.2 wants, 600 to 2200. Asking only for "normal" would pile
// everything around 1500 and the placement test would run out of items at both
// ends — which is the defect the shipped bank was rebuilt to cure.
const char* kLevels[] = { "easiest", "easier", "normal", "harder", "hardest" };
const int kLevelCount = 5;

int pieceValue(char piece)
{
    switch (piece) {
    case 'P': return 1;
    case 'N': case 'B': return 3;
    case 'R': return 5;
    case 'Q': return 9;
    default: return 0;
    }
}

int materialOf(const core::Position& position, bool white)
{
    int total = 0;
    for (int square = 0; square < 64; ++square) {
        const char piece = position.pieceAt(square);
        if (piece == ' ')
            continue;
        const bool isWhite = piece >= 'A' && piece <= 'Z';
        if (isWhite != white)
            continue;
        total += pieceValue(isWhite ? piece : static_cast<char>(piece - 'a' + 'A'));
    }
    return total;
}

// A sentence for a puzzle whose themes say nothing we have words for. The same
// rule as in tools/import_lichess_puzzles.py, and the same discipline:
// everything here is *measured* on the board. teacher.md §6.6 forbids a bare
// "falsch" and forbids a number as the answer; it does not permit inventing a
// reason, so this says only what can be checked.
QString computedSentence(const core::Position& start, const QStringList& line)
{
    const bool learnerIsWhite = start.whiteToMove();
    const int before = materialOf(start, learnerIsWhite) - materialOf(start, !learnerIsWhite);

    core::Position probe = start;
    for (int i = 0; i < line.size(); ++i) {
        if (!probe.play(line.at(i).toStdString()))
            return QString();
    }
    if (probe.endReason() == core::EndReason::Checkmate)
        return QObject::tr("Am Ende steht das Matt.");

    const int won = (materialOf(probe, learnerIsWhite) - materialOf(probe, !learnerIsWhite))
            - before;
    if (won >= 9)
        return QObject::tr("Die Folge gewinnt die Dame.");
    if (won >= 5)
        return QObject::tr("Die Folge gewinnt einen Turm.");
    if (won >= 3)
        return QObject::tr("Die Folge gewinnt eine Leichtfigur.");
    if (won >= 1)
        return QObject::tr("Die Folge gewinnt einen Bauern.");
    if (won <= -1) {
        // It gives material away and is still the best move — worth saying,
        // because it is the case a learner distrusts most.
        return QObject::tr("Die Folge gibt Material und ist trotzdem die stärkste.");
    }
    return QObject::tr("Die Folge hält die Stellung; ein anderer Zug gibt etwas her.");
}

} // namespace

PuzzleFeed::PuzzleFeed(Lichess* lichess, QObject* parent)
    : QObject(parent)
    , m_lichess(lichess)
    , m_pause(new QTimer(this))
    , m_target(0)
    , m_level(0)
    , m_emptyRounds(0)
    , m_pending(false)
    , m_allowed(false)
    , m_held(false)
{
    m_pause->setSingleShot(true);
    m_pause->setInterval(kPauseBetweenBatchesMs);
    connect(m_pause, SIGNAL(timeout()), this, SLOT(onPauseOver()));
    if (m_lichess) {
        connect(m_lichess, SIGNAL(jsonArrived(QString, int, QByteArray)),
                this, SLOT(onJsonArrived(QString, int, QByteArray)));
    }
}

void PuzzleFeed::setPaths(const QString& directory, const QString& themesPath)
{
    m_directory = directory;
    m_themesPath = themesPath;
}

QString PuzzleFeed::poolPath() const
{
    if (m_directory.isEmpty())
        return QString();
    return m_directory + QStringLiteral("/items/fetched.json");
}

bool PuzzleFeed::load()
{
    m_items.clear();
    m_ids.clear();

    if (!m_themesPath.isEmpty() && !m_themes.load(m_themesPath)) {
        // Without the table a fetched puzzle cannot be given a dimension, and
        // an item without one would be filed under TAK by accident. Better to
        // fetch nothing and say so.
        m_message = tr("Die Themenliste fehlt in dieser Installation, deshalb hole "
                       "ich keine neuen Aufgaben. Die mitgelieferten reichen zum Üben.");
        emit changed();
        return false;
    }

    const QString path = poolPath();
    if (path.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return true;    // nothing fetched yet is the normal first start
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QJsonArray array = document.isArray()
            ? document.array()
            : document.object().value(QStringLiteral("items")).toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QJsonObject item = array.at(i).toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || m_ids.contains(id))
            continue;
        m_ids.insert(id);
        m_items.append(item);
    }
    emit changed();
    return true;
}

void PuzzleFeed::setTarget(int items)
{
    if (items < 0)
        items = 0;
    if (m_target == items)
        return;
    m_target = items;
    emit changed();
    scheduleRefill();
}

void PuzzleFeed::setAllowed(bool allowed)
{
    if (m_allowed == allowed)
        return;
    m_allowed = allowed;
    if (!m_allowed) {
        m_message.clear();
        m_pause->stop();
    }
    // Saying yes is a fresh start: whatever made it give up last time, the
    // learner has just asked again.
    m_emptyRounds = 0;
    emit changed();
    scheduleRefill();
}

void PuzzleFeed::refill()
{
    if (!m_allowed || m_held || m_pending || !m_lichess)
        return;
    if (m_pause->isActive())
        return;   // the trickle is deliberate, see kPauseBetweenBatchesMs
    if (m_themes.isEmpty())
        return;
    if (m_items.size() >= m_target)
        return;
    if (m_emptyRounds >= kMaxEmptyRounds)
        return;
    requestBatch();
}

void PuzzleFeed::scheduleRefill()
{
    if (!m_allowed || m_held || m_pending || m_pause->isActive())
        return;
    m_pause->start();
}

void PuzzleFeed::setHeld(bool held)
{
    if (m_held == held)
        return;
    m_held = held;
    if (m_held)
        m_pause->stop();
    else
        scheduleRefill();   // the game is over; carry on where it left off
    emit changed();
}

void PuzzleFeed::onPauseOver()
{
    refill();
}

void PuzzleFeed::fetchNow()
{
    m_emptyRounds = 0;
    m_pause->stop();
    m_message.clear();
    emit changed();
    refill();
}

void PuzzleFeed::requestBatch()
{
    const int missing = m_target - m_items.size();
    const int want = missing < kBatchSize ? missing : kBatchSize;
    if (want <= 0)
        return;

    const QString level = QString::fromLatin1(kLevels[m_level % kLevelCount]);
    m_pending = true;
    emit changed();
    m_lichess->fetchJson(QStringLiteral("/api/puzzle/batch/mix?nb=%1&difficulty=%2")
                                 .arg(want).arg(level),
                         QStringLiteral("puzzles"));
}

void PuzzleFeed::onJsonArrived(const QString& tag, int status, const QByteArray& body)
{
    if (tag != QLatin1String("puzzles"))
        return;
    m_pending = false;

    if (status < 200 || status >= 300) {
        // Not an error the learner has to act on: the shipped bank is there
        // and the app is complete without this. A 429 never reaches this
        // point — the client re-queues that request itself and waits a minute
        // (§3.4) — so what lands here is a real refusal, and it is not worth
        // retrying at once.
        m_message = tr("Neue Aufgaben konnte ich gerade nicht holen. "
                       "Die mitgelieferten reichen zum Üben.");
        ++m_emptyRounds;
        emit changed();
        return;
    }
    m_message.clear();

    const QJsonArray puzzles = QJsonDocument::fromJson(body)
            .object().value(QStringLiteral("puzzles")).toArray();

    QHash<QString, int> perDimension;
    for (int i = 0; i < m_items.size(); ++i)
        perDimension[m_items.at(i).value(QStringLiteral("dimension")).toString()] += 1;
    const int cap = qMax(1, int(m_target * kMaxSharePerDimension));

    int added = 0;
    for (int i = 0; i < puzzles.size(); ++i) {
        const QJsonObject item = itemFromPuzzle(puzzles.at(i).toObject());
        if (item.isEmpty())
            continue;
        const QString id = item.value(QStringLiteral("id")).toString();
        if (m_ids.contains(id))
            continue;
        const QString dimension = item.value(QStringLiteral("dimension")).toString();
        if (perDimension.value(dimension) >= cap)
            continue;   // that dimension has had its share
        perDimension[dimension] += 1;
        m_ids.insert(id);
        m_items.append(item);
        ++added;
    }

    // Round-robin over the five levels: each answer moves to the next one, so
    // the pool spreads instead of piling up around the middle.
    m_level = (m_level + 1) % kLevelCount;
    if (added > 0)
        save();
    if (added >= kMinUsefulPerBatch)
        m_emptyRounds = 0;
    else
        ++m_emptyRounds;
    emit changed();
    // The next batch waits: filling a pool nobody is waiting for is not worth
    // being throttled over.
    if (m_items.size() < m_target && m_emptyRounds < kMaxEmptyRounds)
        m_pause->start();
}

void PuzzleFeed::save()
{
    const QString path = poolPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray array;
    for (int i = 0; i < m_items.size(); ++i)
        array.append(m_items.at(i));
    QJsonObject root;
    root.insert(QStringLiteral("items"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
}

void PuzzleFeed::clear()
{
    m_items.clear();
    m_ids.clear();
    m_emptyRounds = 0;
    const QString path = poolPath();
    if (!path.isEmpty())
        QFile::remove(path);
    emit changed();
}

QJsonObject PuzzleFeed::itemFromPuzzle(const QJsonObject& entry) const
{
    const QJsonObject puzzle = entry.value(QStringLiteral("puzzle")).toObject();
    const QJsonObject game = entry.value(QStringLiteral("game")).toObject();

    const QString id = puzzle.value(QStringLiteral("id")).toString();
    const QString pgn = game.value(QStringLiteral("pgn")).toString();
    const int initialPly = puzzle.value(QStringLiteral("initialPly")).toInt(-1);
    const QJsonArray solution = puzzle.value(QStringLiteral("solution")).toArray();
    if (id.isEmpty() || pgn.isEmpty() || initialPly < 0 || solution.isEmpty())
        return QJsonObject();

    // **The trap.** The two Lichess sources disagree about the first move, and
    // the mistake looks almost right either way round:
    //
    //   * the CSV export gives a FEN *before* the opponent's move, and
    //     `Moves[0]` is that opponent move (teacher.md §4.2 quotes it);
    //   * this API gives no FEN at all. The position is the PGN played out for
    //     `initialPly + 1` half moves, and `solution[0]` is already the
    //     learner's move — there is nothing to strip.
    //
    // Verified against the live endpoint: with `initialPly` half moves played,
    // `solution[0]` is not even legal.
    core::Position position;
    // The endpoint sends bare SAN separated by spaces, no move numbers. Those
    // are filtered anyway: `initialPly` counts half moves, so one stray token
    // would shift the whole position by one and the puzzle would still look
    // plausible. Split by hand — the enum that skips empty parts moved from
    // QString to Qt in 5.14 and this app is built against 5.6.
    QStringList sanMoves;
    const QStringList tokens = pgn.split(QLatin1Char(' '));
    for (int i = 0; i < tokens.size(); ++i) {
        const QString token = tokens.at(i).trimmed();
        if (token.isEmpty() || token.contains(QLatin1Char('.')))
            continue;
        if (token == QLatin1String("1-0") || token == QLatin1String("0-1")
                || token == QLatin1String("1/2-1/2") || token == QLatin1String("*"))
            continue;
        sanMoves << token;
    }
    const int plies = initialPly + 1;
    if (sanMoves.size() < plies)
        return QJsonObject();
    for (int i = 0; i < plies; ++i) {
        if (!position.playSan(sanMoves.at(i).toStdString()))
            return QJsonObject();
    }

    QStringList line;
    core::Position probe = position;
    for (int i = 0; i < solution.size(); ++i) {
        const QString move = solution.at(i).toString();
        if (move.isEmpty() || !probe.play(move.toStdString()))
            return QJsonObject();
        line << move;
    }
    // The line has to end on the learner's move, or the last thing they do is
    // watch (§6.5). A trailing opponent move is dropped, as in SolutionLine.
    if ((line.size() % 2) == 0)
        line.removeLast();
    if (line.isEmpty())
        return QJsonObject();

    QSet<QString> themes;
    const QJsonArray themeArray = puzzle.value(QStringLiteral("themes")).toArray();
    for (int i = 0; i < themeArray.size(); ++i)
        themes.insert(themeArray.at(i).toString());

    const int rating = puzzle.value(QStringLiteral("rating")).toInt();
    if (rating <= 0)
        return QJsonObject();

    QString explanation = m_themes.sentenceFor(themes);
    if (explanation.isEmpty())
        explanation = computedSentence(position, line);

    QJsonObject item;
    // Same prefix as the imported bank, so a puzzle that is in both is caught
    // by ItemBank::merge() and not asked twice.
    item.insert(QStringLiteral("id"), QStringLiteral("li-") + id);
    item.insert(QStringLiteral("fen"), QString::fromStdString(position.fen()));
    item.insert(QStringLiteral("solution"), line.first());
    item.insert(QStringLiteral("line"), QJsonArray::fromStringList(line));
    item.insert(QStringLiteral("alsoAccepted"), QJsonArray());
    item.insert(QStringLiteral("dimension"),
                QString::fromLatin1(core::dimensionKey(m_themes.dimensionOf(themes))));
    item.insert(QStringLiteral("difficulty"), rating);
    item.insert(QStringLiteral("explanation"), explanation);
    item.insert(QStringLiteral("source"), QStringLiteral("lichess-api"));
    item.insert(QStringLiteral("sort"), m_themes.sortOf(themes));
    item.insert(QStringLiteral("themes"), themeArray);
    return item;
}

} // namespace schach
