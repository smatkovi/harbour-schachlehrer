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
#include "Database.h"
#include "core/Skill.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace schach {

namespace {
// 2: placement_seen, so that a second placement test does not reopen with the
// same positions as the first (teacher.md §5.2 — a remembered position
// measures memory, not skill). CREATE TABLE IF NOT EXISTS carries an existing
// database over without a migration step.
const int kSchemaVersion = 2;

QString joinInstances(const std::vector<std::string>& list)
{
    QStringList out;
    for (std::size_t i = 0; i < list.size(); ++i)
        out << QString::fromStdString(list[i]);
    return out.join(QLatin1String("\n"));
}

// The solution of a card is a line now (teacher.md §6.5), and it is kept in
// the column that used to hold one move: space-separated UCI. A row written
// before this change holds exactly one move, which reads back as a line of
// length one — so there is no migration, and none can go wrong.
QString joinLine(const std::vector<std::string>& line)
{
    QStringList out;
    for (std::size_t i = 0; i < line.size(); ++i)
        out << QString::fromStdString(line[i]);
    return out.join(QString::fromLatin1(" "));
}

std::vector<std::string> splitLine(const QString& text)
{
    // Split by hand: the enum that says "skip the empty parts" moved from
    // QString to Qt in 5.14, and this app is built against 5.6.
    std::vector<std::string> out;
    const QStringList parts = text.split(QLatin1Char(' '));
    for (int i = 0; i < parts.size(); ++i) {
        if (!parts.at(i).isEmpty())
            out.push_back(parts.at(i).toStdString());
    }
    return out;
}

std::vector<std::string> splitInstances(const QString& text)
{
    std::vector<std::string> out;
    if (text.isEmpty())
        return out;
    const QStringList parts = text.split(QLatin1Char('\n'));
    for (int i = 0; i < parts.size(); ++i)
        out.push_back(parts.at(i).toStdString());
    return out;
}
} // namespace

Database::Database(QObject* parent)
    : QObject(parent)
{
    // Qt 5.6: a QSqlDatabase connection belongs to the thread that opened it,
    // so every Database instance gets its own named connection.
    m_connectionName = QStringLiteral("schach-%1").arg(reinterpret_cast<quintptr>(this));
}

Database::~Database()
{
    close();
}

bool Database::isOpen() const { return m_db.isValid() && m_db.isOpen(); }

bool Database::open(const QString& path)
{
    close();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    if (path.isEmpty()) {
        m_db.setDatabaseName(QStringLiteral(":memory:"));
    } else {
        const QFileInfo info(path);
        QDir().mkpath(info.absolutePath());
        m_db.setDatabaseName(path);
    }
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    // platform.md §5.1.
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return createSchema();
}

void Database::close()
{
    if (m_db.isValid()) {
        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool Database::exec(const QString& sql)
{
    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

int Database::schemaVersion() const
{
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
        return query.value(0).toInt();
    return 0;
}

bool Database::createSchema()
{
    // docs/design.md §6 names the tables; platform.md §5.1 names the columns.
    // `plies` is not in §6 but the analysis needs it, and it is what a later
    // Lichess import writes into.
    const char* kTables[] = {
        "CREATE TABLE IF NOT EXISTS games ("
        " id INTEGER PRIMARY KEY, source TEXT NOT NULL DEFAULT 'local',"
        " ext_id TEXT UNIQUE, played_at INTEGER,"
        " white TEXT, black TEXT, result TEXT,"
        " eco TEXT, opening TEXT, time_control TEXT,"
        " initial_fen TEXT, pgn TEXT)",

        "CREATE TABLE IF NOT EXISTS plies ("
        " game_id INTEGER NOT NULL, ply INTEGER NOT NULL,"
        " san TEXT, uci TEXT, epd TEXT,"
        " eval_cp INTEGER, eval_mate INTEGER,"
        " best_uci TEXT, best_eval_cp INTEGER, clock_ms INTEGER,"
        " PRIMARY KEY (game_id, ply))",

        "CREATE TABLE IF NOT EXISTS findings ("
        " id INTEGER PRIMARY KEY, game_id INTEGER, ply INTEGER,"
        " code TEXT NOT NULL, dimension TEXT, severity INTEGER, delta_w REAL,"
        " fen TEXT, played_uci TEXT, best_uci TEXT, motif TEXT, sentence TEXT)",

        "CREATE TABLE IF NOT EXISTS cards ("
        " id TEXT PRIMARY KEY, dimension TEXT, pattern TEXT, title TEXT,"
        " code TEXT, motif TEXT, origin INTEGER,"
        " seed_fen TEXT, solution_uci TEXT, origin_game INTEGER,"
        " state INTEGER, stability REAL, difficulty REAL, reps INTEGER,"
        " lapses INTEGER, streak INTEGER, due_day INTEGER, last_review_day INTEGER,"
        " interval_days INTEGER, priority INTEGER, transfer_sightings INTEGER,"
        " created_day INTEGER, last_occurrence_day INTEGER, queued_mass REAL,"
        " instances TEXT)",

        "CREATE TABLE IF NOT EXISTS reviews ("
        " id INTEGER PRIMARY KEY, card_id TEXT NOT NULL, reviewed_at INTEGER,"
        " rating INTEGER, elapsed_ms INTEGER, correct INTEGER, used_hint INTEGER)",

        "CREATE TABLE IF NOT EXISTS skills ("
        " id INTEGER PRIMARY KEY, measured_at INTEGER, theta REAL,"
        " tak REAL, srg REAL, rec REAL, endd REAL, stl REAL, erd REAL,"
        " blunder_rate REAL)",

        // Which placement items this learner has already been shown, across
        // all tests. Not part of `reviews`: a placement item is not a card and
        // has no schedule — this table answers one question only, "seen or
        // not", and it has to survive the app being reinstalled over its data.
        "CREATE TABLE IF NOT EXISTS placement_seen ("
        " item_id TEXT PRIMARY KEY, seen_at INTEGER, correct INTEGER)",

        "CREATE INDEX IF NOT EXISTS ix_cards_due ON cards(due_day)",
        "CREATE INDEX IF NOT EXISTS ix_plies_epd ON plies(epd)",
        "CREATE INDEX IF NOT EXISTS ix_findings_game ON findings(game_id)",
        "CREATE INDEX IF NOT EXISTS ix_findings_code ON findings(code)",
    };
    for (std::size_t i = 0; i < sizeof(kTables) / sizeof(kTables[0]); ++i) {
        if (!exec(QString::fromLatin1(kTables[i])))
            return false;
    }
    if (schemaVersion() < kSchemaVersion)
        exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
    return true;
}

qint64 Database::insertGame(const GameRecord& game)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "INSERT INTO games (source, ext_id, played_at, white, black, result, eco, opening,"
            " time_control, initial_fen, pgn)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(game.source);
    query.addBindValue(game.externalId.isEmpty() ? QVariant(QVariant::String) : game.externalId);
    query.addBindValue(game.playedAt);
    query.addBindValue(game.white);
    query.addBindValue(game.black);
    query.addBindValue(game.result);
    query.addBindValue(game.eco);
    query.addBindValue(game.opening);
    query.addBindValue(game.timeControl);
    query.addBindValue(game.initialFen);
    query.addBindValue(game.pgn);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

bool Database::insertPlies(qint64 gameId, const QVector<PlyRecord>& plies)
{
    // One transaction for the whole game: platform.md §5.1 measured the
    // difference between minutes and seconds on a large import.
    m_db.transaction();
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO plies (game_id, ply, san, uci, epd, eval_cp, eval_mate,"
            " best_uci, best_eval_cp, clock_ms) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (int i = 0; i < plies.size(); ++i) {
        const PlyRecord& ply = plies.at(i);
        query.addBindValue(gameId);
        query.addBindValue(ply.ply);
        query.addBindValue(ply.san);
        query.addBindValue(ply.uci);
        query.addBindValue(ply.fenBefore);
        query.addBindValue(ply.hasEval ? QVariant(ply.evalCp) : QVariant(QVariant::Int));
        query.addBindValue(ply.evalMate);
        query.addBindValue(ply.bestUci);
        query.addBindValue(ply.bestEvalCp);
        query.addBindValue(ply.clockMs);
        if (!query.exec()) {
            m_lastError = query.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    return m_db.commit();
}

QVector<GameRecord> Database::recentGames(int limit) const
{
    QVector<GameRecord> out;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "SELECT id, source, ext_id, played_at, white, black, result, eco, opening,"
            " time_control, initial_fen, pgn FROM games ORDER BY played_at DESC, id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        GameRecord game;
        game.id = query.value(0).toLongLong();
        game.source = query.value(1).toString();
        game.externalId = query.value(2).toString();
        game.playedAt = query.value(3).toLongLong();
        game.white = query.value(4).toString();
        game.black = query.value(5).toString();
        game.result = query.value(6).toString();
        game.eco = query.value(7).toString();
        game.opening = query.value(8).toString();
        game.timeControl = query.value(9).toString();
        game.initialFen = query.value(10).toString();
        game.pgn = query.value(11).toString();
        out.append(game);
    }
    return out;
}

qint64 Database::newestPlayedAt(const QString& source) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT MAX(played_at) FROM games WHERE source = ?"));
    query.addBindValue(source);
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toLongLong();
}

bool Database::hasExternalId(const QString& externalId) const
{
    return gameIdOfExternalId(externalId) > 0;
}

qint64 Database::gameIdOfExternalId(const QString& externalId) const
{
    if (externalId.isEmpty())
        return -1;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id FROM games WHERE ext_id = ?"));
    query.addBindValue(externalId);
    if (!query.exec() || !query.next())
        return -1;
    return query.value(0).toLongLong();
}

int Database::gameCountOfSource(const QString& source) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM games WHERE source = ?"));
    query.addBindValue(source);
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

int Database::gameCount() const
{
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM games")) && query.next())
        return query.value(0).toInt();
    return 0;
}

int Database::ownMoveCount() const
{
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT COUNT(*) FROM plies")) && query.next())
        return query.value(0).toInt();
    return 0;
}

qint64 Database::insertFinding(const FindingRecord& finding)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "INSERT INTO findings (game_id, ply, code, dimension, severity, delta_w, fen,"
            " played_uci, best_uci, motif, sentence) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(finding.gameId);
    query.addBindValue(finding.ply);
    query.addBindValue(finding.code);
    query.addBindValue(finding.dimension);
    query.addBindValue(finding.severity);
    query.addBindValue(finding.deltaW);
    query.addBindValue(finding.fen);
    query.addBindValue(finding.playedUci);
    query.addBindValue(finding.bestUci);
    query.addBindValue(finding.motif);
    query.addBindValue(finding.sentence);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

QVector<FindingRecord> Database::findingsOfGame(qint64 gameId) const
{
    QVector<FindingRecord> out;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "SELECT id, game_id, ply, code, dimension, severity, delta_w, fen, played_uci,"
            " best_uci, motif, sentence FROM findings WHERE game_id = ? ORDER BY ply"));
    query.addBindValue(gameId);
    if (!query.exec())
        return out;
    while (query.next()) {
        FindingRecord finding;
        finding.id = query.value(0).toLongLong();
        finding.gameId = query.value(1).toLongLong();
        finding.ply = query.value(2).toInt();
        finding.code = query.value(3).toString();
        finding.dimension = query.value(4).toString();
        finding.severity = query.value(5).toInt();
        finding.deltaW = query.value(6).toDouble();
        finding.fen = query.value(7).toString();
        finding.playedUci = query.value(8).toString();
        finding.bestUci = query.value(9).toString();
        finding.motif = query.value(10).toString();
        finding.sentence = query.value(11).toString();
        out.append(finding);
    }
    return out;
}

QVector<double> Database::errorMassByDimension(int games) const
{
    QVector<double> out(core::kDimensionCount, 0.0);
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "SELECT dimension, SUM(delta_w) FROM findings WHERE game_id IN"
            " (SELECT id FROM games ORDER BY played_at DESC, id DESC LIMIT ?)"
            " GROUP BY dimension"));
    query.addBindValue(games);
    if (!query.exec())
        return out;
    while (query.next()) {
        const core::Dimension dimension =
                core::dimensionFromKey(query.value(0).toString().toStdString());
        out[static_cast<int>(dimension)] += query.value(1).toDouble();
    }
    return out;
}

QVector<ClassTally> Database::findingTallies(int games) const
{
    // findings carry no date of their own; the game does (platform.md §5.1),
    // and that is the resolution the fade-out of core/Routine.h needs.
    QVector<ClassTally> out;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "SELECT f.code, COUNT(*), MAX(g.played_at) FROM findings f"
            " JOIN games g ON g.id = f.game_id WHERE f.game_id IN"
            " (SELECT id FROM games ORDER BY played_at DESC, id DESC LIMIT ?)"
            " GROUP BY f.code"));
    query.addBindValue(games);
    if (!query.exec())
        return out;
    while (query.next()) {
        ClassTally tally;
        tally.code = query.value(0).toString();
        tally.count = query.value(1).toInt();
        tally.lastPlayedAt = query.value(2).toLongLong();
        out.append(tally);
    }
    return out;
}

int Database::blunderCount(int games) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM findings WHERE delta_w >= 18 AND game_id IN"
            " (SELECT id FROM games ORDER BY played_at DESC, id DESC LIMIT ?)"));
    query.addBindValue(games);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

bool Database::upsertCard(const core::Card& card)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO cards (id, dimension, pattern, title, code, motif, origin,"
            " seed_fen, solution_uci, origin_game, state, stability, difficulty, reps, lapses,"
            " streak, due_day, last_review_day, interval_days, priority, transfer_sightings,"
            " created_day, last_occurrence_day, queued_mass, instances)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(QString::fromStdString(card.id));
    query.addBindValue(QString::fromLatin1(core::dimensionKey(card.dimension)));
    query.addBindValue(QString::fromStdString(card.pattern));
    query.addBindValue(QString::fromStdString(card.title));
    query.addBindValue(QString::fromLatin1(core::errorKey(card.errorClass)));
    query.addBindValue(QString::fromLatin1(core::motifKey(card.motif)));
    query.addBindValue(static_cast<int>(card.origin));
    query.addBindValue(QString::fromStdString(card.seedFen));
    query.addBindValue(card.solutionLine.empty()
                       ? QString::fromStdString(card.solutionUci)
                       : joinLine(card.solutionLine));
    query.addBindValue(static_cast<qint64>(card.originGameId));
    query.addBindValue(static_cast<int>(card.srs.state));
    query.addBindValue(card.srs.stability);
    query.addBindValue(card.srs.difficulty);
    query.addBindValue(card.srs.reps);
    query.addBindValue(card.srs.lapses);
    query.addBindValue(card.srs.consecutiveCorrect);
    query.addBindValue(static_cast<qint64>(card.srs.dueDay));
    query.addBindValue(static_cast<qint64>(card.srs.lastReviewDay));
    query.addBindValue(card.srs.intervalDays);
    query.addBindValue(card.priority);
    query.addBindValue(card.transferSightings);
    query.addBindValue(static_cast<qint64>(card.createdDay));
    query.addBindValue(static_cast<qint64>(card.lastOccurrenceDay));
    query.addBindValue(card.queuedMass);
    query.addBindValue(joinInstances(card.shownInstances));
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

namespace {
core::Card cardFromRow(const QSqlQuery& query)
{
    core::Card card;
    card.id = query.value(0).toString().toStdString();
    card.dimension = core::dimensionFromKey(query.value(1).toString().toStdString());
    card.pattern = query.value(2).toString().toStdString();
    card.title = query.value(3).toString().toStdString();
    card.errorClass = core::errorFromKey(query.value(4).toString().toStdString());
    card.motif = core::motifFromKey(query.value(5).toString().toStdString());
    card.origin = static_cast<core::CardOrigin>(query.value(6).toInt());
    card.seedFen = query.value(7).toString().toStdString();
    card.solutionLine = splitLine(query.value(8).toString());
    card.normaliseSolution();
    card.originGameId = query.value(9).toLongLong();
    card.srs.state = static_cast<core::CardState>(query.value(10).toInt());
    card.srs.stability = query.value(11).toDouble();
    card.srs.difficulty = query.value(12).toDouble();
    card.srs.reps = query.value(13).toInt();
    card.srs.lapses = query.value(14).toInt();
    card.srs.consecutiveCorrect = query.value(15).toInt();
    card.srs.dueDay = query.value(16).toLongLong();
    card.srs.lastReviewDay = query.value(17).toLongLong();
    card.srs.intervalDays = query.value(18).toInt();
    card.priority = query.value(19).toInt();
    card.transferSightings = query.value(20).toInt();
    card.createdDay = query.value(21).toLongLong();
    card.lastOccurrenceDay = query.value(22).toLongLong();
    card.queuedMass = query.value(23).toDouble();
    card.shownInstances = splitInstances(query.value(24).toString());
    return card;
}

const char* kCardColumns =
        "id, dimension, pattern, title, code, motif, origin, seed_fen, solution_uci, origin_game,"
        " state, stability, difficulty, reps, lapses, streak, due_day, last_review_day,"
        " interval_days, priority, transfer_sightings, created_day, last_occurrence_day,"
        " queued_mass, instances";
} // namespace

bool Database::loadCard(const QString& id, core::Card& card) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT %1 FROM cards WHERE id = ?")
                          .arg(QString::fromLatin1(kCardColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return false;
    card = cardFromRow(query);
    return true;
}

QVector<core::Card> Database::dueCards(qint64 today, int limit) const
{
    QVector<core::Card> out;
    QSqlQuery query(m_db);
    // teacher.md §5.6: highest priority first, then the most overdue — own
    // errors before library cards.
    query.prepare(QStringLiteral(
                          "SELECT %1 FROM cards WHERE state != %2 AND due_day <= ?"
                          " ORDER BY priority DESC, due_day ASC LIMIT ?")
                          .arg(QString::fromLatin1(kCardColumns))
                          .arg(static_cast<int>(core::CardState::Retired)));
    query.addBindValue(today);
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next())
        out.append(cardFromRow(query));
    return out;
}

int Database::dueCardCount(qint64 today) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM cards WHERE state != %1 AND due_day <= ?")
                          .arg(static_cast<int>(core::CardState::Retired)));
    query.addBindValue(today);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

int Database::backlogCount(qint64 today) const
{
    // The backlog of teacher.md §5.6: cards that were due *before* today.
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM cards WHERE state != %1 AND due_day < ?")
                          .arg(static_cast<int>(core::CardState::Retired)));
    query.addBindValue(today);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

int Database::newCardsCreatedOn(qint64 day) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM cards WHERE created_day = ?"));
    query.addBindValue(day);
    if (query.exec() && query.next())
        return query.value(0).toInt();
    return 0;
}

bool Database::recordReview(const QString& cardId, qint64 at, core::Rating rating, int milliseconds,
                            bool correct, bool usedHint)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "INSERT INTO reviews (card_id, reviewed_at, rating, elapsed_ms, correct, used_hint)"
            " VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(cardId);
    query.addBindValue(at);
    query.addBindValue(static_cast<int>(rating));
    query.addBindValue(milliseconds);
    query.addBindValue(correct ? 1 : 0);
    query.addBindValue(usedHint ? 1 : 0);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::recordSkill(qint64 at, double theta, const QVector<double>& thetaPerDimension,
                           double blunderRate)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
            "INSERT INTO skills (measured_at, theta, tak, srg, rec, endd, stl, erd, blunder_rate)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(at);
    query.addBindValue(theta);
    for (int i = 0; i < core::kDimensionCount; ++i)
        query.addBindValue(i < thetaPerDimension.size() ? thetaPerDimension.at(i) : theta);
    query.addBindValue(blunderRate);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

bool Database::latestSkill(double& theta, QVector<double>& thetaPerDimension) const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
                "SELECT theta, tak, srg, rec, endd, stl, erd FROM skills"
                " ORDER BY measured_at DESC, id DESC LIMIT 1"))
        || !query.next())
        return false;
    theta = query.value(0).toDouble();
    thetaPerDimension.clear();
    for (int i = 0; i < core::kDimensionCount; ++i)
        thetaPerDimension.append(query.value(1 + i).toDouble());
    return true;
}

// --- placement items already seen --------------------------------------------

bool Database::rememberPlacementItem(const QString& itemId, qint64 at, bool correct)
{
    if (itemId.isEmpty())
        return false;
    QSqlQuery query(m_db);
    // REPLACE and not IGNORE: seeing an item again should move its date, so
    // that a later "oldest first" rule has something to work with.
    query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO placement_seen (item_id, seen_at, correct)"
            " VALUES (?, ?, ?)"));
    query.addBindValue(itemId);
    query.addBindValue(at);
    query.addBindValue(correct ? 1 : 0);
    if (!query.exec()) {
        m_lastError = query.lastError().text();
        return false;
    }
    return true;
}

QStringList Database::placementItemsSeen() const
{
    QStringList out;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT item_id FROM placement_seen")))
        return out;
    while (query.next())
        out << query.value(0).toString();
    return out;
}

qint64 Database::lastMeasuredAt() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral(
                "SELECT measured_at FROM skills ORDER BY measured_at DESC, id DESC LIMIT 1"))
        || !query.next())
        return 0;
    return query.value(0).toLongLong();
}

} // namespace schach
