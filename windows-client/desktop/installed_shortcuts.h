#pragma once

#include <QDir>

// Called only for the registered Windows installation.
bool repairInstalledShortcuts(const QString &exe, const QDir &programs, const QDir &desktop);
