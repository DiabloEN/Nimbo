#pragma once
#include <QLocale>
#include <QStringList>

namespace UiText {
enum class Id {
#define NIMBO_STRING(key, ru, en) key,
#include "interface_strings.inc"
#undef NIMBO_STRING
    Count
};

const char* value(Id id);
QString resolveLanguage(const QString& preference, const QStringList& systemLanguages);
bool setLanguage(const QString& preference);
QLocale locale();
QString knownMessage(const QString& message);
QString serverMessage(const QString& message);
Id identify(const QString& authoredText);

#define NIMBO_STRING(key, ru, en) inline const char* key() { return value(Id::key); }
#include "interface_strings.inc"
#undef NIMBO_STRING
}
