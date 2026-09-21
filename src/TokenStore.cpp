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
#include "TokenStore.h"

#ifdef SCHACH_HAVE_SAILFISH_SECRETS
#include "SecretsTokenStore.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace schach {

TokenStore::~TokenStore() = default;

// --- the file, platform.md §3.3's minimum ------------------------------------

FileTokenStore::FileTokenStore(const QString& directory)
    : m_directory(directory)
{
}

QString FileTokenStore::path() const
{
    if (m_directory.isEmpty())
        return QString();
    return m_directory + QStringLiteral("/lichess.token");
}

QString FileTokenStore::load()
{
    const QString file = path();
    if (file.isEmpty())
        return QString();
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly))
        return QString();
    // §3.3: "Make sure your application can handle at least 512 characters."
    const QString token = QString::fromLatin1(handle.read(4096)).trimmed();
    handle.close();
    return token;
}

bool FileTokenStore::save(const QString& token)
{
    const QString file = path();
    if (file.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile handle(file);
    if (!handle.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    // The permissions are set before anything is written, so the secret is
    // never readable by anyone else, not even for a moment.
    handle.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    handle.write(token.toLatin1());
    handle.close();
    handle.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

void FileTokenStore::clear()
{
    const QString file = path();
    if (!file.isEmpty())
        QFile::remove(file);
}

QString FileTokenStore::describe() const
{
    return QCoreApplication::translate("schach::TokenStore",
                                       "in einer nur für dich lesbaren Datei");
}

QString MemoryTokenStore::describe() const
{
    return QCoreApplication::translate("schach::TokenStore",
                                       "nur im Arbeitsspeicher, bis die App endet");
}

// --- choosing one ------------------------------------------------------------

TokenStore* makeTokenStore(const QString& directory)
{
#ifdef SCHACH_HAVE_SAILFISH_SECRETS
    SecretsTokenStore* secrets = new SecretsTokenStore;
    if (secrets->available()) {
        // Migration, exactly once: an installation from before this code has
        // its token in the file. Moving it over keeps the user logged in; the
        // file goes away, because leaving a copy of a secret behind after
        // putting it somewhere safer is worse than never having moved it.
        FileTokenStore file(directory);
        const QString carried = file.load();
        if (!carried.isEmpty() && secrets->load().isEmpty()) {
            if (secrets->save(carried))
                file.clear();
        }
        return secrets;
    }
    // The daemon is not answering. platform.md §3.3 allows the file as the
    // fallback, and the settings page says which one is in use — an app that
    // silently downgrades where it keeps a key is lying by omission.
    delete secrets;
#endif
    return new FileTokenStore(directory);
}

} // namespace schach
