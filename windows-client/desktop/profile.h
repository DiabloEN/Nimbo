#pragma once
#include <QJsonObject>
#include <QUrl>

namespace DesktopProfile {
QUrl origin(QString address);
QJsonObject validate(const QByteArray& json);
void inspectEncrypted(const QByteArray& file);
QJsonObject open(const QByteArray& file, const QString& password);
QByteArray seal(const QJsonObject& profile, const QString& password);
QByteArray randomKey();
QByteArray protect(const QByteArray& plain);
QByteArray unprotect(const QByteArray& cipher);
}
