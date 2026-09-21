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
#ifndef SCHACH_SECRETSTOKENSTORE_H
#define SCHACH_SECRETSTOKENSTORE_H

// The Lichess token in the Sailfish OS Secrets daemon: an owner-only
// collection in the default encrypted storage plugin, unlocked together with
// the device. Needs the Sailjail permission "Secrets" in the desktop file.
//
// After salichess (https://github.com/van-ess0/salichess, GPL-3.0-or-later),
// src/core/secretstokenstore.cpp — see CREDITS/CODE.md.
//
// The requests are synchronous (waitForFinished()). That blocks the UI thread,
// which is only acceptable because it happens exactly three times in the life
// of an installation: at start, at login and at logout.

#include "TokenStore.h"

#include <QScopedPointer>

namespace Sailfish { namespace Secrets { class SecretManager; } }

namespace schach {

class SecretsTokenStore : public TokenStore
{
public:
    SecretsTokenStore();
    ~SecretsTokenStore() override;

    // False when the daemon does not answer. The caller then falls back to
    // the file instead of leaving the user unable to log in at all.
    bool available() const;

    QString load() override;
    bool save(const QString& token) override;
    void clear() override;
    QString describe() const override;
    bool encrypted() const override { return true; }

private:
    bool ensureCollection();

    QScopedPointer<Sailfish::Secrets::SecretManager> m_manager;
};

} // namespace schach

#endif // SCHACH_SECRETSTOKENSTORE_H
