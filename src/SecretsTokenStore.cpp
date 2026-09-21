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
#include "SecretsTokenStore.h"

// The whole backend is compiled only where the Sailfish Secrets development
// files exist; CMake defines this when pkg-config finds sailfishsecrets. On a
// host build (the tests) the file store is the only backend, and that is fine:
// the tests must not talk to the user's real key store anyway.
#ifdef SCHACH_HAVE_SAILFISH_SECRETS

#include <Secrets/createcollectionrequest.h>
#include <Secrets/deletesecretrequest.h>
#include <Secrets/result.h>
#include <Secrets/secret.h>
#include <Secrets/secretmanager.h>
#include <Secrets/storedsecretrequest.h>
#include <Secrets/storesecretrequest.h>

#include <QCoreApplication>
#include <QtGlobal>

using namespace Sailfish::Secrets;

namespace schach {
namespace {

// Collection names are unique across all apps on the device, and the encrypted
// storage plugin accepts at most 31 alphanumeric characters. This is
// OrganizationName + ApplicationName from the desktop file with the
// punctuation removed: org.smatkovi / harbour-schachlehrer.
const char kCollectionName[] = "orgsmatkovischachlehrer";
const char kSecretName[] = "lichessAccessToken";

Secret::Identifier tokenIdentifier()
{
    return Secret::Identifier(QLatin1String(kSecretName), QLatin1String(kCollectionName),
                              SecretManager::DefaultEncryptedStoragePluginName);
}

bool succeeded(Request& request, const char* what)
{
    request.startRequest();
    request.waitForFinished();
    if (request.result().code() == Result::Succeeded)
        return true;
    qWarning("Sailfish Secrets: %s failed: %s", what,
             qPrintable(request.result().errorMessage()));
    return false;
}

} // namespace

SecretsTokenStore::SecretsTokenStore()
    : m_manager(new SecretManager)
{
}

SecretsTokenStore::~SecretsTokenStore() = default;

bool SecretsTokenStore::available() const
{
    return m_manager->isInitialized();
}

QString SecretsTokenStore::load()
{
    if (!available())
        return QString();

    StoredSecretRequest request;
    request.setManager(m_manager.data());
    request.setIdentifier(tokenIdentifier());
    request.setUserInteractionMode(SecretManager::SystemInteraction);
    request.startRequest();
    request.waitForFinished();
    // No collection and no secret both simply mean "not logged in". That is
    // not an error and must not be reported as one.
    if (request.result().code() != Result::Succeeded)
        return QString();
    return QString::fromUtf8(request.secret().data());
}

bool SecretsTokenStore::save(const QString& token)
{
    if (!available() || !ensureCollection())
        return false;

    Secret secret(tokenIdentifier());
    secret.setType(Secret::TypeBlob);
    secret.setData(token.toUtf8());

    // A collection secret is overwritten in place, so logging in again does
    // not need a delete first.
    StoreSecretRequest request;
    request.setManager(m_manager.data());
    request.setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request.setUserInteractionMode(SecretManager::SystemInteraction);
    request.setSecret(secret);
    return succeeded(request, "storing the token");
}

void SecretsTokenStore::clear()
{
    if (!available())
        return;

    DeleteSecretRequest request;
    request.setManager(m_manager.data());
    request.setIdentifier(tokenIdentifier());
    request.setUserInteractionMode(SecretManager::SystemInteraction);
    request.startRequest();
    request.waitForFinished();   // "not found" is the normal case, not a fault
}

QString SecretsTokenStore::describe() const
{
    return QCoreApplication::translate("schach::SecretsTokenStore",
                                       "im verschlüsselten Schlüsselspeicher von Sailfish OS");
}

bool SecretsTokenStore::ensureCollection()
{
    CreateCollectionRequest request;
    request.setManager(m_manager.data());
    request.setCollectionName(QLatin1String(kCollectionName));
    request.setAccessControlMode(SecretManager::OwnerOnlyMode);
    request.setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request.setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request.setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request.setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request.setUserInteractionMode(SecretManager::SystemInteraction);
    request.startRequest();
    request.waitForFinished();

    if (request.result().code() == Result::Succeeded
            || request.result().errorCode() == Result::CollectionAlreadyExistsError)
        return true;
    qWarning("Sailfish Secrets: creating the collection failed: %s",
             qPrintable(request.result().errorMessage()));
    return false;
}

} // namespace schach

#endif // SCHACH_HAVE_SAILFISH_SECRETS
