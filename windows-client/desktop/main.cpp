#include "window.h"
#include "version.h"
#include "installed_shortcuts.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonDocument>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QSaveFile>
#include <QSettings>

static bool finishInstalledUpdate() {
    const QDir directory(QCoreApplication::applicationDirPath());
    QFile replacement(directory.filePath("uninstall-update.bin"));
    QSettings registration("HKEY_CURRENT_USER\\Software\\Nimbo\\DesktopPreview", QSettings::NativeFormat);
    const QString installed = registration.value("InstallDir").toString();
    if (installed.isEmpty() || QDir(QDir::fromNativeSeparators(installed)) != directory) return false;
    const bool linksReady = repairInstalledShortcuts(QCoreApplication::applicationFilePath(),
        QDir(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)));
    if (!replacement.exists()) return false;
    QSaveFile uninstaller(directory.filePath("Uninstall.exe"));
    if (!replacement.open(QIODevice::ReadOnly) || !uninstaller.open(QIODevice::WriteOnly)) return false;
    const auto bytes = replacement.readAll();
    if (bytes.size() != replacement.size() || !bytes.startsWith("MZ") ||
        uninstaller.write(bytes) != bytes.size() || !uninstaller.commit()) return false;
    QSettings listing("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NimboDesktopPreview", QSettings::NativeFormat);
    listing.setValue("DisplayName", "Nimbo");
    listing.setValue("DisplayVersion", QCoreApplication::applicationVersion());
    listing.setValue("DisplayIcon",directory.filePath("assets/nimbo.ico"));
    listing.setValue("UninstallString","\""+directory.filePath("Uninstall.exe")+"\"");
    if (!linksReady) qWarning("Nimbo could not repair all installed shortcuts");
    replacement.close();
    replacement.remove();
    return true;
}

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("Nimbo"); app.setApplicationName("Nimbo Desktop"); app.setApplicationVersion(NIMBO_DESKTOP_VERSION);
    app.setApplicationDisplayName("Nimbo");
    const bool installedUpdate=finishInstalledUpdate();
    QApplication::setStyle("Fusion");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"data-dir","Profile storage directory","directory"});
    parser.addOption({"screenshot","Save the initial window for visual acceptance","file"});
    parser.addOption({"synthetic-media","Use generated audio and video for local media verification"});
    parser.process(app);
    const auto updateView=qgetenv("NIMBO_UPDATE_VIEW");
    qunsetenv("NIMBO_UPDATE_VIEW");
    DesktopWindow window(parser.value("data-dir").isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation):parser.value("data-dir"),parser.isSet("synthetic-media"),
                         QJsonDocument::fromJson(updateView).object(),updateView.isEmpty() && installedUpdate && parser.isSet("data-dir"));
    if(parser.isSet("screenshot"))window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    if(parser.isSet("screenshot"))QTimer::singleShot(150,&window,[&]{app.exit(window.grab().save(parser.value("screenshot"))?0:1);});
    return app.exec();
}
