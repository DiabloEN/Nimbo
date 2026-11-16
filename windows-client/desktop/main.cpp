#include "window.h"
#include "version.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QStandardPaths>
#include <QSaveFile>
#include <QSettings>

static void finishInstalledUpdate() {
    const QDir directory(QCoreApplication::applicationDirPath());
    QFile replacement(directory.filePath("uninstall-update.bin"));
    if (!replacement.exists()) return;
    QSettings registration("HKEY_CURRENT_USER\\Software\\Nimbo\\DesktopPreview", QSettings::NativeFormat);
    const QString installed = registration.value("InstallDir").toString();
    if (installed.isEmpty() || QDir(installed) != directory) return;
    QSaveFile uninstaller(directory.filePath("Uninstall.exe"));
    if (!replacement.open(QIODevice::ReadOnly) || !uninstaller.open(QIODevice::WriteOnly)) return;
    const auto bytes = replacement.readAll();
    if (bytes.size() != replacement.size() || !bytes.startsWith("MZ") ||
        uninstaller.write(bytes) != bytes.size() || !uninstaller.commit()) return;
    QSettings listing("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NimboDesktopPreview", QSettings::NativeFormat);
    listing.setValue("DisplayName", "Nimbo");
    listing.setValue("DisplayVersion", QCoreApplication::applicationVersion());
    const QDir menu(QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation));
    const QString link = menu.filePath("Nimbo.lnk");
    const auto exe = QCoreApplication::applicationFilePath();
    if (QFileInfo(link).symLinkTarget().compare(exe, Qt::CaseInsensitive) != 0 && !QFile::link(exe, link)) return;
    for (const auto name : {"Nimbo Desktop.lnk", "Nimbo Desktop Preview.lnk"}) {
        const QString old = menu.filePath(name);
        if (QFileInfo(old).symLinkTarget().compare(exe, Qt::CaseInsensitive) == 0) QFile::remove(old);
    }
    replacement.close();
    replacement.remove();
}

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("Nimbo"); app.setApplicationName("Nimbo Desktop"); app.setApplicationVersion(NIMBO_DESKTOP_VERSION);
    app.setApplicationDisplayName("Nimbo");
    finishInstalledUpdate();
    QApplication::setStyle("Fusion");
    QCommandLineParser parser;parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"data-dir","Profile storage directory","directory"});
    parser.addOption({"screenshot","Save the initial window for visual acceptance","file"});
    parser.addOption({"synthetic-media","Use generated audio and video for local media verification"});
    parser.process(app);
    DesktopWindow window(parser.value("data-dir").isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation):parser.value("data-dir"),parser.isSet("synthetic-media"));
    if(parser.isSet("screenshot"))window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    if(parser.isSet("screenshot"))QTimer::singleShot(150,&window,[&]{app.exit(window.grab().save(parser.value("screenshot"))?0:1);});
    return app.exec();
}
