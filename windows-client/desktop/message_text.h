#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace MessageText {
inline QString styled(const QString& text,const QString& mask) {
    QString html;
    for(int start=0;start<text.size();){
        const int flag=start<mask.size()?mask[start].digitValue():0;
        int end=start+1;
        while(end<text.size() && (end<mask.size()?mask[end].digitValue():0)==flag)++end;
        auto run=text.mid(start,end-start);
        if(flag==4 && run=="* ")run="• ";
        run=run.toHtmlEscaped();
        if(flag & 1)run="<b>"+run+"</b>";
        if(flag & 2)run="<i>"+run+"</i>";
        html+=run;start=end;
    }
    return html;
}
inline QString links(const QString& text,const QString& format={}) {
    static const QRegularExpression pattern(
        QStringLiteral(R"link((?<![\p{L}\p{N}_/@])(?:https?://|www\.)[^\s<>"\x00-\x1f]+)link"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QString html;
    qsizetype offset = 0;
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        QString value = match.captured();
        while (!value.isEmpty() && QStringLiteral(".,!?;:…").contains(value.back())) value.chop(1);
        for (const auto pair : {QStringLiteral("()"), QStringLiteral("[]"), QStringLiteral("{}")})
            while (value.endsWith(pair.back()) && value.count(pair.back()) > value.count(pair.front())) value.chop(1);
        const QUrl url(value.startsWith("www.") ? "https://" + value : value);
        if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
            (url.scheme() != "https" && url.scheme() != "http")) continue;
        html += styled(text.mid(offset, match.capturedStart()-offset),format.mid(offset));
        html += QStringLiteral("<a style='color:#8fc4f5' href=\"%1\">%2</a>")
                    .arg(QString::fromLatin1(url.toEncoded()).toHtmlEscaped(), styled(value,format.mid(match.capturedStart())));
        offset = match.capturedStart() + value.size();
    }
    return html + styled(text.mid(offset),format.mid(offset));
}

inline QJsonArray moveMentions(const QString& before,const QString& after,const QJsonArray& mentions) {
    int start=0, end=before.size(), nextEnd=after.size();
    while(start<end && start<nextEnd && before[start]==after[start]) ++start;
    while(end>start && nextEnd>start && before[end-1]==after[nextEnd-1]) { --end; --nextEnd; }
    QJsonArray result;
    for(const auto entry:mentions) {
        auto mention=entry.toObject();
        int a=mention["start"].toInt(), b=mention["end"].toInt();
        if(a>=end) { a+=nextEnd-end; b+=nextEnd-end; }
        else if(b>start) continue;
        if(a<0 || b<=a || after.mid(a,b-a)!="@"+mention["label"].toString()) continue;
        mention["start"]=a; mention["end"]=b; result.append(mention);
    }
    return result;
}

inline bool addressed(const QJsonObject& mention, const QJsonObject& member) {
    if (member.isEmpty() || member["blocked"].toBool()) return false;
    if (mention["kind"] == "member") return mention["id"] == member["id"];
    if (mention["kind"] == "everyone") return true;
    if (member["role"] == "owner") return false;
    const auto id = mention["id"];
    if (id == "member" && member["role"] == "member") return true;
    return member["roleIds"].isArray() ? member["roleIds"].toArray().contains(id)
                                       : (member["role"] == "admin" && id == "admin");
}

inline QString body(const QString& text, const QJsonArray& mentions, const QJsonObject& member,const QString& format={}) {
    QString html = "<p style='white-space:pre-wrap;line-height:20.8px;margin:0'>";
    int offset = 0;
    for (const auto entry : mentions) {
        const auto mention = entry.toObject();
        const int start = mention["start"].toInt(-1), end = mention["end"].toInt(-1);
        if (start < offset || end <= start || end > text.size()) continue;
        html += links(text.mid(offset, start-offset),format.mid(offset));
        const auto style = addressed(mention, member) ? "background-color:#285b9d;color:#f1f7ff;font-weight:500"
                                                      : "background-color:#2d403d;color:#c3e5df";
        html += QString("<span style='%1'>%2</span>").arg(style, styled(text.mid(start,end-start),format.mid(start)));
        offset = end;
    }
    return html + links(text.mid(offset),format.mid(offset)) + "</p>";
}
}
