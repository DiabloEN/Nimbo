#pragma once
#include "interface_text.h"
#include <QtWidgets>

class MessageInput : public QPlainTextEdit {
public:
    MessageInput(){
        setObjectName("messageText");setAccessibleName(UiText::Message());setFrameStyle(QFrame::NoFrame);
        setStyleSheet("QPlainTextEdit{background:transparent;border:0;color:#ededed;placeholder-text-color:#727272;font-size:14px;padding:0 6px}");document()->setDocumentMargin(0);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setFixedHeight(20);setTabChangesFocus(true);
        connect(this,&QPlainTextEdit::textChanged,this,[this]{
            if(document()->characterCount()-1>8000){
                auto c=textCursor();c.setPosition(8000);c.movePosition(QTextCursor::End,QTextCursor::KeepAnchor);c.removeSelectedText();
            }
        });
    }
    std::function<void()> submit;
    QString draftText() const {
        auto value=toPlainText();
        for(auto block=document()->begin();block.isValid();block=block.next()){
            if(!block.text().startsWith(QStringLiteral("• ")))continue;
            const auto first=block.begin().fragment().charFormat();
            if(first.intProperty(QTextFormat::UserProperty)==4)value[block.position()]='*';
        }
        return value;
    }
    QString formatMask() const {
        QString result(document()->characterCount()-1, '0');
        for(auto block=document()->begin();block.isValid();block=block.next()){
            for(auto it=block.begin();!it.atEnd();++it){
                const auto fragment=it.fragment();
                const auto style=fragment.charFormat();
                int mask=(style.fontWeight()>=QFont::Bold?1:0)|(style.fontItalic()?2:0);
                const int marker=style.intProperty(QTextFormat::UserProperty);
                if(marker==4 || marker==8)mask=marker;
                result.replace(fragment.position(),fragment.length(),QString(fragment.length(),QChar('0'+mask)));
            }
        }
        while(result.endsWith('0'))result.chop(1);
        return result;
    }
    void setDraft(const QString& value,const QString& mask){
        QSignalBlocker guard(this);
        clear();setCurrentCharFormat({});
        auto visible=value.left(8000);
        for(int i=0;i+1<visible.size() && i+1<mask.size();++i)
            if((!i || visible[i-1]=='\n') && visible.mid(i,2)=="* " && mask.mid(i,2)=="44")
                visible[i]=QChar(0x2022);
        setPlainText(visible);
        const int length=document()->characterCount()-1;
        QTextCursor cursor(document());
        for(int i=0;i<qMin(mask.size(),length);){
            int end=i+1;
            while(end<mask.size() && end<length && mask[end]==mask[i])++end;
            int flag=mask[i].digitValue();
            if(flag>0){
                QTextCharFormat style;
                style.setFontWeight(flag&1?QFont::Bold:QFont::Normal);
                style.setFontItalic(flag&2);
                if(flag==4 || flag==8)style.setProperty(QTextFormat::UserProperty,flag);
                cursor.setPosition(i);cursor.setPosition(end,QTextCursor::KeepAnchor);cursor.setCharFormat(style);
            }
            i=end;
        }
        document()->clearUndoRedoStacks();
        moveCursor(QTextCursor::End);
        if(currentCharFormat().intProperty(QTextFormat::UserProperty))setCurrentCharFormat({});
    }
protected:
    QMimeData* createMimeDataFromSelection() const override{
        auto* data=QPlainTextEdit::createMimeDataFromSelection();
        const auto cursor=textCursor();
        data->setHtml(cursor.selection().toHtml());
        data->setText(draftText().mid(cursor.selectionStart(),cursor.selectionEnd()-cursor.selectionStart()));
        data->setData("application/x-nimbo-format",formatMask().mid(cursor.selectionStart(),cursor.selectionEnd()-cursor.selectionStart()).toLatin1());
        return data;
    }
    void insertFromMimeData(const QMimeData* data)override{
        if(!data->hasText())return;
        QString value=data->text();value.replace("\r\n","\n");
        if(data->hasFormat("application/x-nimbo-format") || data->hasHtml()){
            MessageInput copy;
            if(data->hasFormat("application/x-nimbo-format")){
                copy.setDraft(value,QString::fromLatin1(data->data("application/x-nimbo-format")));
            }else{
                // Import text and supported emphasis only. Never embed clipboard resources.
                QTextDocument parsed;parsed.setHtml(data->html());
                QString plain,mask;
                for(auto block=parsed.begin();block.isValid();block=block.next()){
                    if(block!=parsed.begin()){plain+='\n';mask+='0';}
                    if(auto* list=block.textList()){
                        const bool numbered=list->format().style()==QTextListFormat::ListDecimal;
                        const auto prefix=numbered?QString::number(list->itemNumber(block)+list->format().start())+". ":QString("* ");
                        plain+=prefix;mask+=QString(prefix.size(),numbered?'8':'4');
                    }
                    for(auto it=block.begin();!it.atEnd();++it){
                        auto frag=it.fragment();auto run=frag.text();run.remove(QChar::ObjectReplacementCharacter);
                        plain+=run;const auto fmt=frag.charFormat();
                        mask+=QString(run.size(),QChar('0'+(fmt.fontWeight()>=QFont::Bold?1:0)+(fmt.fontItalic()?2:0)));
                    }
                }
                copy.setDraft(plain,mask);
            }
            auto source=copy.textCursor();source.select(QTextCursor::Document);
            auto target=textCursor();target.insertFragment(source.selection());setTextCursor(target);
        }else insertPlainText(value);
    }
    void keyPressEvent(QKeyEvent* e)override{
        if(e->matches(QKeySequence::Bold)||e->matches(QKeySequence::Italic)){
            QTextCharFormat style;
            if(e->matches(QKeySequence::Bold))style.setFontWeight(currentCharFormat().fontWeight()>=QFont::Bold?QFont::Normal:QFont::Bold);
            else style.setFontItalic(!currentCharFormat().fontItalic());
            mergeCurrentCharFormat(style);return;
        }
        auto cursor=textCursor();
        const bool newline=e->key()==Qt::Key_Return||e->key()==Qt::Key_Enter;
        if(cursor.hasSelection() && newline && (!submit || e->modifiers().testFlag(Qt::ShiftModifier))){
            cursor.beginEditBlock();cursor.removeSelectedText();
            QKeyEvent same(e->type(),e->key(),e->modifiers(),e->text());setTextCursor(cursor);
            keyPressEvent(&same);cursor.endEditBlock();return;
        }
        const auto line=cursor.block().text();
        static const QRegularExpression marker(R"(^(\*|1|\d{1,8}\.)$)");
        static const QRegularExpression list(R"(^(•|\d{1,8}\.) )");
        const auto prefix=list.match(line).captured();
        if(e->text()==" "&&!cursor.hasSelection()&&marker.match(line.left(cursor.positionInBlock())).hasMatch()){
            const auto typed=line.left(cursor.positionInBlock());
            cursor.beginEditBlock();cursor.movePosition(QTextCursor::StartOfBlock,QTextCursor::KeepAnchor);
            QTextCharFormat fmt;fmt.setProperty(QTextFormat::UserProperty,typed=="*"?4:8);
            cursor.insertText(typed=="*"?"• ":typed=="1"?"1. ":typed+" ",fmt);cursor.endEditBlock();setTextCursor(cursor);setCurrentCharFormat({});return;
        }
        if(!prefix.isEmpty() && formatMask().mid(cursor.block().position(),1)==(prefix.startsWith(QChar(0x2022))?"4":"8")){
            if((newline && (!submit || e->modifiers().testFlag(Qt::ShiftModifier))) ||
               (e->key()==Qt::Key_Backspace && !cursor.hasSelection() && cursor.positionInBlock()==prefix.size())){
                cursor.beginEditBlock();
                if(e->key()==Qt::Key_Backspace || line==prefix){
                    cursor.movePosition(QTextCursor::StartOfBlock);cursor.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor,prefix.size());cursor.removeSelectedText();
                }else{
                    const auto next=prefix.startsWith(QChar(0x2022))?prefix:QString::number(prefix.chopped(2).toInt()+1)+". ";
                    if(next.size()<=10){cursor.insertBlock();QTextCharFormat fmt;fmt.setProperty(QTextFormat::UserProperty,prefix.startsWith(QChar(0x2022))?4:8);cursor.insertText(next,fmt);}
                }
                cursor.endEditBlock();setTextCursor(cursor);setCurrentCharFormat({});return;
            }
        }
        if(submit && newline&&!e->modifiers().testFlag(Qt::ShiftModifier)){
            if(submit)submit();e->accept();return;
        }
        if(newline){cursor.insertBlock();setTextCursor(cursor);return;}
        QPlainTextEdit::keyPressEvent(e);
    }
};

