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
#include <sailfishapp.h>

#include <QDir>
#include <QGuiApplication>
#include <QLocale>
#include <QStandardPaths>
#include <QQmlContext>
#include <QQuickView>
#include <QTranslator>

#include "TeacherEngine.h"

// Sailfish OS entry point. The engine binary and the tablebases are data the
// app reads at runtime, so their paths are resolved through SailfishApp rather
// than compiled in (docs/design.md §5).
int main(int argc, char *argv[])
{
    QGuiApplication *app = SailfishApp::application(argc, argv);

    // Same names as [X-Sailjail] in the desktop file, so the database and the
    // settings land in the private path Sailjail grants us.
    QCoreApplication::setOrganizationName(QStringLiteral("org.smatkovi"));
    QCoreApplication::setApplicationName(QStringLiteral("harbour-schachlehrer"));

    QQuickView *view = SailfishApp::createView();

    QTranslator translator;
    const QString translationDirectory = SailfishApp::pathTo(
        QStringLiteral("translations")).toLocalFile();
    if (translator.load(QLocale(), QStringLiteral("harbour-schachlehrer"),
                        QStringLiteral("-"), translationDirectory)) {
        app->installTranslator(&translator);
    }

    schach::TeacherEngine *teacher = new schach::TeacherEngine(app);
    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDirectory);
    teacher->setPaths(SailfishApp::pathTo(QStringLiteral("bin/stockfish")).toLocalFile(),
                      SailfishApp::pathTo(QStringLiteral("syzygy")).toLocalFile(),
                      SailfishApp::pathTo(QStringLiteral("net/nn-37f18f62d772.nnue")).toLocalFile(),
                      dataDirectory + QStringLiteral("/schachlehrer.sqlite"));
    teacher->setItemBankPath(
        SailfishApp::pathTo(QStringLiteral("items/placement.json")).toLocalFile());
    view->rootContext()->setContextProperty(QStringLiteral("teacher"), teacher);

    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-schachlehrer.qml")));
    view->show();
    return app->exec();
}
