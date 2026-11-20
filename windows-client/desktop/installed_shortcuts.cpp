#include "installed_shortcuts.h"
#include <QFileInfo>
#include <QTemporaryDir>
#include <windows.h>

static QString normalized(const QString &path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}

static bool targetsInstallation(const QString &path, const QString &exe) {
    if (path.isEmpty()) return false;
    const QFileInfo target(normalized(path));
    if (target.fileName() != "nimbo.exe" && target.fileName() != "nimbodesktop.exe") return false;
    const QDir current = QFileInfo(normalized(exe)).dir();
    const QString targetDir = normalized(target.path());
    if (targetDir == normalized(current.path())) return true;
    return current.dirName() == "nimbo" &&
           targetDir == normalized(current.filePath("../nimbo desktop preview"));
}

static bool writeShortcut(const QString &path, const QString &exe) {
    // Construct the replacement first, so failure cannot remove the only working link.
    QTemporaryDir temp(QFileInfo(path).dir().filePath(".nimbo-link-XXXXXX"));
    if (!temp.isValid()) return false;
    const QString generated = temp.filePath("Nimbo.lnk");
    if (!QFile::link(exe, generated)) return false;
    // Replace the .lnk itself. Qt file-writing APIs can follow a Windows shortcut.
    return MoveFileExW(QDir::toNativeSeparators(generated).toStdWString().c_str(),
                       QDir::toNativeSeparators(path).toStdWString().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
}

bool repairInstalledShortcuts(const QString &exe, const QDir &programs, const QDir &desktop) {
    const QString link = programs.filePath("Nimbo.lnk");
    const QString existing = QFileInfo(link).symLinkTarget();
    if (normalized(existing) != normalized(exe)) {
        if (QFileInfo::exists(link) && !targetsInstallation(existing, exe)) return false;
        if (!writeShortcut(link, exe)) return false;
    }
    for (const auto name : {"Nimbo Desktop.lnk", "Nimbo Desktop Preview.lnk"}) {
        const QString old = programs.filePath(name);
        if (targetsInstallation(QFileInfo(old).symLinkTarget(), exe) && !QFile::remove(old)) return false;
    }
    // Keep the user's desktop shortcut name and their choice to have no shortcut there.
    for (const auto name : {"Nimbo.lnk", "Nimbo Desktop.lnk", "Nimbo Desktop Preview.lnk"}) {
        const QString path = desktop.filePath(name);
        const QString target = QFileInfo(path).symLinkTarget();
        if (normalized(target) != normalized(exe) && targetsInstallation(target, exe) &&
            !writeShortcut(path, exe)) return false;
    }
    return true;
}
