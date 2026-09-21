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
#include "Themes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace schach {

namespace {

QSet<QString> toSet(const QJsonArray& array)
{
    QSet<QString> out;
    for (int i = 0; i < array.size(); ++i) {
        const QString value = array.at(i).toString();
        if (!value.isEmpty())
            out.insert(value);
    }
    return out;
}

} // namespace

bool Themes::load(const QString& path)
{
    m_order.clear();
    m_byDimension.clear();
    m_bySort.clear();
    m_sentences.clear();
    m_motifs.clear();
    m_default = QStringLiteral("TAK");
    m_error.clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        m_error = QStringLiteral("%1: %2").arg(path, parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();

    const QJsonArray order = root.value(QStringLiteral("dimensionOrder")).toArray();
    const QJsonObject byDimension = root.value(QStringLiteral("dimensionThemes")).toObject();
    for (int i = 0; i < order.size(); ++i) {
        const QString key = order.at(i).toString();
        if (key.isEmpty() || !byDimension.contains(key))
            continue;
        Entry entry;
        entry.key = key;
        entry.themes = toSet(byDimension.value(key).toArray());
        m_order << key;
        m_byDimension.append(entry);
    }

    const QString fallback = root.value(QStringLiteral("defaultDimension")).toString();
    if (!fallback.isEmpty())
        m_default = fallback;
    m_motifs = toSet(root.value(QStringLiteral("motifThemes")).toArray());

    const QJsonObject bySort = root.value(QStringLiteral("sortThemes")).toObject();
    // The order matters and is the file's, not this map's: quiet beats
    // defensive for a move that is both (§6.3).
    const char* kSortOrder[] = { "quiet", "defensive" };
    for (std::size_t i = 0; i < sizeof(kSortOrder) / sizeof(kSortOrder[0]); ++i) {
        Entry entry;
        entry.key = QString::fromLatin1(kSortOrder[i]);
        entry.themes = toSet(bySort.value(entry.key).toArray());
        if (!entry.themes.isEmpty())
            m_bySort.append(entry);
    }

    const QJsonArray sentences = root.value(QStringLiteral("sentences")).toArray();
    for (int i = 0; i < sentences.size(); ++i) {
        const QJsonObject entry = sentences.at(i).toObject();
        const QString theme = entry.value(QStringLiteral("theme")).toString();
        const QString text = entry.value(QStringLiteral("text")).toString();
        if (!theme.isEmpty() && !text.isEmpty())
            m_sentences.append(qMakePair(theme, text));
    }

    if (m_order.isEmpty())
        m_error = QStringLiteral("%1: no dimensions").arg(path);
    return !m_order.isEmpty();
}

core::Dimension Themes::dimensionOf(const QSet<QString>& themes) const
{
    for (int i = 0; i < m_byDimension.size(); ++i) {
        const Entry& entry = m_byDimension.at(i);
        if ((themes & entry.themes).isEmpty())
            continue;
        // A quiet position with no motif in it is the STL question: what is
        // there to *do* here? A quiet move that executes a fork is a TAK item
        // that happens to be quiet. Keeping the two questions apart is what
        // lets §6.3's quota be filled across all six dimensions instead of
        // piling every quiet position into STL.
        if (entry.key == QLatin1String("STL") && !(themes & m_motifs).isEmpty())
            continue;
        return core::dimensionFromKey(entry.key.toStdString());
    }
    return core::dimensionFromKey(m_default.toStdString());
}

QString Themes::sortOf(const QSet<QString>& themes) const
{
    for (int i = 0; i < m_bySort.size(); ++i) {
        if (!(themes & m_bySort.at(i).themes).isEmpty())
            return m_bySort.at(i).key;
    }
    return QStringLiteral("other");
}

QString Themes::sentenceFor(const QSet<QString>& themes) const
{
    for (int i = 0; i < m_sentences.size(); ++i) {
        if (themes.contains(m_sentences.at(i).first))
            return m_sentences.at(i).second;
    }
    return QString();
}

} // namespace schach
