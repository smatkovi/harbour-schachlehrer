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
#include <QFileInfo>
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
    // The engine binary: /usr/bin first, because Sailjail is documented to be
    // able to refuse an exec from /usr/share (chess-spec/platform.md §7.1), and
    // the packaged path second, so an older installation keeps working.
    QString enginePath = QStringLiteral("/usr/bin/harbour-schachlehrer-engine");
    if (!QFileInfo(enginePath).isExecutable())
        enginePath = SailfishApp::pathTo(QStringLiteral("bin/stockfish")).toLocalFile();

    teacher->setPaths(enginePath,
                      SailfishApp::pathTo(QStringLiteral("syzygy")).toLocalFile(),
                      SailfishApp::pathTo(QStringLiteral("net/nn-37f18f62d772.nnue")).toLocalFile(),
                      dataDirectory + QStringLiteral("/schachlehrer.sqlite"));
    // Two banks, and the second one may simply not be there. The hand-written
    // items carry the German explanations and the quiet quota of teacher.md
    // §6.3; the imported Lichess ones carry the spread, the calibrated
    // difficulties and the multi-move lines of §4.2 and §6.5
    // (tools/import_lichess_puzzles.py builds them).
    teacher->setItemBankPath(
        SailfishApp::pathTo(QStringLiteral("items/placement.json")).toLocalFile());
    teacher->addItemBankPath(
        SailfishApp::pathTo(QStringLiteral("items/puzzles.json")).toLocalFile());
    // And what the app fetched from Lichess on earlier starts, plus the table
    // that lets it fetch more. Both are optional: with no network, no account
    // and no permission the app runs on the two banks above and says nothing
    // about it (teacher.md §0.2).
    teacher->setThemesPath(
        SailfishApp::pathTo(QStringLiteral("items/themes.json")).toLocalFile());
    view->rootContext()->setContextProperty(QStringLiteral("teacher"), teacher);
    // Der Stand einer laufenden Sparringpartie wird nach jedem Zug
    // weggeschrieben; das hier ist der Nachschlag fuer den geordneten Abgang
    // (Brett gedreht, Zug zurueckgenommen und sofort zugemacht).
    QObject::connect(app, &QGuiApplication::aboutToQuit,
                     teacher, &schach::TeacherEngine::saveState);

    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-schachlehrer.qml")));
    view->show();
    return app->exec();
}
