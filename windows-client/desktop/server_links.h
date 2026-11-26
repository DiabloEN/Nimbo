#pragma once
#include "profile.h"
#include <QLineEdit>
#include <QRegularExpression>
#include <QStringDecoder>
#include <memory>
#include <optional>

namespace ServerLinks {
struct Link {
    QUrl origin;
    QString code;
};

inline QString invitation(const QUrl& origin,const QString& code) {
    return origin.toString()+"/#/invite/"+QString::fromLatin1(QUrl::toPercentEncoding(code,"!'()*"));
}
inline QString returnLink(const QUrl& origin) { return origin.toString(); }

inline std::optional<Link> read(const QString& text) {
    static const QRegularExpression space(QStringLiteral("\\s+"),QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression invalidCode(QStringLiteral("[\\x00-\\x1f\\x7f]"));
    static const QRegularExpression invalidPercent(QStringLiteral("%(?![A-Fa-f0-9]{2})"));
    for(const auto& token:text.trimmed().split(space,Qt::SkipEmptyParts)){
        if(!token.startsWith("https://",Qt::CaseInsensitive) && !token.startsWith("http://",Qt::CaseInsensitive))continue;
        const auto hash=token.indexOf('#');
        if(hash<0)continue;
        const auto origin=DesktopProfile::origin(token.left(hash));
        if(origin.isEmpty())continue;
        const auto route=token.mid(hash+1);
        if(route=="/server")return Link{origin,{}};
        if(!route.startsWith("/i/") && !route.startsWith("/invite/"))continue;
        const auto encoded=route.mid(route.indexOf('/',1)+1);
        if(invalidPercent.match(encoded).hasMatch())continue;
        QStringDecoder decoder(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);
        const QString code=decoder(QByteArray::fromPercentEncoding(encoded.toUtf8()));
        if(!decoder.hasError() && code.size()>=16 && code.size()<=256 && !invalidCode.match(code).hasMatch())
            return Link{origin,code};
    }
    return std::nullopt;
}

inline std::optional<Link> readEntry(const QString& text) {
    if(auto link=read(text))return link;
    const auto value=text.trimmed();
    if(!value.startsWith("https://",Qt::CaseInsensitive) && !value.startsWith("http://",Qt::CaseInsensitive))
        return std::nullopt;
    const auto origin=DesktopProfile::origin(value);
    return origin.isEmpty()?std::nullopt:std::optional<Link>(Link{origin,{}});
}

// Normalize pastes immediately. Let keyboard entry finish before interpreting a partial URL.
template<class Consumer>
auto bind(QLineEdit* input,Consumer receive,bool plainOrigin=true) {
    input->setMaxLength(4096);
    auto normalized=std::make_shared<QString>();
    auto apply=[input,receive,normalized,plainOrigin]{
        if(input->text()==*normalized)return;
        if(auto parsed=plainOrigin?readEntry(input->text()):read(input->text())){receive(*parsed);*normalized=input->text();}
    };
    QObject::connect(input,&QLineEdit::textChanged,input,[input,previous=input->text(),apply,normalized](const QString& value) mutable {
        if(value!=*normalized)normalized->clear();
        const bool typing=value.size()==previous.size()+1 &&
            QString(value).remove(qMax(0,input->cursorPosition()-1),1)==previous;
        if(!typing)apply();
        previous=input->text();
    });
    QObject::connect(input,&QLineEdit::editingFinished,input,apply);
    return apply;
}
}
