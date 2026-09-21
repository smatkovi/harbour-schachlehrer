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
#ifndef SCHACH_TOKENSTORE_H
#define SCHACH_TOKENSTORE_H

// Where the Lichess access token lives.
//
// chess-spec/platform.md §3.3 quotes the Lichess documentation ("Do not
// hardcode tokens in your application's code") and names two acceptable
// places: the Sailfish Secrets store, or *at least* a file with 0600 under
// AppDataLocation. Never QSettings.
//
// The interface exists so that both can be had: Secrets on the device, the
// file as the documented fallback when Secrets is not reachable, and an
// in-memory one in the tests, which must never touch the user's real store.
// The idea and the Secrets backend are taken from salichess
// (https://github.com/van-ess0/salichess, GPL-3.0-or-later) — see
// CREDITS/CODE.md.

#include <QString>

namespace schach {

class TokenStore
{
public:
    virtual ~TokenStore();

    // An empty string means "no token", never an error. Not being logged in
    // is a normal state, not a failure.
    virtual QString load() = 0;
    virtual bool save(const QString& token) = 0;
    virtual void clear() = 0;

    // One German sentence fragment naming the place, for the settings page.
    // The user is entitled to know where their key ended up, especially when
    // it is the fallback and not the encrypted store.
    virtual QString describe() const = 0;

    // True for the encrypted store, false for the file. The settings page says
    // so plainly rather than implying a safety that is not there.
    virtual bool encrypted() const = 0;
};

// A file with 0600 under `directory` — platform.md §3.3's minimum.
class FileTokenStore : public TokenStore
{
public:
    explicit FileTokenStore(const QString& directory);

    QString load() override;
    bool save(const QString& token) override;
    void clear() override;
    QString describe() const override;
    bool encrypted() const override { return false; }

    QString path() const;

private:
    QString m_directory;
};

// Nothing on disk at all. For the tests.
class MemoryTokenStore : public TokenStore
{
public:
    QString load() override { return m_token; }
    bool save(const QString& token) override { m_token = token; return true; }
    void clear() override { m_token.clear(); }
    QString describe() const override;
    bool encrypted() const override { return false; }

private:
    QString m_token;
};

// The store the app should use: Sailfish Secrets when this build has it and
// the daemon answers, the 0600 file otherwise. A token found in the file is
// moved into Secrets once and the file is deleted, so an installation that
// predates this code does not log the user out.
TokenStore* makeTokenStore(const QString& directory);

} // namespace schach

#endif // SCHACH_TOKENSTORE_H
