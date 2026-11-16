#include "main_window.h"
#ifdef NIMBO_MEDIA
#include "media_dialog.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPainter>
#include <QSettings>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Nimbo");
    QCoreApplication::setApplicationName("Nimbo");
    QCoreApplication::setApplicationVersion(NIMBO_APP_VERSION);
    QApplication::setStyle("Fusion");
#ifdef Q_OS_WIN
    {
        const auto fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        for (const auto *file : {"segoeui.ttf", "segoeuib.ttf", "seguisb.ttf"})
            QFontDatabase::addApplicationFont(fonts + file);
    }
#endif
    QApplication::setFont(QFont("Segoe UI", 10));

    QCommandLineParser parser;
    parser.setApplicationDescription("Nimbo — native desktop preview");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"screenshot", "Save a window preview and exit.", "path"});
    parser.addOption({"data-dir", "Override the local data directory.", "path"});
    parser.addOption({"benchmark-idle", "Show the idle UI and close after a bounded benchmark interval.", "seconds"});
#ifdef NIMBO_MEDIA
    parser.addOption({"media-dialog", "Open the connection dialog without starting capture."});
#endif
    parser.process(app);

    QString dataDir = parser.value("data-dir");
    if (dataDir.isEmpty()) dataDir = qEnvironmentVariable("NIMBO_DATA_DIR");
    if (dataDir.isEmpty()) dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    dataDir = QDir(dataDir).absolutePath();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDir);

    QPixmap mark(64, 64);
    mark.fill(Qt::transparent);
    {
        QPainter painter(&mark);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#a99bff"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(1, 1, 62, 62), 18, 18);
        painter.setPen(QColor("#14131e"));
        painter.setFont(QFont("Segoe UI", 31, QFont::Bold));
        painter.drawText(mark.rect(), Qt::AlignCenter, "n");
    }
    app.setWindowIcon(QIcon(mark));

    MainWindow window(dataDir);
    if (parser.isSet("screenshot")) window.setAttribute(Qt::WA_DontShowOnScreen);
    else if (auto *screen = window.screen())
        window.resize(window.size().boundedTo(screen->availableGeometry().size() - QSize(40, 60)));
    window.show();
#ifdef NIMBO_MEDIA
    if (parser.isSet("media-dialog")) QTimer::singleShot(0, &window, [&window] { showMediaDialog(&window); });
#endif
    if (parser.isSet("benchmark-idle")) {
        const int seconds = qBound(5, parser.value("benchmark-idle").toInt(), 120);
        QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    }
    if (parser.isSet("screenshot")) {
        const QString path = parser.value("screenshot");
        QTimer::singleShot(900, &window, [&window, path, &app] {
            auto* target = QApplication::activeModalWidget();
            app.exit((target ? target : &window)->grab().save(path) ? 0 : 2);
        });
    }
    return app.exec();
}
