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
// Entry point of the MeeGo Harmattan (Nokia N9 / N950) edition: Qt 4.7 with
// QtQuick 1.1 and com.nokia.meego, against the same engine as the Sailfish
// and Android builds. src/ is never touched; everything that differs lives
// under meego/.
#include <QApplication>
#include <QDeclarativeComponent>
#include <QDeclarativeContext>
#include <QDeclarativeEngine>
#include <QDeclarativeError>
#include <QDeclarativeView>
#include <QDir>
#include <QStandardPaths>
#include <QImage>
#include <QLocale>
#include <QPainter>
#include <QTextCodec>
#include <QTimer>
#include <QTranslator>
#include <QUrl>
#include <QWidget>
#include <cstdio>

#include "TeacherEngine.h"

#include <QDeclarativeItem>
#include <qdeclarative.h>

// Debug aid: with SCHACH_SHOT_DIR set, a PNG of the view is written there
// every 2.5 seconds. The N9 has no other way to look at the UI over ssh.
class ScreenshotTimer : public QObject
{
    Q_OBJECT
public:
    ScreenshotTimer(QWidget* view, const QString& dir)
        : QObject(view), m_view(view), m_dir(dir), m_count(0)
    {
        m_timer.setInterval(2500);
        connect(&m_timer, SIGNAL(timeout()), this, SLOT(shoot()));
        m_timer.start();
    }
private slots:
    void shoot()
    {
        const QString file = QString::fromLatin1("%1/shot-%2.png").arg(m_dir).arg(++m_count);
        QImage image(m_view->size(), QImage::Format_ARGB32);
        image.fill(0xff000000);
        QPainter painter(&image);
        m_view->render(&painter);
        painter.end();
        if (!image.save(file))
            qWarning("screenshot: cannot write %s", qPrintable(file));
    }
private:
    QWidget* m_view;
    QString m_dir;
    QTimer m_timer;
    int m_count;
};

// QtQuick 1.1 has neither "pragma Singleton" nor singletons in a qmldir, but
// the shared QML of qml-common/ refers to Style, Prefs and Theme by those
// bare names several hundred times. Instantiating each one here and putting
// it in the root context gives exactly the same spelling at the QML side,
// which is why none of those references had to change.
static QObject* instantiate(QDeclarativeEngine* engine, const QString& file)
{
    // The component is parented to the engine rather than left on the stack:
    // an object created by a QDeclarativeComponent lives in a context owned by
    // that component, so destroying it tears the context down and every
    // binding in the object silently evaluates to undefined afterwards. That
    // showed up as a white screen with default font sizes, because the QML
    // still found Theme but every property on it read as undefined.
    QDeclarativeComponent* component =
        new QDeclarativeComponent(engine, QUrl::fromLocalFile(file), engine);
    if (component->isError()) {
        const QList<QDeclarativeError> errors = component->errors();
        for (int i = 0; i < errors.size(); ++i)
            std::fprintf(stderr, "%s: %s\n", qPrintable(file), qPrintable(errors[i].toString()));
        return 0;
    }
    QObject* object = component->create(engine->rootContext());
    if (object) {
        object->setParent(engine);
        QDeclarativeEngine::setObjectOwnership(object, QDeclarativeEngine::CppOwnership);
    } else {
        std::fprintf(stderr, "%s: component created no object\n", qPrintable(file));
    }
    return object;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // Without this the QML cannot read teacher.learn: Qt 4 refuses an
    // unregistered QObject* property type.

    // Qt 4 passes untranslated tr()/qsTr() sources through Latin-1, and the
    // German strings are full of umlauts ("Königrufen").
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));

    // Same pair as the Sailfish and Android builds, so a phone that has had
    // both keeps one settings file.
    QCoreApplication::setOrganizationName(QString::fromLatin1("harbour-schachlehrer"));
    QCoreApplication::setApplicationName(QString::fromLatin1("harbour-schachlehrer"));

    // Installed as /opt/harbour-schachlehrer/{bin,qml,assets,translations}.
    QString root = QString::fromLocal8Bit(qgetenv("SCHACH_ROOT"));
    if (root.isEmpty())
        root = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QString::fromLatin1(".."));

    // The catalogue is not decoration here: the interface is German, and Qt 4.7
    // loses an umlaut in a qsTr key, so the keys are ASCII and the real text
    // lives in the German catalogue (meego/german-catalogue.py). A device with
    // no locale set -- this N950 reports none -- would otherwise show the bare
    // keys, so German is the fallback rather than a preference.
    QTranslator translator;
    const QString translationDirectory = root + QString::fromLatin1("/translations");
    if (!translator.load(QString::fromLatin1("harbour-schachlehrer-") + QLocale::system().name(),
                         translationDirectory))
        translator.load(QString::fromLatin1("harbour-schachlehrer-de"), translationDirectory);
    app.installTranslator(&translator);

    // Declared before the view so it outlives every binding to it.
    schach::TeacherEngine engine;

    // Everything the app needs at runtime sits under the install root; the
    // Sailfish build finds the same files through SailfishApp::pathTo().
    const QString dataDirectory =
        QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QDir().mkpath(dataDirectory);
    engine.setPaths(root + QString::fromLatin1("/bin/stockfish"),
                    root + QString::fromLatin1("/syzygy"),
                    root + QString::fromLatin1("/net/nn-37f18f62d772.nnue"),
                    dataDirectory + QString::fromLatin1("/schachlehrer.sqlite"));
    engine.setItemBankPath(root + QString::fromLatin1("/items/placement.json"));
    engine.addItemBankPath(root + QString::fromLatin1("/items/puzzles.json"));
    engine.setThemesPath(root + QString::fromLatin1("/items/themes.json"));

    QDeclarativeView view;
    view.setResizeMode(QDeclarativeView::SizeRootObjectToView);

    QDeclarativeContext* ctx = view.rootContext();
    ctx->setContextProperty(QString::fromLatin1("teacher"), &engine);
    const QString qml = root + QString::fromLatin1("/qml/");
    // They live in qml/context/, not beside the pages: QtQuick 1.1 turns every
    // .qml file in a directory into a type of that name for its neighbours, so
    // a Theme.qml next to the pages would register a *type* called Theme and
    // shadow this context property. Reading a property off a type yields
    // undefined, which is what painted the first run white.
    const QString ctxQml = qml + QString::fromLatin1("context/");
    // Theme first: Style reads it, and Prefs reads neither.
    // Not "Theme": com.nokia.meego puts a Theme of its own into scope, which
    // wins over a context property of that name -- the QML then read every
    // metric off the wrong object and got undefined. Style and Prefs are not
    // affected, so only this one is renamed.
    ctx->setContextProperty(QString::fromLatin1("AppTheme"), instantiate(view.engine(), ctxQml + QString::fromLatin1("Theme.qml")));
    ctx->setContextProperty(QString::fromLatin1("Style"), instantiate(view.engine(), ctxQml + QString::fromLatin1("Style.qml")));
    ctx->setContextProperty(QString::fromLatin1("Prefs"), instantiate(view.engine(), ctxQml + QString::fromLatin1("Prefs.qml")));
    ctx->setContextProperty(QString::fromLatin1("Dimensions"), instantiate(view.engine(), ctxQml + QString::fromLatin1("Dimensions.qml")));
    ctx->setContextProperty(QString::fromLatin1("PieceCode"), instantiate(view.engine(), ctxQml + QString::fromLatin1("PieceCode.qml")));
    ctx->setContextProperty(QString::fromLatin1("MoveList"), instantiate(view.engine(), ctxQml + QString::fromLatin1("MoveList.qml")));

    view.setSource(QUrl::fromLocalFile(qml + QString::fromLatin1("harbour-schachlehrer.qml")));
    if (view.status() == QDeclarativeView::Error) {
        const QList<QDeclarativeError> errors = view.errors();
        for (int i = 0; i < errors.size(); ++i)
            std::fprintf(stderr, "%s\n", qPrintable(errors[i].toString()));
        return 1;
    }

    const QByteArray shotDir = qgetenv("SCHACH_SHOT_DIR");
    if (!shotDir.isEmpty())
        new ScreenshotTimer(&view, QString::fromLocal8Bit(shotDir));

    if (qgetenv("SCHACH_WINDOWED").isEmpty()) {
        view.showFullScreen();
    } else {
        view.resize(480, 854);
        view.setVisible(true);
    }
    return app.exec();
}

#include "main.moc"
