// Force-included (-include) into every translation unit of the MeeGo build.
// Qt 4.7 lacks the literal macros the shared code uses everywhere.
#pragma once
#include <QtGlobal>
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#include <QByteArray>
#include <QString>
#ifndef QStringLiteral
#define QStringLiteral(str) QString::fromUtf8(str)
#endif
#ifndef QByteArrayLiteral
#define QByteArrayLiteral(str) QByteArray(str)
#endif
// Q_ENUM is Qt 5. Q_ENUMS does the same registration here; moc 4.7 swallows
// whatever declaration follows it, so it is only safe because a constructor
// and a static constant follow, neither of which moc needs.
#ifndef Q_ENUM
#define Q_ENUM(x) Q_ENUMS(x)
#endif
// QUrl grew its component-formatting enum in Qt 5; the Qt 4 QUrlQuery stand-in
// ignores the argument, so the call sites can name a constant either way.
#define SCHACH_URL_DECODED 0
#define SCHACH_URL_ENCODED 0
// Qt 4.7 has no SHA-256 at all.
#include "qt4sha256.h"
#define SCHACH_SHA256(data) qt4compat::sha256(data)
#endif
