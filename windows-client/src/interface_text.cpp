#include "interface_text.h"
#include <atomic>

namespace UiText {
namespace {
struct Entry { const char* ru; const char* en; };
constexpr Entry entries[]{
#define NIMBO_STRING(key, ru, en) {ru, en},
#include "interface_strings.inc"
#undef NIMBO_STRING
};
std::atomic<bool> english{false};
}

const char* value(Id id) {
    const auto& text = entries[static_cast<int>(id)];
    return english.load(std::memory_order_relaxed) ? text.en : text.ru;
}

QString resolveLanguage(const QString& preference, const QStringList& systemLanguages) {
    if (preference == "ru" || preference == "en") return preference;
    for (const auto& tag : systemLanguages) {
        const auto language = QLocale(tag).language();
        if (language == QLocale::Russian) return "ru";
        if (language == QLocale::English) return "en";
    }
    return "en";
}

bool setLanguage(const QString& preference) {
    const bool next = resolveLanguage(preference, QLocale().uiLanguages()) == "en";
    return english.exchange(next) != next;
}

QLocale locale() {
    return QLocale(english.load() ? QLocale::English : QLocale::Russian);
}

QString knownMessage(const QString& message) {
    if (message.isEmpty()) return message;
    const auto utf8 = message.toUtf8();
    for (const auto& entry : entries)
        if (utf8 == entry.ru || utf8 == entry.en) return QString::fromUtf8(english.load() ? entry.en : entry.ru);
    return message;
}

QString serverMessage(const QString& message) {
    if (!english.load() || message.isEmpty()) return message;
    const auto utf8 = message.toUtf8();
    for (const auto& entry : entries)
        if (utf8 == entry.ru) return QString::fromUtf8(entry.en);
    return message;
}

Id identify(const QString& authoredText) {
    const auto utf8 = authoredText.toUtf8();
    for (int i = 0; i < static_cast<int>(Id::Count); ++i)
        if (utf8 == value(static_cast<Id>(i))) return static_cast<Id>(i);
    return Id::Count;
}
}
